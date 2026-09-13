//! Tamias ↔ Truck 桥（C ABI）。
//!
//! 这半边只做三件事：持有 Truck 的拓扑对象（handle table）、把动词转发给 Truck、
//! 把结果（网格 / 边的测量）序列化成 POD 交回 C++。业务逻辑（特征树、指纹匹配、
//! 错误文案）一律不在这里——那是中间层的事。
//!
//! 规矩：
//! - 每个导出函数都用 `guard` 包住，panic 绝不穿 FFI，转成错误码 + `truck_last_error()`。
//! - 跨边界只有 POD：`#[repr(C)]` 结构体、裸指针 + 长度。不传 String / Vec / Option。
//! - 网格缓冲由 Rust 分配，C++ 拷走之后调 `truck_verts_free` / `truck_indices_free`。

use std::cell::RefCell;
use std::collections::hash_map::DefaultHasher;
use std::collections::HashMap;
use std::ffi::{c_char, CString};
use std::hash::{Hash, Hasher};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};

use truck_meshalgo::prelude::*;
use truck_modeling::*;
use truck_shapeops::{and, or};

// Truck 自己导出了 `Result<T>` 别名，这里用别名区分，避免签名被它的错误类型吃掉。
type Res<T> = std::result::Result<T, (i32, String)>;

// 与 C++ 侧 truck_bridge.h 的 TruckEdgeMeasure 一一对应。
#[repr(C)]
pub struct TruckEdgeMeasure {
    pub key: u64,
    pub mid: [f64; 3],
    pub dir: [f64; 3],
    pub has_dir: i32,
    pub length: f64,
    pub n1: [f64; 3],
    pub has_n1: i32,
    pub n2: [f64; 3],
    pub has_n2: i32,
}

pub const TRUCK_OK: i32 = 0;
pub const TRUCK_E_FAILED: i32 = 1;
pub const TRUCK_E_PANIC: i32 = 2;
pub const TRUCK_E_BAD_ARG: i32 = 3;
pub const TRUCK_E_UNSUPPORTED: i32 = 4;
pub const TRUCK_E_BAD_HANDLE: i32 = 5;

enum Shape {
    Face(Face),
    Solid(Solid),
}

static NEXT_ID: AtomicU64 = AtomicU64::new(1);

fn table() -> &'static Mutex<HashMap<u64, Arc<Shape>>> {
    static TABLE: OnceLock<Mutex<HashMap<u64, Arc<Shape>>>> = OnceLock::new();
    TABLE.get_or_init(|| Mutex::new(HashMap::new()))
}

thread_local! {
    static LAST_ERROR: RefCell<CString> =
        RefCell::new(CString::new("ok").unwrap_or_default());
}

fn set_error(msg: &str) {
    let sanitized = msg.replace('\0', " ");
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = CString::new(sanitized).unwrap_or_default();
    });
}

fn guard<F>(body: F) -> i32
where
    F: FnOnce() -> Res<()>,
{
    match catch_unwind(AssertUnwindSafe(body)) {
        Ok(Ok(())) => TRUCK_OK,
        Ok(Err((code, msg))) => {
            set_error(&msg);
            code
        }
        Err(_) => {
            set_error("truck panicked");
            TRUCK_E_PANIC
        }
    }
}

fn store(shape: Shape) -> u64 {
    let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    table().lock().unwrap().insert(id, Arc::new(shape));
    id
}

fn load(handle: u64) -> Res<Arc<Shape>> {
    table()
        .lock()
        .unwrap()
        .get(&handle)
        .cloned()
        .ok_or_else(|| (TRUCK_E_BAD_HANDLE, format!("unknown body handle {handle}")))
}

fn point(x: f64, y: f64, z: f64) -> Point3 {
    Point3::new(x, y, z)
}

fn vec3(x: f64, y: f64, z: f64) -> Vector3 {
    Vector3::new(x, y, z)
}

// 点列 → 闭合线框 → 平面。轮廓一律落在 Tamias 的 XZ 平面（y = 0）。
fn face_from_loop(points: &[[f64; 3]]) -> Res<Face> {
    if points.len() < 3 {
        return Err((TRUCK_E_BAD_ARG, "a loop needs at least 3 points".into()));
    }
    let vertices: Vec<Vertex> = points
        .iter()
        .map(|p| builder::vertex(point(p[0], p[1], p[2])))
        .collect();
    let count = vertices.len();
    let edges: Vec<Edge> = (0..count)
        .map(|i| builder::line(&vertices[i], &vertices[(i + 1) % count]))
        .collect();
    let wire: Wire = edges.into_iter().collect();
    builder::try_attach_plane(&[wire])
        .map_err(|e| (TRUCK_E_FAILED, format!("attach plane failed: {e:?}")))
}

