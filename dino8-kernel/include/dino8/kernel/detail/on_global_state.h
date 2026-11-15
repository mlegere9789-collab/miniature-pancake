#pragma once

// Process-wide guard for the few openNURBS data that are mutable statics: the error and warning counters
// (ON_ERROR_COUNT and friends, bumped by every ON_ERROR / ON_WARNING), the serial number generators of every hash
// table and component manifest (constructed with each ONX_Model and each archive), the class-id UUID written by
// ON_ClassId::Create() while a .3dm is read, the bundled libuuid and time zone state behind ON_CreateUuid and the
// revision-history clock, the lazily set offsets and the debug serial counter of the SubD heap, and the font
// tables behind annotation text. openNURBS keeps them as plain integers and plain pointers (see
// dino8-core/KERNEL_THREADS.md section 2.3), so two threads running file I/O or SubD code at the same time race on them.
//
// Not part of the public dino8::kernel API: implementation plumbing in dino8::kernel::detail. The kernel's .3dm
// model code (file_io.cpp) and its SubD code (subd.cpp) hold the guard for the duration of every entry point that
// reaches those statics, so independent calls made from different threads are safe without the caller taking any
// lock. The mutex is recursive because entry points call each other (SubD::BooleanSubD calls SubD::Subdivide, ...);
// it is never held across a wait for another thread (the kernel starts no threads and Manifold runs with
// MANIFOLD_PAR=-1), and it is the only lock the guard takes, so it cannot take part in a lock-order cycle.
// Calls that never reach those statics (tessellation, mass properties, booleans, fillets, sweeps, hull, mesh and
// point-cloud queries) do not take it and still run fully in parallel.

#include <mutex>

namespace dino8::kernel::detail {

inline std::recursive_mutex& OnGlobalStateMutex() {
  // Heap-allocated and never freed: a thread that is still inside a kernel call during process teardown must not
  // find the mutex already destroyed.
  static std::recursive_mutex* const m = new std::recursive_mutex();
  return *m;
}

class OnGlobalStateGuard {
 public:
  OnGlobalStateGuard() : lock_(OnGlobalStateMutex()) {}
  OnGlobalStateGuard(const OnGlobalStateGuard&) = delete;
  OnGlobalStateGuard& operator=(const OnGlobalStateGuard&) = delete;

 private:
  std::lock_guard<std::recursive_mutex> lock_;
};

// Holds the guard across the whole construction and the whole destruction of an object that owns an ONX_Model, whose
// constructor and destructor both reach the statics above (hash table and manifest serial numbers, the clock, the
// error counter in ONX_Model::Reset()). C++ destroys members in reverse declaration order, so declare, in this order,
//   OnGlobalStateConstructionLock first;  ONX_Model;  OnGlobalStateDestructionLock last;
// and call EndConstruction() as the last statement of every constructor of the owner. The first member locks before the
// ONX_Model is built (and unlocks if that construction throws); the last member locks first when destruction starts, and
// the first member unlocks after the ONX_Model is gone. Both members must be neither copied nor moved (the owner is
// not copyable: ONX_Model is not).
class OnGlobalStateConstructionLock {
 public:
  OnGlobalStateConstructionLock() { OnGlobalStateMutex().lock(); }
  OnGlobalStateConstructionLock(const OnGlobalStateConstructionLock&) = delete;
  OnGlobalStateConstructionLock& operator=(const OnGlobalStateConstructionLock&) = delete;
  ~OnGlobalStateConstructionLock() { OnGlobalStateMutex().unlock(); }  // pairs with the lock taken by the destruction member
  void EndConstruction() { OnGlobalStateMutex().unlock(); }              // pairs with the lock taken by this constructor
};

class OnGlobalStateDestructionLock {
 public:
  OnGlobalStateDestructionLock() = default;
  OnGlobalStateDestructionLock(const OnGlobalStateDestructionLock&) = delete;
  OnGlobalStateDestructionLock& operator=(const OnGlobalStateDestructionLock&) = delete;
  ~OnGlobalStateDestructionLock() { OnGlobalStateMutex().lock(); }
};

}  // namespace dino8::kernel::detail
