// Copyright 2011 Google Inc.
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
// Definitions for the memory allocation hooks required by the RPC runtime.

#include <stdlib.h>

#include "syzygy/call_trace/call_trace_rpc.h"


// Memory allocator used by the RPC runtime.
void* __RPC_USER midl_user_allocate(size_t len) {
  return ::malloc(len);
}

// Memory de-allocator used by the RPC runtime.
void __RPC_USER midl_user_free(void* ptr) {
  ::free(ptr);
}
