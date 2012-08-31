// Copyright 2012 Google Inc.
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
#ifndef SYZYGY_GRINDER_PROFILE_GRINDER_H_
#define SYZYGY_GRINDER_PROFILE_GRINDER_H_

#include <dia2.h>
#include <iostream>
#include <map>

#include "base/file_path.h"
#include "base/win/scoped_comptr.h"
#include "syzygy/trace/parse/parser.h"

namespace grinder {

// Fwd.
struct InvocationEdge;
struct InvocationNode;
struct Metrics;
struct ModuleRVA;
typedef uint32 RVA;

// A worker class to sink profile trace events and output the aggregate data in
// KCacheGrind-compatible format.
//
// The profiler instrumentation captures the wall-clock time from entry to exit
// for each pair of caller/function for each invocation. This is termed
// "inclusive" time, as it includes the time spent in other functions called.
//
// The KCacheGrind file format also requires listing "exclusive" time for each
// function, where exclusive time is the amount of time spent executing the
// function itself, e.g. exclusive of the time spent calling other functions.
//
// The profile data is captured in a trace log. The trace log is a run of
// records where each record in the log is associated with a particular thread
// (and process), and contains a set of invocation records.
// Each invocation record contains inclusive wall-clock time (and potentially
// other inclusive metrics) for one or more invocations from a particular
// caller address, to a particular function.
// Note that the same caller/function pair may occur multiple times in a trace
// log, even for the same thread, as the profile instrumentation spills a trace
// record to the log when the number of caller/function pairs it's seen exeeds
// the size of the thread-local buffer used to aggregate the data.
//
// This class aggregates the data from a trace log, and builds a graph of
// function nodes and call edges. For each call edge, it aggregates the data
// from one or more log records, by summing up the call counts and inclusive
// metrics. For each function node, it also computes the exclusive cost, by
// summing up the cost of the incoming edges, and subtracting the cost of the
// outgoing edges.
//
// For information on the KCacheGrind file format, see:
// http://kcachegrind.sourceforge.net/cgi-bin/show.cgi/KcacheGrindCalltreeFormat
class ProfileGrinder : public trace::parser::ParseEventHandlerImpl {
 public:
  typedef trace::parser::Parser Parser;

  ProfileGrinder();
  ~ProfileGrinder();

  // @name Accessors.
  // @{

  // If thread_parts is true, the grinder will aggregate and output
  // separate parts for each thread seen in the trace file(s).
  bool thread_parts() const { return thread_parts_; }
  void set_thread_parts(bool thread_parts) { thread_parts_ = thread_parts; }

  const Parser* parser() const { return parser_; }
  void set_parser(Parser* parser) { parser_ = parser; }
  // @}

  // Invoke after processing all traces.
  // This functions adds all caller edges to each function node's linked list of
  // callers. In so doing, it also computes each function node's inclusive cost.
  // @returns true on success, false on failure.
  bool ResolveCallers();

  // Invoke after ResolveCallers.
  // Outputs the aggregated data to @p file.
  // @returns true on success, false on failure.
  bool OutputData(FILE* file);

  // @name ParseEventHandler overrides.
  // @{
  virtual void OnInvocationBatch(
      base::Time time,
      DWORD process_id,
      DWORD thread_id,
      size_t num_invocations,
      const TraceBatchInvocationInfo* data) OVERRIDE;
  virtual void OnThreadName(base::Time time,
                            DWORD process_id,
                            DWORD thread_id,
                            const base::StringPiece& thread_name) OVERRIDE;
  // @}
 private:
  typedef sym_util::ModuleInformation ModuleInformation;

  // Forward declarations.
  struct PartData;
  struct ModuleRVA;
  struct Metrics;
  struct InvocationNode;
  struct InvocationEdge;

  typedef std::set<ModuleInformation,
      bool (*)(const ModuleInformation& a, const ModuleInformation& b)>
          ModuleInformationSet;
  typedef std::map<ModuleRVA, InvocationNode> InvocationNodeMap;
  typedef std::pair<ModuleRVA, ModuleRVA> InvocationEdgeKey;
  typedef std::map<InvocationEdgeKey, InvocationEdge> InvocationEdgeMap;

  typedef base::win::ScopedComPtr<IDiaSession> SessionPtr;
  typedef std::map<const ModuleInformation*, SessionPtr> ModuleSessionMap;

