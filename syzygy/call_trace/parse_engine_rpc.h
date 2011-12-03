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
// Call trace event parsing classes.

#ifndef SYZYGY_CALL_TRACE_PARSE_ENGINE_RPC_H_
#define SYZYGY_CALL_TRACE_PARSE_ENGINE_RPC_H_

#include "base/time.h"
#include "base/file_path.h"
#include "syzygy/call_trace/call_trace_defs.h"
#include "syzygy/call_trace/parse_engine.h"

namespace call_trace {
namespace parser {

class ParseEngineRpc : public ParseEngine {
 public:
  ParseEngineRpc();
  virtual ~ParseEngineRpc();

  // @name ParseEngine implementation
  // @{
  virtual bool IsRecognizedTraceFile(const FilePath& trace_file_path) OVERRIDE;
  virtual bool OpenTraceFile(const FilePath& trace_file_path) OVERRIDE;
  virtual bool ConsumeAllEvents() OVERRIDE;
  virtual bool CloseAllTraceFiles() OVERRIDE;
  // @}

 private:
  // A set of trace file paths.
  typedef std::vector<FilePath> TraceFileSet;

  // An iterator over a set of trace file paths.
  typedef TraceFileSet::iterator TraceFileIter;

  // Dispatches all of the events contained in the given trace file.
  //
  // For each segment in the trace file calls ConsumeSegmentEvents().
  //
  // @return true on success
  bool ConsumeTraceFile(const FilePath& trace_file_path);

  // Dispactches all of the events in the given segment buffer.
  //
  // @param file_header the header information describing the trace file.
  // @param segment_header the header iformation describign the segment.
  // @param buffer the full segment data buffer.
  // @param buffer_length the length of the segment data buffer (in bytes).
  // @return true on success.
  bool ConsumeSegmentEvents(const TraceFileHeader& file_header,
                            const TraceFileSegmentHeader& segment_header,
                            uint8* buffer,
                            size_t buffer_length);

  // The set of trace files to consume when ConsumeAllEvents() is called.
  TraceFileSet trace_file_set_;

  DISALLOW_COPY_AND_ASSIGN(ParseEngineRpc);
};

}  // namespace call_trace::parser
}  // namespace call_trace

#endif  // SYZYGY_CALL_TRACE_PARSE_ENGINE_RPC_H_