fn shape_edges(shape: &Shape) -> Vec<Edge> {
    match shape {
        Shape::Face(face) => face.edge_iter().collect(),
        Shape::Solid(solid) => solid.edge_iter().collect(),
    }
}

// 一条边的测量（Tamias Y-up 空间）。闭合边没有稳定方向，位置用采样质心。
fn measure_edge(edge: &Edge) -> Option<TruckEdgeMeasure> {
    let curve = edge.curve();
    let (u0, u1) = curve.range_tuple();
    if !(u1 > u0) {
        return None;
    }
    const SAMPLES: usize = 24;
    let mut points = Vec::with_capacity(SAMPLES + 1);
    for i in 0..=SAMPLES {
        let t = u0 + (u1 - u0) * (i as f64) / (SAMPLES as f64);
        points.push(curve.subs(t));
    }
    let closed = (points[0] - points[SAMPLES]).magnitude() < 1e-9;
    let span = if closed { SAMPLES } else { SAMPLES + 1 };
    let mut sum = vec3(0.0, 0.0, 0.0);
    for p in points.iter().take(span) {
        sum += p.to_vec();
    }
    let centroid = Point3::from_vec(sum / (span as f64));
    let mut length = 0.0;
    for i in 0..span {
        length += (points[(i + 1) % span] - points[i]).magnitude();
    }

    let mut dir = vec3(0.0, 0.0, 0.0);
    let mut has_dir = false;
    if !closed {
        let d = curve.der((u0 + u1) / 2.0);
        if d.magnitude() > 1e-12 {
            dir = d.normalize();
            has_dir = true;
        }
    }

    let mut hasher = DefaultHasher::new();
    edge.id().hash(&mut hasher);

    Some(TruckEdgeMeasure {
        key: hasher.finish(),
        mid: [centroid.x, centroid.y, centroid.z],
        dir: [dir.x, dir.y, dir.z],
        has_dir: has_dir as i32,
        length,
        // 相邻面法线：v1 不做（Truck 的曲面法线要投影到 (u,v)，留到下一步）。
        n1: [0.0; 3],
        has_n1: 0,
        n2: [0.0; 3],
        has_n2: 0,
    })
}

fn write_out(target: *mut u64, handle: u64) -> Res<()> {
    if target.is_null() {
        return Err((TRUCK_E_BAD_ARG, "null out handle".into()));
    }
    unsafe { *target = handle };
    Ok(())
}

#[no_mangle]
pub extern "C" fn truck_version() -> *const c_char {
    static VERSION: &str = "truck 0.6 (tamias-bridge 0.1)\0";
    VERSION.as_ptr() as *const c_char
}

#[no_mangle]
pub extern "C" fn truck_last_error() -> *const c_char {
    LAST_ERROR.with(|slot| slot.borrow().as_ptr())
}

#[no_mangle]
pub extern "C" fn truck_rect_face(width: f64, height: f64, out: *mut u64) -> i32 {
    guard(|| {
        if !(width > 0.0) || !(height > 0.0) {
            return Err((TRUCK_E_BAD_ARG, "rect face needs positive extents".into()));
        }
        let (hw, hh) = (width / 2.0, height / 2.0);
        let face = face_from_loop(&[
            [-hw, 0.0, -hh],
            [hw, 0.0, -hh],
            [hw, 0.0, hh],
            [-hw, 0.0, hh],
        ])?;
        write_out(out, store(Shape::Face(face)))
    })
}

#[no_mangle]
pub extern "C" fn truck_circle_face(radius: f64, out: *mut u64) -> i32 {
    guard(|| {
        if !(radius > 0.0) {
            return Err((TRUCK_E_BAD_ARG, "circle face needs a positive radius".into()));
        }
        let start = builder::vertex(point(0.0, 0.0, radius));
        let wire = builder::rsweep(
            &start,
            Point3::origin(),
            Vector3::unit_y(),
            Rad(std::f64::consts::TAU + 1e-3),
        );
        let face = builder::try_attach_plane(&[wire])
            .map_err(|e| (TRUCK_E_FAILED, format!("attach plane failed: {e:?}")))?;
        write_out(out, store(Shape::Face(face)))
    })
}

