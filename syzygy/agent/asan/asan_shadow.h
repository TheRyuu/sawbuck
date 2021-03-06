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
//
// Implements an all-static class that manages shadow memory for ASAN.
#ifndef SYZYGY_AGENT_ASAN_ASAN_SHADOW_H_
#define SYZYGY_AGENT_ASAN_ASAN_SHADOW_H_

#include <string>

#include "base/basictypes.h"

namespace agent {
namespace asan {

// An all-static class that manages the ASAN shadow memory.
class Shadow {
 public:
   // The granularity of the shadow memory.
   static const size_t kShadowGranularityLog = 3;
   static const size_t kShadowGranularity = 1 << kShadowGranularityLog;

  // Set up the shadow memory.
  static void SetUp();

  // Tear down the shadow memory.
  static void TearDown();

  // The different markers we use to mark the shadow memory.
  enum ShadowMarker {
    kHeapAddressableByte = 0x00,
    kHeapNonAccessibleByteMask = 0xf0,
    kAsanMemoryByte = 0xf1,
    kInvalidAddress = 0xf2,
    kHeapLeftRedzone = 0xfa,
    kHeapRightRedzone = 0xfb,
    kHeapFreedByte = 0xfd,
  };

  // Poisons @p size bytes starting at @p addr with @p shadow_val value.
  // @pre addr + size mod 8 == 0.
  // @param address The starting address.
  // @param size The size of the memory to poison.
  // @param shadow_val The poison marker value.
  static void Poison(const void* addr, size_t size, ShadowMarker shadow_val);

  // Un-poisons @p size bytes starting at @p addr.
  // @pre addr mod 8 == 0 && size mod 8 == 0.
  // @param addr The starting address.
  // @param size The size of the memory to unpoison.
  static void Unpoison(const void* addr, size_t size);

  // Mark @p size bytes starting at @p addr as freed.
  // @param addr The starting address.
  // @param size The size of the memory to mark as freed.
  static void MarkAsFreed(const void* addr, size_t size);

  // Returns true iff the byte at @p addr is not poisoned.
  // @param addr The address that we want to check.
  // @returns true if this address is accessible, false otherwise.
  static bool IsAccessible(const void* addr);

  // Returns the ShadowMarker value for the byte at @p addr.
  // @param addr The address for which we want the ShadowMarker value.
  // @returns the ShadowMarker value for this address.
  static ShadowMarker GetShadowMarkerForAddress(const void* addr);

  // Appends a textual description of the shadow memory for @p addr to
  // @p output.
  // @param addr The address for which we want to get the textual description.
  // @param output The string in which we want to store this information.
  static void AppendShadowMemoryText(const void* addr, std::string* output);

  // Returns true iff the array starting at @p addr is null terminated within a
  // contiguous accessible region of memory. When returning true the length of
  // the null-terminated array (including the trailing zero) will be returned
  // via @p size. When returning false the offset of the invalid access will be
  // returned via @p size.
  // @param addr The starting address of the array that we want to check.
  // @param size Will receive the size of the null terminated array or the
  //     offset of the invalid access.
  // @param max_size The maximum length to check. Ignored if set to zero.
  // @return true iff the array starting at @p addr is null terminated within a
  //     contiguous accessible region of memory, false otherwise.
  static bool GetNullTerminatedArraySize(const void* addr,
                                         size_t* size,
                                         size_t max_size);

 protected:
  // Reset the shadow memory.
  static void Reset();

  // Appends a line of shadow byte text for the bytes ranging from
  // shadow_[index] to shadow_[index + 7], prefixed by @p prefix. If the index
  // @p bug_index is present in this range then its value will be surrounded by
  // brackets.
  static void AppendShadowByteText(const char *prefix,
                                   uintptr_t index,
                                   std::string* output,
                                   size_t bug_index);

  // One shadow byte for every 8 bytes in a 2G address space. By default Chrome
  // is not large address aware, so we shouldn't be using the high memory.
  static const size_t kShadowSize = 1 << (31 - kShadowGranularityLog);
  static uint8 shadow_[kShadowSize];
};

}  // namespace asan
}  // namespace agent

#endif  // SYZYGY_AGENT_ASAN_ASAN_SHADOW_H_
