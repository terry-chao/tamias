#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/render/lod_request.h"
#include "engine/render/mesh_lod.h"

#include <condition_variable>
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
  [[nodiscard]] std::vector<TessJobResult> take_completed();
  void cancel_geometry(std::uint64_t geometry_id);
  void cancel_all();
  void wait_idle();
  void shutdown();

 private:
  TessWorker();
  ~TessWorker();
  void thread_main();

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