#[no_mangle]
pub unsafe extern "C" fn truck_polygon_face(
    xyz: *const f64,
    count: usize,
    out: *mut u64,
) -> i32 {
    guard(|| {
        if xyz.is_null() || count < 3 {
            return Err((TRUCK_E_BAD_ARG, "polygon needs at least 3 points".into()));
        }
        let flat = unsafe { std::slice::from_raw_parts(xyz, count * 3) };
        let points: Vec<[f64; 3]> = (0..count)
            .map(|i| [flat[i * 3], flat[i * 3 + 1], flat[i * 3 + 2]])
            .collect();
        let face = face_from_loop(&points)?;
        write_out(out, store(Shape::Face(face)))
    })
}

#[no_mangle]
pub extern "C" fn truck_extrude(handle: u64, depth: f64, out: *mut u64) -> i32 {
    guard(|| {
        let shape = load(handle)?;
        match shape.as_ref() {
            Shape::Face(face) => {
                let solid = builder::tsweep(face, vec3(0.0, depth, 0.0));
                write_out(out, store(Shape::Solid(solid)))
            }
            Shape::Solid(_) => Err((
                TRUCK_E_UNSUPPORTED,
                "extrude expects a profile face, got a solid".into(),
            )),
        }
    })
}

// op: 0 = Fuse, 1 = Common, 2 = Cut（与 tamias::BooleanOp 一致）。
#[no_mangle]
pub extern "C" fn truck_boolean(a: u64, b: u64, op: i32, out: *mut u64) -> i32 {
    guard(|| {
        let shape_a = load(a)?;
        let shape_b = load(b)?;
        let (Shape::Solid(solid_a), Shape::Solid(solid_b)) = (shape_a.as_ref(), shape_b.as_ref())
        else {
            return Err((
                TRUCK_E_UNSUPPORTED,
                "boolean expects two solids (extrude the profiles first)".into(),
            ));
        };
        // 点重合容差：形状在米级别，1e-4 够用；太大反而会把薄壁吃掉。
        const TOL: f64 = 1e-4;
        let result = match op {
            1 => and(solid_a, solid_b, TOL),
            2 => {
                // Truck 没有单独的 difference：把工具体翻面再求交。
                let mut inverted = solid_b.clone();
                inverted.not();
                and(solid_a, &inverted, TOL)
            }
            _ => or(solid_a, solid_b, TOL),
        };
        let solid = result
            .ok_or_else(|| (TRUCK_E_FAILED, "truck boolean produced no solid".into()))?;
        write_out(out, store(Shape::Solid(solid)))
    })
}

#[no_mangle]
pub extern "C" fn truck_transform(
    handle: u64,
    x: f64,
    y: f64,
    z: f64,
    out: *mut u64,
) -> i32 {
    guard(|| {
        let shape = load(handle)?;
        let moved = match shape.as_ref() {
            Shape::Face(face) => Shape::Face(builder::translated(face, vec3(x, y, z))),
            Shape::Solid(solid) => Shape::Solid(builder::translated(solid, vec3(x, y, z))),
        };
        write_out(out, store(moved))
    })
}

