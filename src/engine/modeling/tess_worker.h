#pragma once

#include "engine/base/result.h"
#include "engine/graphics/mesh.h"
#include "engine/render/lod_request.h"
#include "engine/render/mesh_lod.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tamias {

struct TessJobResult {
  std::uint64_t geometry_id = 0;
  MeshLod lod = MeshLod::Work;
  Result<MeshCpu> mesh = Err("unset");
};

// Single background tessellate queue (OCCT is not used on the UI or render thread).
class TessWorker {
 public:
  static TessWorker& instance();

  TessWorker(const TessWorker&) = delete;
  TessWorker& operator=(const TessWorker&) = delete;

  void enqueue(std::uint64_t geometry_id, MeshLod lod, std::function<Result<MeshCpu>()> run);
  // 没有后台线程的构建（Emscripten 默认不带 pthread）靠这个推进队列：每次最多跑
  // max_jobs 个任务，由壳按帧调用。桌面有 worker 线程，这里是 no-op（返回 0）。
  // 用它而不是在 enqueue 里直接跑，是为了把一帧的工作量摊到多帧，避免卡住主线程。
  std::size_t pump(std::size_t max_jobs);
  [[nodiscard]] std::vector<TessJobResult> take_completed();
  void cancel_geometry(std::uint64_t geometry_id);
  void cancel_all();
  void wait_idle();
  void shutdown();

 private:
  TessWorker();
  ~TessWorker();
  void thread_main();
  // 取出并执行一个任务；队列空时返回 false。桌面由 worker 线程调用，
  // Emscripten 由 pump() 在主线程按帧调用。
  bool step();

  struct Job {
    std::uint64_t geometry_id = 0;
    MeshLod lod = MeshLod::Work;
    std::function<Result<MeshCpu>()> run;
  };

  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<Job> jobs_;
  std::vector<TessJobResult> completed_;
  std::thread thread_;
  bool stop_ = false;
  std::uint32_t in_flight_ = 0;
};

}  // namespace tamias
