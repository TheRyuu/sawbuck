// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "syzygy/agent/asan/asan_heap.h"

#include "base/rand_util.h"
#include "base/sha1.h"
#include "gtest/gtest.h"
#include "syzygy/agent/asan/asan_logger.h"
#include "syzygy/agent/asan/asan_shadow.h"
#include "syzygy/agent/asan/unittest_util.h"

namespace agent {
namespace asan {

namespace {

// A derived class to expose protected members for unit-testing.
class TestHeapProxy : public HeapProxy {
 public:
  using HeapProxy::BlockHeader;
  using HeapProxy::FindAddressBlock;
  using HeapProxy::FreeBlockHeader;
  using HeapProxy::GetAllocSize;
  using HeapProxy::GetBadAccessKind;
  using HeapProxy::ToBlock;

  explicit TestHeapProxy(StackCaptureCache* stack_cache, AsanLogger* logger)
      : HeapProxy(stack_cache, logger) {
  }

  // Verify that the access to @p addr contained in @p header is an underflow.
  bool IsUnderflowAccess(uint8* addr, BlockHeader* header) {
    return GetBadAccessKind(addr, header) == HEAP_BUFFER_UNDERFLOW;
  }

  // Verify that the access to @p addr contained in @p header is an overflow.
  bool IsOverflowAccess(uint8* addr, BlockHeader* header) {
    return GetBadAccessKind(addr, header) == HEAP_BUFFER_OVERFLOW;
  }

  // Verify that the access to @p addr contained in @p header is an use after
  // free.
  bool IsUseAfterAccess(uint8* addr, BlockHeader* header) {
    return GetBadAccessKind(addr, header) == USE_AFTER_FREE;
  }

  // Determines if the address @p mem corresponds to a block in quarantine.
  bool InQuarantine(const void* mem) {
    base::AutoLock lock(lock_);
    FreeBlockHeader* current_block = head_;
    while (current_block != NULL) {
      void* block_alloc = static_cast<void*>(ToAlloc(current_block));
      EXPECT_TRUE(block_alloc != NULL);
      if (block_alloc == mem)
        return true;
      current_block = current_block->next;
    }
    return false;
  }
};

class HeapTest : public testing::TestWithAsanLogger {
 public:
  HeapTest() : stack_cache_(&logger_), proxy_(&stack_cache_, &logger_) {
  }

  virtual void SetUp() OVERRIDE {
    testing::TestWithAsanLogger::SetUp();

    HeapProxy::Init();

    logger_.set_instance_id(instance_id());
    logger_.Init();
    ASSERT_TRUE(proxy_.Create(0, 0, 0));
  }

  virtual void TearDown() OVERRIDE {
    ASSERT_TRUE(proxy_.Destroy());
    testing::TestWithAsanLogger::TearDown();
  }

  // Verifies that [alloc, alloc + size) is accessible, and that
  // [alloc - 1] and [alloc+size] are poisoned.
  void VerifyAllocAccess(void* alloc, size_t size) {
    uint8* mem = reinterpret_cast<uint8*>(alloc);
    ASSERT_FALSE(Shadow::IsAccessible(mem - 1));
    for (size_t i = 0; i < size; ++i)
      ASSERT_TRUE(Shadow::IsAccessible(mem + i));
    ASSERT_FALSE(Shadow::IsAccessible(mem + size));
  }

  // Verifies that [alloc-1, alloc+size] is poisoned.
  void VerifyFreedAccess(void* alloc, size_t size) {
    uint8* mem = reinterpret_cast<uint8*>(alloc);
    ASSERT_FALSE(Shadow::IsAccessible(mem - 1));
    for (size_t i = 0; i < size; ++i)
      ASSERT_FALSE(Shadow::IsAccessible(mem + i));
    ASSERT_FALSE(Shadow::IsAccessible(mem + size));
  }

  void RandomSetMemory(void* alloc, size_t size) {
    base::RandBytes(alloc, size);
  }

 protected:
  // Arbitrary constant for all size limit.
  static const size_t kMaxAllocSize = 134584;

