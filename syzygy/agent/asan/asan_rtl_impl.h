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
// Implement the ASAN RTL functions.
#ifndef SYZYGY_AGENT_ASAN_ASAN_RTL_IMPL_H_
#define SYZYGY_AGENT_ASAN_ASAN_RTL_IMPL_H_

#include <windows.h>

// Exposes the Asan Rtl functions.
extern "C" {

HANDLE WINAPI asan_HeapCreate(DWORD options,
                              SIZE_T initial_size,
                              SIZE_T maximum_size);

BOOL WINAPI asan_HeapDestroy(HANDLE heap);

LPVOID WINAPI asan_HeapAlloc(HANDLE heap,
                             DWORD flags,
                             SIZE_T bytes);

LPVOID WINAPI asan_HeapReAlloc(HANDLE heap,
                               DWORD flags,
                               LPVOID mem,
                               SIZE_T bytes);

BOOL WINAPI asan_HeapFree(HANDLE heap,
                          DWORD flags,
                          LPVOID mem);

SIZE_T WINAPI asan_HeapSize(HANDLE heap,
                            DWORD flags,
                            LPCVOID mem);

BOOL WINAPI asan_HeapValidate(HANDLE heap,
                              DWORD flags,
                              LPCVOID mem);

SIZE_T WINAPI asan_HeapCompact(HANDLE heap,
                               DWORD flags);

BOOL WINAPI asan_HeapLock(HANDLE heap);

BOOL WINAPI asan_HeapUnlock(HANDLE heap);

BOOL WINAPI asan_HeapWalk(HANDLE heap,
                          LPPROCESS_HEAP_ENTRY entry);

BOOL WINAPI asan_HeapSetInformation(
    HANDLE heap, HEAP_INFORMATION_CLASS info_class,
    PVOID info, SIZE_T info_length);

BOOL WINAPI asan_HeapQueryInformation(
    HANDLE heap, HEAP_INFORMATION_CLASS info_class,
    PVOID info, SIZE_T info_length, PSIZE_T return_length);

void WINAPI asan_SetCallBack(void (*callback)(CONTEXT*));

}  // extern "C"

namespace agent {
namespace asan {

class AsanRuntime;

// Initialize the Asan runtime library global variables.
// @param runtime The Asan runtime manager.
void SetUpRtl(AsanRuntime* runtime);

// Tear down the runtime library.
void TearDownRtl();

}  // namespace asan
}  // namespace agent

#endif  // SYZYGY_AGENT_ASAN_ASAN_RTL_IMPL_H_
