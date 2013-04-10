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
// Declares the data structure that will be used by various pieces of the
// instrumentation and trace agent's to collect data at runtime.

#ifndef SYZYGY_COMMON_INDEXED_FREQUENCY_DATA_H_
#define SYZYGY_COMMON_INDEXED_FREQUENCY_DATA_H_

#include <windows.h>

#include "base/basictypes.h"
#include "syzygy/common/assertions.h"

namespace common {

#pragma pack(push, 1)

// This data structure is injected into an instrumented image in a read-write
// section of its own. It will be initialized by the runtime client library
// and is referred to by all of the instrumentation code.
struct IndexedFrequencyData {
  // An identifier denoting the agent with which this frequency data
  // instrumentation is intended to work.
  uint32 agent_id;

  // The version of the data structure and agent of the toolchain that
  // instrumented the binary. If this doesn't match the running client
  // library then the whole process should be aborted. This just a simple
  // counter which should be updated whenever a non-backwards compatible
  // change is made to the data structure or its usage.
  uint32 version;

  // The TLS slot associated with this module (if any). This allows for the
  // frequency trace data to be managed on a per-thread basis, if desired by the
  // agent.
  DWORD tls_index;

  // This points to an array of length 'num_entries' counter elements. At
  // link time it is initialized to point to statically allocated array that is
  // in the .data section of the image (this is done so that if capture is not
  // enabled the binary can still run without crashing). If a single process-
  // wide frequency table is needed, the agent may allocate a call-trace buffer
  // and redirect this pointer to point into it. Alternatively, it may allocate
  // any thread-specific context it requires and refer to this pointer as a
  // fall-back measure if tracing is disabled.
  //
  // The total size (in bytes) of the buffer pointed to by is
  // num_entries * frequency_size.
  void* frequency_data;

  // The number of entries in the frequency table. This is required by the
  // runtime client library so it knows how big an array to allocate.
  uint32 num_entries;

  // The number of bytes used for each element of frequency_data: 1, 4, or 8.
  uint8 frequency_size;

  // Each module only needs to be registered once with the call-trace service.
  // Our hooks grab various entry points (e.g. TLS initializers and the image
  // entry points), so the initialization routine may be called repeatedly. We
  // use this to determine whether or not we should try initializing things.
  // Upon first entry this is protected by the loader lock and afterwards it
  // is only read, so synchronization is not an issue.
  uint8 initialization_attempted;
};
COMPILE_ASSERT_IS_POD(IndexedFrequencyData);

#pragma pack(pop)

// The basic-block coverage agent ID.
extern const uint32 kBasicBlockCoverageAgentId;

// The basic-block entry counting agent ID.
extern const uint32 kBasicBlockEntryAgentId;

// The jump table counting agent ID.
extern const uint32 kJumpTableCountAgentId;

// The basic-block trace agent version.
extern const uint32 kBasicBlockFrequencyDataVersion;

// The jump table trace agent version.
extern const uint32 kJumpTableFrequencyDataVersion;

// The name of the basic-block ranges stream added to the PDB by
// any instrumentation employing basic-block trace data.
extern const char kBasicBlockRangesStreamName[];

}  // namespace common

#endif  // SYZYGY_COMMON_INDEXED_FREQUENCY_DATA_H_