  AsanLogger logger_;
  StackCaptureCache stack_cache_;
  TestHeapProxy proxy_;
};

}  // namespace

TEST_F(HeapTest, ToFromHandle) {
  HANDLE handle = HeapProxy::ToHandle(&proxy_);
  ASSERT_TRUE(handle != NULL);
  ASSERT_EQ(&proxy_, HeapProxy::FromHandle(handle));
}

TEST_F(HeapTest, SetQuarantineMaxSize) {
  size_t quarantine_size = proxy_.quarantine_max_size() * 2;
  // Increments the quarantine max size if it was set to 0.
  if (quarantine_size == 0)
    quarantine_size++;
  DCHECK_GT(quarantine_size, 0U);
  proxy_.set_quarantine_max_size(quarantine_size);
  ASSERT_EQ(quarantine_size, proxy_.quarantine_max_size());
}

TEST_F(HeapTest, PopOnSetQuarantineMaxSize) {
  const size_t kAllocSize = 100;
  const size_t real_alloc_size = TestHeapProxy::GetAllocSize(kAllocSize);
  LPVOID mem = proxy_.Alloc(0, kAllocSize);
  ASSERT_FALSE(proxy_.InQuarantine(mem));
  proxy_.set_quarantine_max_size(real_alloc_size);
  ASSERT_TRUE(proxy_.Free(0, mem));
  // The quarantine is just large enough to keep this block.
  ASSERT_TRUE(proxy_.InQuarantine(mem));
  // We resize the quarantine to a smaller size, the block should pop out.
  proxy_.set_quarantine_max_size(real_alloc_size - 1);
  ASSERT_FALSE(proxy_.InQuarantine(mem));
}

TEST_F(HeapTest, Quarantine) {
  const size_t kAllocSize = 100;
  const size_t real_alloc_size = TestHeapProxy::GetAllocSize(kAllocSize);
  const size_t number_of_allocs = 16;
  proxy_.set_quarantine_max_size(real_alloc_size * number_of_allocs);

  LPVOID mem = proxy_.Alloc(0, kAllocSize);
  ASSERT_TRUE(mem != NULL);
  ASSERT_TRUE(proxy_.Free(0, mem));
  // Allocate a bunch of blocks until the first one is pushed out of the
  // quarantine.
  for (size_t i = 0;i < number_of_allocs; ++i) {
    ASSERT_TRUE(proxy_.InQuarantine(mem));
    LPVOID mem2 = proxy_.Alloc(0, kAllocSize);
    ASSERT_TRUE(mem2 != NULL);
    ASSERT_TRUE(proxy_.Free(0, mem2));
    ASSERT_TRUE(proxy_.InQuarantine(mem2));
  }

  ASSERT_FALSE(proxy_.InQuarantine(mem));
}

TEST_F(HeapTest, AllocFree) {
  const size_t kAllocSize = 100;
  LPVOID mem = proxy_.Alloc(0, kAllocSize);
  ASSERT_TRUE(mem != NULL);
  ASSERT_EQ(kAllocSize, proxy_.Size(0, mem));
  const size_t kReAllocSize = 2 * kAllocSize;
  mem = proxy_.ReAlloc(0, mem, kReAllocSize);
  ASSERT_EQ(kReAllocSize, proxy_.Size(0, mem));
  ASSERT_TRUE(proxy_.Free(0, mem));
}

TEST_F(HeapTest, DoubleFree) {
  const size_t kAllocSize = 100;
  // Ensure that the quarantine is large enough to keep this block, this is
  // needed for the use-after-free check.
  proxy_.set_quarantine_max_size(TestHeapProxy::GetAllocSize(kAllocSize));
  LPVOID mem = proxy_.Alloc(0, kAllocSize);
  ASSERT_TRUE(mem != NULL);
  ASSERT_TRUE(proxy_.Free(0, mem));
  ASSERT_FALSE(proxy_.Free(0, mem));
  ASSERT_TRUE(LogContains("double-free"));
}

TEST_F(HeapTest, AllocsAccessibility) {
  // Ensure that the quarantine is large enough to keep the allocated blocks in
  // this test.
  proxy_.set_quarantine_max_size(kMaxAllocSize * 2);
  for (size_t size = 10; size < kMaxAllocSize; size = size * 5 + 123) {
    // Do an alloc/realloc/free and test that access is correctly managed.
    void* mem = proxy_.Alloc(0, size);
    ASSERT_TRUE(mem != NULL);
    ASSERT_NO_FATAL_FAILURE(VerifyAllocAccess(mem, size));
    RandomSetMemory(mem, size);

    size_t new_size = size;
    while (new_size == size)
      new_size = base::RandInt(size / 2, size * 2);

    unsigned char sha1_before[base::kSHA1Length] = {};
    base::SHA1HashBytes(reinterpret_cast<unsigned char*>(mem),
                        std::min(size, new_size),
                        sha1_before);

    void* new_mem = proxy_.ReAlloc(0, mem, size * 2);
    ASSERT_TRUE(new_mem != NULL);
    ASSERT_NE(mem, new_mem);

    unsigned char sha1_after[base::kSHA1Length] = {};
    base::SHA1HashBytes(reinterpret_cast<unsigned char*>(new_mem),
                        std::min(size, new_size),
                        sha1_after);
    ASSERT_EQ(0, memcmp(sha1_before, sha1_after, base::kSHA1Length));

    ASSERT_NO_FATAL_FAILURE(VerifyFreedAccess(mem, size));
    ASSERT_NO_FATAL_FAILURE(VerifyAllocAccess(new_mem, size * 2));

    ASSERT_TRUE(proxy_.Free(0, new_mem));
    ASSERT_NO_FATAL_FAILURE(VerifyFreedAccess(new_mem, size * 2));
  }
}

TEST_F(HeapTest, Size) {
  for (size_t size = 10; size < kMaxAllocSize; size = size * 5 + 123) {
    void* mem = proxy_.Alloc(0, size);
    ASSERT_FALSE(mem == NULL);
    ASSERT_EQ(size, proxy_.Size(0, mem));
    ASSERT_TRUE(proxy_.Free(0, mem));
  }
}

TEST_F(HeapTest, Validate) {
  for (size_t size = 10; size < kMaxAllocSize; size = size * 5 + 123) {
    void* mem = proxy_.Alloc(0, size);
    ASSERT_FALSE(mem == NULL);
    ASSERT_TRUE(proxy_.Validate(0, mem));
    ASSERT_TRUE(proxy_.Free(0, mem));
  }
}

TEST_F(HeapTest, Compact) {
  // Compact should return a non-zero size.
  ASSERT_LT(0U, proxy_.Compact(0));

  // TODO(siggi): It may not be possible to allocate the size returned due
  //     to padding - fix and test.
}

TEST_F(HeapTest, LockUnlock) {
  // We can't really test these, aside from not crashing.
  ASSERT_TRUE(proxy_.Lock());
  ASSERT_TRUE(proxy_.Unlock());
}

TEST_F(HeapTest, Walk) {
  // We assume at least two entries to walk through.
  PROCESS_HEAP_ENTRY entry = {};
  ASSERT_TRUE(proxy_.Walk(&entry));
  ASSERT_TRUE(proxy_.Walk(&entry));
}

TEST_F(HeapTest, SetQueryInformation) {
  ULONG compat_flag = -1;
  unsigned long ret = 0;
  // Get the current value of the compat flag.
  ASSERT_TRUE(
      proxy_.QueryInformation(HeapCompatibilityInformation,
                              &compat_flag, sizeof(compat_flag), &ret));
  ASSERT_EQ(sizeof(compat_flag), ret);
  ASSERT_TRUE(compat_flag != -1);

  // Put the heap in LFH, which should always succeed, except when a debugger
  // is attached. When a debugger is attached, the heap is wedged in certain
  // debug settings.
  if (base::debug::BeingDebugged()) {
    LOG(WARNING) << "Can't test HeapProxy::SetInformation under debugger.";
    return;
  }

  compat_flag = 2;
  ASSERT_TRUE(
      proxy_.SetInformation(HeapCompatibilityInformation,
                            &compat_flag, sizeof(compat_flag)));
}

TEST_F(HeapTest, FindAddressBlock) {
  const size_t kAllocSize = 100;
  void* mem = proxy_.Alloc(0, kAllocSize);
  ASSERT_FALSE(mem == NULL);
  ASSERT_FALSE(proxy_.FindAddressBlock(static_cast<const uint8*>(mem)) == NULL);
  uint8* out_of_bounds_address =
      static_cast<uint8*>(mem) + kAllocSize * 2;
  ASSERT_TRUE(proxy_.FindAddressBlock(out_of_bounds_address) == NULL);
  ASSERT_TRUE(proxy_.Free(0, mem));
}

TEST_F(HeapTest, GetBadAccessKind) {
  const size_t kAllocSize = 100;
  // Ensure that the quarantine is large enough to keep this block, this is
  // needed for the use-after-free check.
  proxy_.set_quarantine_max_size(TestHeapProxy::GetAllocSize(kAllocSize));
  uint8* mem = static_cast<uint8*>(proxy_.Alloc(0, kAllocSize));
  ASSERT_FALSE(mem == NULL);
  TestHeapProxy::BlockHeader* header =
      const_cast<TestHeapProxy::BlockHeader*>(proxy_.ToBlock(mem));
  uint8* heap_underflow_address = mem - 1;
  uint8* heap_overflow_address = mem + kAllocSize * sizeof(uint8);
  ASSERT_TRUE(proxy_.IsUnderflowAccess(heap_underflow_address, header));
  ASSERT_TRUE(proxy_.IsOverflowAccess(heap_overflow_address, header));
  ASSERT_TRUE(proxy_.Free(0, mem));
  ASSERT_TRUE(proxy_.IsUseAfterAccess(mem, header));
}

}  // namespace asan
}  // namespace agent
