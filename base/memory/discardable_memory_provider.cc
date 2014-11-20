// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/discardable_memory_provider.h"

#include "base/bind.h"
#include "base/containers/hash_tables.h"
#include "base/containers/mru_cache.h"
#include "base/debug/trace_event.h"
#include "base/lazy_instance.h"
#include "base/memory/discardable_memory.h"
#include "base/synchronization/lock.h"
#include "base/sys_info.h"

namespace base {
namespace internal {

namespace {

static base::LazyInstance<DiscardableMemoryProvider>::Leaky g_provider =
    LAZY_INSTANCE_INITIALIZER;

// If this is given a valid value via SetInstanceForTest, this pointer will be
// returned by GetInstance rather than |g_provider|.
static DiscardableMemoryProvider* g_provider_for_test = NULL;

// This is admittedly pretty magical. It's approximately enough memory for two
// 2560x1600 images.
static const size_t kDefaultDiscardableMemoryLimit = 32 * 1024 * 1024;
static const size_t kDefaultBytesToReclaimUnderModeratePressure =
    kDefaultDiscardableMemoryLimit / 2;

}  // namespace

DiscardableMemoryProvider::DiscardableMemoryProvider()
    : allocations_(AllocationMap::NO_AUTO_EVICT),
      bytes_allocated_(0),
      discardable_memory_limit_(kDefaultDiscardableMemoryLimit),
      bytes_to_reclaim_under_moderate_pressure_(
          kDefaultBytesToReclaimUnderModeratePressure),
      memory_pressure_listener_(
          base::Bind(&DiscardableMemoryProvider::NotifyMemoryPressure)) {
}

DiscardableMemoryProvider::~DiscardableMemoryProvider() {
  DCHECK(allocations_.empty());
  DCHECK_EQ(0u, bytes_allocated_);
}

// static
DiscardableMemoryProvider* DiscardableMemoryProvider::GetInstance() {
  if (g_provider_for_test)
    return g_provider_for_test;
  return g_provider.Pointer();
}

// static
void DiscardableMemoryProvider::SetInstanceForTest(
    DiscardableMemoryProvider* provider) {
  g_provider_for_test = provider;
}

// static
void DiscardableMemoryProvider::NotifyMemoryPressure(
    MemoryPressureListener::MemoryPressureLevel pressure_level) {
  switch (pressure_level) {
    case MemoryPressureListener::MEMORY_PRESSURE_MODERATE:
      DiscardableMemoryProvider::GetInstance()->Purge();
      return;
    case MemoryPressureListener::MEMORY_PRESSURE_CRITICAL:
      DiscardableMemoryProvider::GetInstance()->PurgeAll();
      return;
  }

  NOTREACHED();
}

void DiscardableMemoryProvider::SetDiscardableMemoryLimit(size_t bytes) {
  AutoLock lock(lock_);
  discardable_memory_limit_ = bytes;
  EnforcePolicyWithLockAcquired();
}

void DiscardableMemoryProvider::SetBytesToReclaimUnderModeratePressure(
    size_t bytes) {
  AutoLock lock(lock_);
  bytes_to_reclaim_under_moderate_pressure_ = bytes;
  EnforcePolicyWithLockAcquired();
}

void DiscardableMemoryProvider::Register(
    const DiscardableMemory* discardable, size_t bytes) {
  AutoLock lock(lock_);
  DCHECK(allocations_.Peek(discardable) == allocations_.end());
  allocations_.Put(discardable, Allocation(bytes));
}

void DiscardableMemoryProvider::Unregister(
    const DiscardableMemory* discardable) {
  AutoLock lock(lock_);
  AllocationMap::iterator it = allocations_.Peek(discardable);
  if (it == allocations_.end())
    return;

  if (it->second.memory) {
    size_t bytes = it->second.bytes;
    DCHECK_LE(bytes, bytes_allocated_);
    bytes_allocated_ -= bytes;
    free(it->second.memory);
  }
  allocations_.Erase(it);
}

scoped_ptr<uint8, FreeDeleter> DiscardableMemoryProvider::Acquire(
    const DiscardableMemory* discardable,
    bool* purged) {
  AutoLock lock(lock_);
  // NB: |allocations_| is an MRU cache, and use of |Get| here updates that
  // cache.
  AllocationMap::iterator it = allocations_.Get(discardable);
  CHECK(it != allocations_.end());

  if (it->second.memory) {
    scoped_ptr<uint8, FreeDeleter> memory(it->second.memory);
    it->second.memory = NULL;
    *purged = false;
    return memory.Pass();
  }

  size_t bytes = it->second.bytes;
  if (!bytes)
    return scoped_ptr<uint8, FreeDeleter>();

  if (discardable_memory_limit_) {
    size_t limit = 0;
    if (bytes < discardable_memory_limit_)
      limit = discardable_memory_limit_ - bytes;

    PurgeLRUWithLockAcquiredUntilUsageIsWithin(limit);
  }

  bytes_allocated_ += bytes;
  *purged = true;
  return scoped_ptr<uint8, FreeDeleter>(static_cast<uint8*>(malloc(bytes)));
}

void DiscardableMemoryProvider::Release(
    const DiscardableMemory* discardable,
    scoped_ptr<uint8, FreeDeleter> memory) {
  AutoLock lock(lock_);
  // NB: |allocations_| is an MRU cache, and use of |Get| here updates that
  // cache.
  AllocationMap::iterator it = allocations_.Get(discardable);
  CHECK(it != allocations_.end());

  DCHECK(!it->second.memory);
  it->second.memory = memory.release();

  EnforcePolicyWithLockAcquired();
}

void DiscardableMemoryProvider::PurgeAll() {
  AutoLock lock(lock_);
  PurgeLRUWithLockAcquiredUntilUsageIsWithin(0);
}

bool DiscardableMemoryProvider::IsRegisteredForTest(
    const DiscardableMemory* discardable) const {
  AutoLock lock(lock_);
  AllocationMap::const_iterator it = allocations_.Peek(discardable);
  return it != allocations_.end();
}

bool DiscardableMemoryProvider::CanBePurgedForTest(
    const DiscardableMemory* discardable) const {
  AutoLock lock(lock_);
  AllocationMap::const_iterator it = allocations_.Peek(discardable);
  return it != allocations_.end() && it->second.memory;
}

size_t DiscardableMemoryProvider::GetBytesAllocatedForTest() const {
  AutoLock lock(lock_);
  return bytes_allocated_;
}

void DiscardableMemoryProvider::Purge() {
  AutoLock lock(lock_);

  if (bytes_to_reclaim_under_moderate_pressure_ == 0)
    return;

  size_t limit = 0;
  if (bytes_to_reclaim_under_moderate_pressure_ < discardable_memory_limit_)
    limit = bytes_allocated_ - bytes_to_reclaim_under_moderate_pressure_;

  PurgeLRUWithLockAcquiredUntilUsageIsWithin(limit);
}

void DiscardableMemoryProvider::PurgeLRUWithLockAcquiredUntilUsageIsWithin(
    size_t limit) {
  TRACE_EVENT1(
      "base",
      "DiscardableMemoryProvider::PurgeLRUWithLockAcquiredUntilUsageIsWithin",
      "limit", limit);

  lock_.AssertAcquired();

  for (AllocationMap::reverse_iterator it = allocations_.rbegin();
       it != allocations_.rend();
       ++it) {
    if (bytes_allocated_ <= limit)
      break;
    if (!it->second.memory)
      continue;

    size_t bytes = it->second.bytes;
    DCHECK_LE(bytes, bytes_allocated_);
    bytes_allocated_ -= bytes;
    free(it->second.memory);
    it->second.memory = NULL;
  }
}

void DiscardableMemoryProvider::EnforcePolicyWithLockAcquired() {
  lock_.AssertAcquired();

  bool exceeded_bound = bytes_allocated_ > discardable_memory_limit_;
  if (!exceeded_bound || !bytes_to_reclaim_under_moderate_pressure_)
    return;

  size_t limit = 0;
  if (bytes_to_reclaim_under_moderate_pressure_ < discardable_memory_limit_)
    limit = bytes_allocated_ - bytes_to_reclaim_under_moderate_pressure_;

  PurgeLRUWithLockAcquiredUntilUsageIsWithin(limit);
}

}  // namespace internal
}  // namespace base
