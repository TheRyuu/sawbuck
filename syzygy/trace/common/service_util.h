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
// Helper functions for Service implementations.

#ifndef SYZYGY_TRACE_COMMON_SERVICE_UTIL_H_
#define SYZYGY_TRACE_COMMON_SERVICE_UTIL_H_

#include "base/string_piece.h"
#include "base/win/scoped_handle.h"

namespace trace {
namespace common {

// Helper function to acquire a named mutex. Once acquired this mutex must be
// freed using ::ReleaseMutex.
// @param mutex_name The name of the mutex to acquire.
// @param mutex will receive a handle to the named mutex.
// @returns true on success, false otherwise. Logs verbosely on failure.
bool AcquireMutex(const base::StringPiece16& mutex_name,
                  base::win::ScopedHandle* mutex);

// Helper function to initialize a named event. The event will automatically be
// destroyed when the last handle to it disappears.
// @param event_name The name of event to create.
// @param hanel Will receive a handle to the named event.
// @returns true on success, false otherwise. Logs verbosely on failure.
bool InitEvent(const base::StringPiece16& event_name,
               base::win::ScopedHandle* handle);

// A helper class to manage a console handler for Control-C.
class ScopedConsoleCtrlHandler {
 public:
  ScopedConsoleCtrlHandler();
  ~ScopedConsoleCtrlHandler();
  bool Init(PHANDLER_ROUTINE handler);

 protected:
  PHANDLER_ROUTINE handler_;
};

}  // namespace common
}  // namespace trace

#endif  // SYZYGY_TRACE_COMMON_SERVICE_UTIL_H_