  bool GetSessionForModule(const ModuleInformation* module,
                           IDiaSession** session_out);

  // Finds or creates the part data for the given @p thread_id.
  PartData* FindOrCreatePart(DWORD process_id, DWORD thread_id);

  // Retrieves the function containing @p address.
  // @param symbol on success returns the function's private symbol, or
  //     public symbol if no private symbol is available.
  // @returns true on success.
  bool GetFunctionByRVA(IDiaSession* session,
                        RVA address,
                        IDiaSymbol** symbol);
  bool GetInfoForCallerRVA(const ModuleRVA& caller,
                           RVA* function_rva,
                           size_t* line);

  bool GetInfoForFunctionRVA(const ModuleRVA& function,
                             std::wstring* function_name,
                             std::wstring* file_name,
                             size_t* line);

  // Converts an absolute address to an RVA.
  void ConvertToModuleRVA(uint32 process_id,
                          trace::parser::AbsoluteAddress64 addr,
                          ModuleRVA* rva);

  // Aggregates a single invocation info and/or creates a new node and edge.
  void AggregateEntryToPart(const ModuleRVA& function_rva,
                            const ModuleRVA& caller_rva,
                            const InvocationInfo& info,
                            PartData* part);

  // Resolves callers for @p part.
  bool ResolveCallersForPart(PartData* part);

  // Outputs data for @p part to @p file.
  bool OutputDataForPart(const PartData& part, FILE* file);

  // Stores the modules we encounter.
  ModuleInformationSet modules_;

  // Stores the DIA session objects we have going for each module.
  ModuleSessionMap module_sessions_;

  // The parts we store. If thread_parts_ is false, we store only a single
  // part with id 0.
  typedef std::map<uint32, PartData> PartDataMap;
  PartDataMap parts_;

  // If true, data is aggregated and output per-thread.
  bool thread_parts_;

  Parser* parser_;
};

// The data we store for each part.
struct ProfileGrinder::PartData {
  PartData();

  // The thread name for this part.
  std::string thread_name_;

  // The process ID for this part.
  uint32 process_id_;

  // The thread ID for this part.
  uint32 thread_id_;

  // Stores the invocation nodes, aka the functions.
  InvocationNodeMap nodes_;

  // Stores the invocation edges.
  InvocationEdgeMap edges_;
};

// RVA in a module. The module should be a canonical pointer
// to the module information to make this comparable against
// other RVAs in the same module.
struct ProfileGrinder::ModuleRVA {
  ModuleRVA() : module(NULL), rva(0) {
  }

  bool operator < (const ModuleRVA& o) const {
    if (module > o.module)
      return false;
    if (module < o.module)
      return true;
    return rva < o.rva;
  }
  bool operator > (const ModuleRVA& o) const {
    return o < *this;
  }
  bool operator == (const ModuleRVA& o) const {
    return !(o < *this || *this < o);
  }
  bool operator != (const ModuleRVA& o) const {
    return !(*this == o);
  }

  const sym_util::ModuleInformation* module;
  RVA rva;
};

// The metrics we capture per function and per caller.
struct ProfileGrinder::Metrics {
  Metrics() : num_calls(0), cycles_min(0), cycles_max(0), cycles_sum(0) {
  }

  uint64 num_calls;
  uint64 cycles_min;
  uint64 cycles_max;
  uint64 cycles_sum;
};

// An invocation node represents a function.
struct ProfileGrinder::InvocationNode {
  InvocationNode() : first_call(NULL) {
  }

  // RVA for the function this instance represents.
  ModuleRVA function;

  // The metrics we've aggregated for this function.
  Metrics metrics;

  // Linked list of all the edges where the caller resolves to us.
  InvocationEdge* first_call;
};

// An invocation edge represents a caller->function pair.
struct ProfileGrinder::InvocationEdge {
  InvocationEdge() : caller_function(NULL), line(0), next_call(NULL) {
  }

  // The function/caller pair we denote.
  ModuleRVA function;
  ModuleRVA caller;

  // Line number of the caller.
  size_t line;
  Metrics metrics;

  // The calling function - resolved from caller.
  InvocationNode* caller_function;
  // Chains to the next edge resolving to the
  // same calling function.
  InvocationEdge* next_call;
};

}  // namespace grinder

#endif  // SYZYGY_GRINDER_PROFILE_GRINDER_H_
