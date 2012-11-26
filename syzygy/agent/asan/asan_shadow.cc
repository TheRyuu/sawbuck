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
#include "syzygy/agent/asan/asan_shadow.h"

#include "base/logging.h"

namespace agent {
namespace asan {

uint8 Shadow::shadow_[kShadowSize];

void Shadow::Poison(const void* addr, size_t size) {
  uintptr_t index = reinterpret_cast<uintptr_t>(addr);
  uintptr_t start = index & 0x7;
  DCHECK_EQ(0U, (index + size) & 0x7);

  index >>= 3;
  if (start)
    shadow_[index++] = start;

  size >>= 3;
  DCHECK_GT(arraysize(shadow_), index + size);
  memset(shadow_ + index, 0xFF, size);
}

void Shadow::Unpoison(const void* addr, size_t size) {
  uintptr_t index = reinterpret_cast<uintptr_t>(addr);
  DCHECK_EQ(0U, index & 0x7);

  uint8 remainder = size & 0x7;
  index >>= 3;
  size >>= 3;
  DCHECK_GT(arraysize(shadow_), index + size);
  memset(shadow_ + index, 0, size);

  if (remainder != 0)
    shadow_[index + size] = remainder;
}

bool __stdcall Shadow::IsAccessible(const void* addr) {
  uintptr_t index = reinterpret_cast<uintptr_t>(addr);
  uintptr_t start = index & 0x7;

  index >>= 3;

  DCHECK_GT(arraysize(shadow_), index);
  uint8 shadow = shadow_[index];
  if (shadow == 0)
    return true;

  if (shadow == 0xFF)
    return false;

  return start < shadow;
}

void Shadow::PrintShadowBytes(const char *prefix, uintptr_t index) {
  fprintf(stderr, "%s0x%08x:", prefix, reinterpret_cast<void*>(index << 3));
  for (uint32 i = 0; i < 8; i++) {
    uint8 shadow_value = shadow_[index + i];
    fprintf(stderr, " %x%x", shadow_value >> 4, shadow_value & 15);
  }
  fprintf(stderr, "\n");
}

void Shadow::PrintShadowMemoryForAddress(const void* addr) {
  uintptr_t index = reinterpret_cast<uintptr_t>(addr);
  index >>= 3;
  fprintf(stderr, "Shadow byte and word:\n");
  fprintf(stderr, "  0x%08x: %x\n", addr, shadow_[index]);
  index &= ~0x7;
  PrintShadowBytes("  ", index);
  fprintf(stderr, "More shadow bytes:\n");
  for (int i = -4; i <= 4; i++) {
    const char *prefix = (i == 0) ? "=>" : "  ";
    PrintShadowBytes(prefix, (index + i * 8));
  }
}

}  // namespace asan
}  // namespace agent