#[no_mangle]
pub extern "C" fn truck_body_release(handle: u64) -> i32 {
    guard(|| {
        table().lock().unwrap().remove(&handle);
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn truck_bounds(handle: u64, min_xyz: *mut f64, max_xyz: *mut f64) -> i32 {
    guard(|| {
        if min_xyz.is_null() || max_xyz.is_null() {
            return Err((TRUCK_E_BAD_ARG, "null bounds buffer".into()));
        }
        let shape = load(handle)?;
        let mut lo = [f64::INFINITY; 3];
        let mut hi = [f64::NEG_INFINITY; 3];
        let mut seen = false;
        for edge in shape_edges(shape.as_ref()) {
            let curve = edge.curve();
            let (u0, u1) = curve.range_tuple();
            for i in 0..=16 {
                let t = u0 + (u1 - u0) * (i as f64) / 16.0;
                let p = curve.subs(t);
                let p = [p.x, p.y, p.z];
                for k in 0..3 {
                    lo[k] = lo[k].min(p[k]);
                    hi[k] = hi[k].max(p[k]);
                }
                seen = true;
            }
        }
        if !seen {
            return Err((TRUCK_E_FAILED, "shape has no edges to measure".into()));
        }
        unsafe {
            std::ptr::copy_nonoverlapping(lo.as_ptr(), min_xyz, 3);
            std::ptr::copy_nonoverlapping(hi.as_ptr(), max_xyz, 3);
        }
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn truck_edge_count(handle: u64, count: *mut usize) -> i32 {
    guard(|| {
        if count.is_null() {
            return Err((TRUCK_E_BAD_ARG, "null count".into()));
        }
        let shape = load(handle)?;
        unsafe { *count = shape_edges(shape.as_ref()).len() };
        Ok(())
    })
}

#[no_mangle]
pub unsafe extern "C" fn truck_measure_edges(
    handle: u64,
    out: *mut TruckEdgeMeasure,
    capacity: usize,
    written: *mut usize,
) -> i32 {
    guard(|| {
        if out.is_null() || written.is_null() {
            return Err((TRUCK_E_BAD_ARG, "null measure buffer".into()));
        }
        let shape = load(handle)?;
        let edges = shape_edges(shape.as_ref());
        if capacity < edges.len() {
            return Err((
                TRUCK_E_BAD_ARG,
                format!("measure buffer too small: {} < {}", capacity, edges.len()),
            ));
        }
        let slice = unsafe { std::slice::from_raw_parts_mut(out, edges.len()) };
        for (i, edge) in edges.iter().enumerate() {
            slice[i] = measure_edge(edge).ok_or_else(|| {
                (TRUCK_E_FAILED, format!("edge {i} has no parameter range"))
            })?;
        }
        unsafe { *written = edges.len() };
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn truck_tessellate(
    handle: u64,
    deflection: f64,
    verts: *mut *mut f32,
    vcount: *mut usize,
    indices: *mut *mut u32,
    icount: *mut usize,
) -> i32 {
    guard(|| {
        if verts.is_null() || vcount.is_null() || indices.is_null() || icount.is_null() {
            return Err((TRUCK_E_BAD_ARG, "null mesh out-params".into()));
        }
        let shape = load(handle)?;
        let solid = match shape.as_ref() {
            Shape::Solid(solid) => solid,
            Shape::Face(_) => {
                return Err((
                    TRUCK_E_UNSUPPORTED,
                    "tessellate expects a solid (extrude the profile first)".into(),
                ));
            }
        };
        let mesh = solid.triangulation(deflection.max(1e-6)).to_polygon();
        let positions = mesh.positions();
        let triangles = mesh.tri_faces();
        if positions.is_empty() || triangles.is_empty() {
            return Err((TRUCK_E_FAILED, "truck produced an empty mesh".into()));
        }

        // 平面着色：每个三角形的法线写给它的三个顶点（Truck 的网格不一定带法线）。
        let mut vertex_data: Vec<f32> = Vec::with_capacity(triangles.len() * 3 * 6);
        let mut index_data: Vec<u32> = Vec::with_capacity(triangles.len() * 3);
        for face in triangles {
            let a = positions[face[0].pos];
            let b = positions[face[1].pos];
            let c = positions[face[2].pos];
            let normal = (b - a).cross(c - a).normalize();
            for p in [a, b, c] {
                vertex_data.extend_from_slice(&[
                    p.x as f32,
                    p.y as f32,
                    p.z as f32,
                    normal.x as f32,
                    normal.y as f32,
                    normal.z as f32,
                ]);
            }
            let base = index_data.len() as u32;
            index_data.extend_from_slice(&[base, base + 1, base + 2]);
        }

        // 注意：vcount 回传的是**顶点数**（每顶点 6 个 f32：位置 + 法线），
        // 不是 float 个数——C++ 侧按顶点数 resize，释放时再乘 6。
        debug_assert_eq!(vertex_data.len() % 6, 0);
        let vertex_len = vertex_data.len();
        let index_len = index_data.len();
        let vertex_ptr = Box::into_raw(vertex_data.into_boxed_slice()) as *mut f32;
        let index_ptr = Box::into_raw(index_data.into_boxed_slice()) as *mut u32;
        unsafe {
            *verts = vertex_ptr;
            *vcount = vertex_len / 6;
            *indices = index_ptr;
            *icount = index_len;
        }
        Ok(())
    })
}

#[no_mangle]
pub unsafe extern "C" fn truck_verts_free(ptr: *mut f32, len: usize) {
    if !ptr.is_null() && len > 0 {
        drop(unsafe { Box::from_raw(std::slice::from_raw_parts_mut(ptr, len)) });
    }
}

#[no_mangle]
pub unsafe extern "C" fn truck_indices_free(ptr: *mut u32, len: usize) {
    if !ptr.is_null() && len > 0 {
        drop(unsafe { Box::from_raw(std::slice::from_raw_parts_mut(ptr, len)) });
    }
}
