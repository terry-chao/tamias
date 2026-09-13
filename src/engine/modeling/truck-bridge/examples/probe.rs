//! 临时探针：确认 Truck 三角化出来的点是不是世界坐标、包围盒对不对。
//! 用法：cargo run --release --example probe
use truck_meshalgo::prelude::*;
use truck_modeling::*;

fn main() {
    let (hw, hh, depth) = (1.5, 0.75, 2.0);
    let pts = [
        (-hw, 0.0, -hh),
        (hw, 0.0, -hh),
        (hw, 0.0, hh),
        (-hw, 0.0, hh),
    ];
    let vs: Vec<Vertex> = pts
        .iter()
        .map(|p| builder::vertex(Point3::new(p.0, p.1, p.2)))
        .collect();
    let edges: Vec<Edge> = (0..4)
        .map(|i| builder::line(&vs[i], &vs[(i + 1) % 4]))
        .collect();
    let wire: Wire = edges.into_iter().collect();
    let face = builder::try_attach_plane(&[wire]).unwrap();
    let solid: Solid = builder::tsweep(&face, Vector3::new(0.0, depth, 0.0));

    let mesh = solid.triangulation(0.05).to_polygon();
    let positions = mesh.positions();
    let mut lo = [f64::MAX; 3];
    let mut hi = [f64::MIN; 3];
    for p in positions {
        let xyz = [p.x, p.y, p.z];
        for k in 0..3 {
            lo[k] = lo[k].min(xyz[k]);
            hi[k] = hi[k].max(xyz[k]);
        }
    }
    println!(
        "mesh verts={} tris={} lo={lo:?} hi={hi:?}",
        positions.len(),
        mesh.tri_faces().len()
    );
    println!("sample {:?}", positions.first());

    let edges: Vec<Edge> = solid.edge_iter().collect();
    let mut elo = [f64::MAX; 3];
    let mut ehi = [f64::MIN; 3];
    for e in &edges {
        let c = e.curve();
        let (u0, u1) = c.range_tuple();
        for i in 0..=16 {
            let t = u0 + (u1 - u0) * (i as f64) / 16.0;
            let p = c.subs(t);
            let xyz = [p.x, p.y, p.z];
            for k in 0..3 {
                elo[k] = elo[k].min(xyz[k]);
                ehi[k] = ehi[k].max(xyz[k]);
            }
        }
    }
    println!("edges={} lo={elo:?} hi={ehi:?}", edges.len());
}
