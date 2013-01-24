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
// Process-related convenience utilities for agents.

#ifndef SYZYGY_AGENT_COMMON_PROCESS_UTILS_H_
#define SYZYGY_AGENT_COMMON_PROCESS_UTILS_H_

#include <windows.h>
#include <vector>

// Forward declarations.
namespace trace {
namespace client {
class RpcSession;
class TraceFileSegment;
}  // namespace client
}  // namespace trace

namespace agent {
namespace common {

typedef std::vector<HMODULE> ModuleVector;

// Retrieves a list of all modules in the process.
// @param modules returns a vector containing all modules in the process.
// @note that other threads in the process can be loading or unloading
//     libraries concurrently with calling this function and using its results.
//     Using the results from this function is therefore inherently racy, unless
//     running under the loader's lock, such as e.g. in a DllMain notification
//     or e.g. a TLS callback function.
void GetProcessModules(ModuleVector* modules);

// Logs a TRACE_PROCESS_ATTACH_EVENT to the provided @p segment and @p session.
// If there is insufficient room in @p segment, returns the buffer to @p service
// and allocates a new one.
// @param module the module to be logged.
// @param session the session owning @p segment.
// @param segment the segment in which the event will be written.
// @returns true on success, false otherwise.
bool LogModule(HMODULE module,
               trace::client::RpcSession* session,
               trace::client::TraceFileSegment* segment);

}  // namespace common
}  // namespace agent

#endif  // SYZYGY_AGENT_COMMON_PROCESS_UTILS_H_
