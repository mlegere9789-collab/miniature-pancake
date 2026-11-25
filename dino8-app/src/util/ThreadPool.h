// A minimal header-only thread pool for the handful of genuinely
// data-parallel batch jobs in Dino 8 (per-object display-mesh warmup, batch
// mesh/boolean tool placement, per-object file I/O serialization).
//
// Deliberately NOT used for anything that mutates shared Document state
// (Document::Add/Remove/BeginChange, the undo/redo stacks, the object
// vector itself) - those stay single-threaded on the calling thread. Callers
// hand this pool independent, read-mostly per-item work (e.g. "tessellate
// this one object" or "boolean-cut this one placement against its own copy
// of the tool"); the caller then applies results back into the Document
// serially. See callers in Viewport.cpp (EnsureDisplay warmup) and
// cmd_solidtools.cpp (ArrayHole-style batch placement) for the pattern.
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <future>
#include <thread>
#include <vector>

namespace dino8::app {

// ParallelFor: splits [0, count) into contiguous chunks (one per hardware
// thread, capped at `count`) and runs `body(i)` for every index, waiting for
// all chunks to finish before returning. Runs `body` on the calling thread
// with no extra threads when `count` is small or the machine is single-core,
// so callers do not need to special-case that.
//
// `body` must not touch anything another concurrently-running index could
// also touch (each SceneObject's own `mutable` display cache is fine - two
// different objects never share one); it must not mutate the Document's
// object list, layer list, undo stack, or anything else shared across
// objects.
template <typename Body>
void ParallelFor(std::size_t count, Body&& body) {
  if (count == 0) return;
  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  const std::size_t worker_count = std::min<std::size_t>(hw, count);
  if (worker_count <= 1) {
    for (std::size_t i = 0; i < count; ++i) body(i);
    return;
  }
  const std::size_t chunk = (count + worker_count - 1) / worker_count;
  std::vector<std::future<void>> futures;
  futures.reserve(worker_count);
  for (std::size_t w = 0; w < worker_count; ++w) {
    const std::size_t begin = w * chunk;
    const std::size_t end = std::min(count, begin + chunk);
    if (begin >= end) break;
    futures.push_back(std::async(std::launch::async, [begin, end, &body]() {
      for (std::size_t i = begin; i < end; ++i) body(i);
    }));
  }
  for (std::future<void>& f : futures) f.get();
}

}  // namespace dino8::app
