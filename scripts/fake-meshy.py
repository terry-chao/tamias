# 假的 Meshy 服务端：只为验证「建任务 → 轮询 → 下载 → 导入」这条回路。
# 用法：python fake-meshy.py <port> <log.jsonl>
#
# 行为：
#   POST /openapi/v2/text-to-3d       -> {"result": "<task id>"}（缺 Bearer 头回 401）
#   GET  /openapi/v2/text-to-3d/<id>  -> 第 1 次 IN_PROGRESS 35%，之后 SUCCEEDED
#   GET  /asset/model.glb             -> 一个真的最小 GLB（tamias 的 loader 能吃进去）
#   GET  /openapi/v1/balance          -> {"balance": 1000}
import datetime
import http.server
import json
import struct
import sys

PORT = int(sys.argv[1])
LOG = sys.argv[2]
TASK_ID = "fake-task-1"


# 与 tests/import_tests.cpp 里那份夹具同一形状：一个三角形的最小 GLB。
def make_glb():
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
             "max": [1, 1, 0], "min": [0, 0, 0]},
            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 6},
        ],
        "buffers": [{"byteLength": 44}],
    }
    text = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    text += b" " * ((4 - len(text) % 4) % 4)
    blob = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0) + struct.pack("<3H", 0, 1, 2)
    blob += b"\x00" * ((4 - len(blob) % 4) % 4)
    chunks = struct.pack("<I4s", len(text), b"JSON") + text
    chunks += struct.pack("<I4s", len(blob), b"BIN\x00") + blob
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


MODEL_GLB = make_glb()


def log_request(handler, body):
    with open(LOG, "a", encoding="utf-8") as handle:
        handle.write(json.dumps({
            "at": datetime.datetime.now().isoformat(timespec="seconds"),
            "method": handler.command,
            "path": handler.path,
            "authorized": bool(handler.headers.get("Authorization")),
            "headers": {k: v for k, v in handler.headers.items()},
            "body": body,
        }, ensure_ascii=False) + "\n")


def send(handler, status, payload, content_type="application/json"):
    data = payload if isinstance(payload, bytes) else json.dumps(payload).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", content_type)
    handler.send_header("Content-Length", str(len(data)))
    handler.end_headers()
    handler.wfile.write(data)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    polls = 0

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length).decode("utf-8")
        try:
            body = json.loads(raw)
        except json.JSONDecodeError:
            body = {"raw": raw}
        log_request(self, body)
        if not self.headers.get("Authorization"):
            send(self, 401, {"message": "missing api key"})
            return
        send(self, 200, {"result": TASK_ID})

    def do_GET(self):
        log_request(self, None)
        if self.path.startswith("/openapi/v2/text-to-3d/"):
            if not self.headers.get("Authorization"):
                send(self, 401, {"message": "missing api key"})
                return
            Handler.polls += 1
            if Handler.polls < 2:
                send(self, 200, {"id": TASK_ID, "status": "IN_PROGRESS", "progress": 35})
                return
            send(self, 200, {
                "id": TASK_ID,
                "status": "SUCCEEDED",
                "progress": 100,
                "credits_consumed": 5,
                "thumbnail_url": f"http://127.0.0.1:{PORT}/asset/thumb.png",
                "model_urls": {"glb": f"http://127.0.0.1:{PORT}/asset/model.glb"},
            })
            return
        if self.path == "/asset/model.glb":
            send(self, 200, MODEL_GLB, "model/gltf-binary")
            return
        if self.path == "/openapi/v1/balance":
            send(self, 200, {"balance": 1000})
            return
        send(self, 404, {"message": "not found"})

    def log_message(self, *args):
        pass  # 控制台保持安静，证据都进 jsonl


if __name__ == "__main__":
    http.server.HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
