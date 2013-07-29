// Copyright 2013 Google Inc. All Rights Reserved.
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
// This file declares some functions used to do the integration tests of the
// Asan interceptor functions.
#ifndef SYZYGY_INTEGRATION_TESTS_ASAN_INTERCEPTORS_TESTS_H_
#define SYZYGY_INTEGRATION_TESTS_ASAN_INTERCEPTORS_TESTS_H_

#include <windows.h>  // NOLINT

namespace testing {

// Disable the intrinsic version of the intercepted function.
#pragma function(memset, memcpy, strlen)

template<typename type>
static type AsanMemsetOverflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xFF, kArraySize * sizeof(type) + 1);
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemsetUnderflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(reinterpret_cast<uint8*>(ptr) - 1, 0xFF, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemsetUseAfterFree() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  type result = ptr[0];
  delete[] ptr;
  memset(reinterpret_cast<uint8*>(ptr), 0xFF, kArraySize * sizeof(type));
  return result;
}

template<typename type>
static type AsanMemchrOverflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  memchr(ptr, 0xFF, kArraySize * sizeof(type) + 1);
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemchrUnderflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  memchr(reinterpret_cast<uint8*>(ptr) - 1, 0xFF, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemchrUseAfterFree() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  memchr(ptr, 0xFF, kArraySize * sizeof(type));
  return result;
}

template<typename type>
static type AsanMemmoveWriteOverflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  memmove(reinterpret_cast<uint8*>(ptr) + 1, ptr, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemmoveWriteUnderflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  memmove(reinterpret_cast<uint8*>(ptr) - 1, ptr, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemmoveReadOverflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  memmove(ptr, reinterpret_cast<uint8*>(ptr) + 1, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemmoveReadUnderflow() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  memmove(ptr, reinterpret_cast<uint8*>(ptr) - 1, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  return result;
}

template<typename type>
static type AsanMemmoveUseAfterFree() {
  const size_t kArraySize = 10;
  type* ptr = new type[kArraySize];
  memset(ptr, 0xAA, kArraySize * sizeof(type));
  type result = ptr[0];
  delete[] ptr;
  memmove(ptr, ptr, kArraySize * sizeof(type));
  return result;
}

template<typename type>
static type AsanMemcpyWriteOverflow() {
  const size_t kArraySize = 10;
  type* src = new type[kArraySize];
  type* dst = new type[kArraySize];
  memset(src, 0xAA, kArraySize * sizeof(type));
  memcpy(reinterpret_cast<uint8*>(dst) + 1, src, kArraySize * sizeof(type));
  type result = src[0];
  delete[] src;
  delete[] dst;
  return result;
}

template<typename type>
static type AsanMemcpyWriteUnderflow() {
  const size_t kArraySize = 10;
  type* src = new type[kArraySize];
  type* dst = new type[kArraySize];
  memset(src, 0xAA, kArraySize * sizeof(type));
  memcpy(reinterpret_cast<uint8*>(dst) - 1, src, kArraySize * sizeof(type));
  type result = src[0];
  delete[] src;
  delete[] dst;
  return result;
}

template<typename type>
static type AsanMemcpyReadOverflow() {
  const size_t kArraySize = 10;
  type* src = new type[kArraySize];
  type* dst = new type[kArraySize];
  memset(src, 0xAA, kArraySize * sizeof(type));
  memcpy(dst, reinterpret_cast<uint8*>(src) + 1, kArraySize * sizeof(type));
  type result = src[0];
  delete[] src;
  delete[] dst;
  return result;
}

template<typename type>
static type AsanMemcpyReadUnderflow() {
  const size_t kArraySize = 10;
  type* src = new type[kArraySize];
  type* dst = new type[kArraySize];
  memset(src, 0xAA, kArraySize * sizeof(type));
  memcpy(dst, reinterpret_cast<uint8*>(src) - 1, kArraySize * sizeof(type));
  type result = src[0];
  delete[] src;
  delete[] dst;
  return result;
}

template<typename type>
static type AsanMemcpyUseAfterFree() {
  const size_t kArraySize = 10;
  type* src = new type[kArraySize];
  type* dst = new type[kArraySize];
  memset(src, 0xAA, kArraySize * sizeof(type));
  type result = src[0];
  delete[] src;
  memcpy(dst, src, kArraySize * sizeof(type));
  delete[] dst;
  return result;
}

size_t AsanStrcspnKeysOverflow();

size_t AsanStrcspnKeysUnderflow();

size_t AsanStrcspnSrcOverflow();

size_t AsanStrcspnSrcUnderflow();

size_t AsanStrcspnUseAfterFree();

size_t AsanStrlenOverflow();

size_t AsanStrlenUnderflow();

size_t AsanStrlenUseAfterFree();

size_t AsanStrrchrOverflow();

size_t AsanStrrchrUnderflow();

size_t AsanStrrchrUseAfterFree();

}  // namespace testing

#endif  // SYZYGY_INTEGRATION_TESTS_ASAN_INTERCEPTORS_TESTS_H_
