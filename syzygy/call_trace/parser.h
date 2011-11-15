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

#ifndef SYZYGY_CALL_TRACE_PARSER_H_
#define SYZYGY_CALL_TRACE_PARSER_H_

#include <list>
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "base/file_path.h"
#include "sawbuck/sym_util/types.h"
#include "syzygy/core/address.h"
#include "syzygy/core/address_space.h"
#include "syzygy/call_trace/call_trace_defs.h"

namespace call_trace {
namespace parser {

typedef sym_util::ModuleInformation ModuleInformation;
typedef uint64 AbsoluteAddress64;
typedef uint64 Size64;
typedef core::AddressSpace<AbsoluteAddress64, Size64, ModuleInformation>
    ModuleSpace;

// Forward declarations.
class ParseEngine;
class ParseEventHandler;

// A facade class that manages the various call trace parser engines which
// Syzygy supports and presents a single interface that selects the most
// appropriate one based on the files being parsed.
class Parser {
 public:
  Parser();
  ~Parser();

  // Allows you to add a parse engine to the Parser facade. This should be
  // called prior to the call to Init. This is mainly intended for testing.
  // The Parser will assume responsibility for deleting the ParseEngine. The
  // parse engine is added to the front of the list, meaning it will be given
  // the chance to handle files before any of the built-in parsers.
  //
  // @param parse_engine pointer to a heap allocated ParseEngine.
  void AddParseEngine(ParseEngine* parse_engine);

  // Initialize the parser implementation.
  bool Init(ParseEventHandler* event_handler);

  // Returns true if an error occurred while parsing the trace files.
  bool error_occurred() const;

  // Set or reset the error flag.
  void set_error_occurred(bool value);

  // Add a trace file to the parse session. This can be called multiple times
  // with different trace file paths. The type of parser used is established
  // based on the type of the first trace file opened. It is an error to
  // open trace files of different type in a single parse session.
  bool OpenTraceFile(const FilePath& trace_file_path);

  // Consume all events across all currently open trace files.
  bool Consume();

  // Given an address and a process id, returns the module in memory at that
  // address. Returns NULL if no such module exists.
  const ModuleInformation* GetModuleInformation(uint32 process_id,
                                                AbsoluteAddress64 addr) const;

  // Cose all currently open trace files.
  bool Close();

 private:
  typedef std::list<ParseEngine*> ParseEngineSet;
  typedef ParseEngineSet::iterator ParseEngineIter;

  // Sets the currently active parse engine to the first engine that
  // recognizes the given trace file.
  bool SetActiveParseEngine(const FilePath& trace_file_path);

  // The set of parse engines available to consume and dispatch the events
  // contained in a set of trace files.
  ParseEngineSet parse_engine_set_;

  // The parse engine currently being used to parse trace files. This value
  // will be set based on the first trace file that gets opened.
  ParseEngine* active_parse_engine_;

  DISALLOW_COPY_AND_ASSIGN(Parser);
};

// Implemented by clients of Parser to receive trace event notifications.
class ParseEventHandler {
 public:
  // Issued for the first call-trace event occurring in an instrumented module.
  virtual void OnProcessStarted(base::Time time, DWORD process_id) = 0;

  // Issued following the last call-trace event for the process given by
  // @p process_id.
  virtual void OnProcessEnded(base::Time time, DWORD process_id) = 0;

  // Issued for non-batch function entry traces.
  virtual void OnFunctionEntry(base::Time time,
                               DWORD process_id,
                               DWORD thread_id,
                               const TraceEnterExitEventData* data) = 0;

  // Issued for function exit traces.
  virtual void OnFunctionExit(base::Time time,
                              DWORD process_id,
                              DWORD thread_id,
                              const TraceEnterExitEventData* data) = 0;

  // Issued for batch function entry traces.
  virtual void OnBatchFunctionEntry(base::Time time,
                                    DWORD process_id,
                                    DWORD thread_id,
                                    const TraceBatchEnterData* data) = 0;

  // Issued for DLL_PROCESS_ATTACH on an instrumented module.
  virtual void OnProcessAttach(base::Time time,
                               DWORD process_id,
                               DWORD thread_id,
                               const TraceModuleData* data) = 0;

  // Issued for DLL_PROCESS_DETACH on an instrumented module.
  virtual void OnProcessDetach(base::Time time,
                               DWORD process_id,
                               DWORD thread_id,
                               const TraceModuleData* data) = 0;

  // Issued for DLL_THREAD_ATTACH on an instrumented module.
  virtual void OnThreadAttach(base::Time time,
                              DWORD process_id,
                              DWORD thread_id,
                              const TraceModuleData* data) = 0;

  // Issued for DLL_THREAD_DETACH on an instrumented module.
  virtual void OnThreadDetach(base::Time time,
                              DWORD process_id,
                              DWORD thread_id,
                              const TraceModuleData* data) = 0;
};

}  // namespace call_trace::parser
}  // namespace call_trace

#endif  // SYZYGY_CALL_TRACE_PARSER_H_
