#include "engine/modeling/tess_worker.h"

namespace tamias {

TessWorker& TessWorker::instance() {
  static TessWorker worker;
  return worker;
}

TessWorker::TessWorker() {
#if !defined(__EMSCRIPTEN__)
  thread_ = std::thread([this] { thread_main(); });
#endif
}

TessWorker::~TessWorker() { shutdown(); }

void TessWorker::enqueue(std::uint64_t geometry_id, MeshLod lod,
                         std::function<Result<MeshCpu>()> run) {
  if (!run || geometry_id == 0) {
    return;
  }
#if defined(__EMSCRIPTEN__)
  TessJobResult result;
  result.geometry_id = geometry_id;
  result.lod = lod;
  result.mesh = run();
  std::scoped_lock lock(mutex_);
  if (!stop_) {
    completed_.push_back(std::move(result));
  }
#else
  {
    std::scoped_lock lock(mutex_);
    if (stop_) {
      return;
    }
    jobs_.push(Job{geometry_id, lod, std::move(run)});
  }
  cv_.notify_one();
#endif
}

std::vector<TessJobResult> TessWorker::take_completed() {
  std::scoped_lock lock(mutex_);
  std::vector<TessJobResult> out;
  out.swap(completed_);
  return out;
}

void TessWorker::cancel_geometry(std::uint64_t geometry_id) {
  std::scoped_lock lock(mutex_);
  std::queue<Job> kept;
  while (!jobs_.empty()) {
    Job job = std::move(jobs_.front());
    jobs_.pop();
    if (job.geometry_id != geometry_id) {
      kept.push(std::move(job));
    }
  }
  jobs_ = std::move(kept);
}

void TessWorker::cancel_all() {
  std::scoped_lock lock(mutex_);
  while (!jobs_.empty()) {
    jobs_.pop();
  }
}

void TessWorker::wait_idle() {
#if defined(__EMSCRIPTEN__)
  (void)0;
#else
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return jobs_.empty() && in_flight_ == 0; });
#endif
}

void TessWorker::shutdown() {
  {
    std::scoped_lock lock(mutex_);
    if (stop_) {
      return;
    }
    stop_ = true;
  }
#if !defined(__EMSCRIPTEN__)
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
#endif
}

void TessWorker::thread_main() {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
      if (stop_ && jobs_.empty()) {
        return;
      }
      job = std::move(jobs_.front());
      jobs_.pop();
      ++in_flight_;
    }
    TessJobResult result;
    result.geometry_id = job.geometry_id;
    result.lod = job.lod;
    result.mesh = job.run ? job.run() : Err("empty tessellate job");
    {
      std::scoped_lock lock(mutex_);
      completed_.push_back(std::move(result));
      --in_flight_;
    }
    cv_.notify_all();
  }
}

}  // namespace tamias
