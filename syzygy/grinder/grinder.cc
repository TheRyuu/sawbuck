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

#include "syzygy/grinder/grinder.h"

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/win/scoped_bstr.h"
#include "sawbuck/common/com_utils.h"
#include "syzygy/pe/find.h"

namespace grinder {

using base::win::ScopedBstr;
using base::win::ScopedComPtr;
using trace::parser::AbsoluteAddress64;
using trace::parser::ParseEventHandler;
using sym_util::ModuleInformation;

namespace {

// Compares invocation nodes on the key attribute of "function" only.
bool InvocationNodeKeyLess(const InvocationNode& a, const InvocationNode& b) {
  return a.function < b.function;
}

// Compares invocation edges on the key attributes of function and caller only.
bool InvocationEdgeKeyLess(const InvocationEdge& a, const InvocationEdge& b) {
  if (a.function < b.function)
    return true;
  if (a.function > b.function)
    return false;

  if (a.caller < b.caller)
    return true;

  return false;
}

// Compares module information without regard to base address.
// Used to canonicalize module information, even across processes, or multiple
// loads for the same module at different addresses in the same process.
bool ModuleInformationKeyLess(const ModuleInformation& a,
                              const ModuleInformation& b) {
  if (a.module_size > b.module_size)
    return false;
  if (a.module_size < b.module_size)
    return true;

  if (a.image_checksum > b.image_checksum)
    return false;
  if (a.image_checksum < b.image_checksum)
    return true;

  if (a.time_date_stamp > b.time_date_stamp)
    return false;
  if (a.time_date_stamp < b.time_date_stamp)
    return true;

  return a.image_file_name < b.image_file_name;
}

}  // namespace

Grinder::Grinder()
    : modules_(ModuleInformationKeyLess),
      nodes_(InvocationNodeKeyLess),
      edges_(InvocationEdgeKeyLess),
      parser_(NULL) {
}

Grinder::~Grinder() {
}

bool Grinder::GetSessionForModule(const ModuleInformation* module,
                                  IDiaSession** session_out) {
  DCHECK(module != NULL);
  DCHECK(session_out != NULL);
  DCHECK(*session_out == NULL);

  ModuleSessionMap::const_iterator it(
      module_sessions_.find(module));

  if (it == module_sessions_.end()) {
    ScopedComPtr<IDiaDataSource> source;
    HRESULT hr = source.CreateInstance(CLSID_DiaSource);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to create DiaSource: "
                 << com::LogHr(hr) << ".";
      return false;
    }

    pe::PEFile::Signature signature;
    signature.path = module->image_file_name;

    signature.base_address = core::AbsoluteAddress(
        static_cast<uint32>(module->base_address));
    signature.module_size = module->module_size;
    signature.module_time_date_stamp = module->time_date_stamp;
    signature.module_checksum = module->image_checksum;

    FilePath module_path;
    if (!pe::FindModuleBySignature(signature, &module_path))
      return false;

    ScopedComPtr<IDiaSession> new_session;
    hr = source->loadDataForExe(module_path.value().c_str(), NULL, NULL);
    if (SUCCEEDED(hr)) {
      hr = source->openSession(new_session.Receive());
      if (FAILED(hr))
        LOG(ERROR) << "Failure in openSession: " << com::LogHr(hr) << ".";

    } else {
      LOG(WARNING) << "Failure in loadDataForExe('"
                   << module_path.value().c_str() << "'): "
                   << com::LogHr(hr) << ".";
    }

    DCHECK((SUCCEEDED(hr) && new_session.get() != NULL) ||
           (FAILED(hr) && new_session.get() == NULL));

    // We store an entry to the cache irrespective of whether we succeeded
    // in opening a session above. This allows us to cache the failures, which
    // means we attempt to load each module only once, and consequently log
    // each failing module only once.
    it = module_sessions_.insert(
        std::make_pair(module, new_session)).first;
  }
  DCHECK(it != module_sessions_.end());

  if (it->second.get() == NULL) {
    // A negative session cache entry - we were previously unable to
    // load this module.
    return false;
  }

  *session_out = it->second;
  (*session_out)->AddRef();

  return true;
}

bool Grinder::GetInfoForCallerRVA(const ModuleRVA& caller,
                                  RVA* function_rva,
                                  size_t* line) {
  DCHECK(function_rva != NULL);
  DCHECK(line != NULL);

  ScopedComPtr<IDiaSession> session;
  if (!GetSessionForModule(caller.module, session.Receive()))
    return false;

  ScopedComPtr<IDiaSymbol> function;
  HRESULT hr = session->findSymbolByRVA(caller.rva,
                                        SymTagFunction,
                                        function.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failure in findSymbolByRVA: " << com::LogHr(hr);
    return false;
  }
  if (function.get() == NULL) {
    LOG(ERROR) << "NULL function returned from findSymbolByRVA.";
    return false;
  }

  // Get the RVA of the function.
  DWORD rva = 0;
  hr = function->get_relativeVirtualAddress(&rva);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failure in get_relativeVirtualAddress: "
               << com::LogHr(hr) << ".";
    return false;
  }
  *function_rva = rva;

  ULONGLONG length = 0;
  hr = function->get_length(&length);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failure in get_length: " << com::LogHr(hr) << ".";
    return false;
  }

  DWORD line_number = 0;
  if (length != 0) {
    ScopedComPtr<IDiaEnumLineNumbers> enum_lines;

    hr = session->findLinesByRVA(caller.rva,
                                 length,
                                 enum_lines.Receive());
    if (FAILED(hr)) {
      LOG(ERROR) << "Failure in findLinesByRVA: " << com::LogHr(hr) << ".";
      return false;
    }

    ScopedComPtr<IDiaLineNumber> line;
    ULONG fetched = 0;
    hr = enum_lines->Next(1, line.Receive(), &fetched);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failure in IDiaLineNumber::Next: "
                 << com::LogHr(hr) << ".";
      return false;
    }

    if (fetched == 1) {
      hr = line->get_lineNumber(&line_number);
      if (FAILED(hr)) {
        LOG(ERROR) << "Failure in get_lineNumber: " << com::LogHr(hr) << ".";
        return false;
      }
    } else if (fetched != 0) {
      NOTREACHED() << "IDiaLineNumber::Next unexpectedly returned "
                   << fetched << " elements.";
    }
  }

  *line = line_number;
  return true;
}

bool Grinder::GetInfoForFunctionRVA(const ModuleRVA& function,
                                    std::wstring* function_name,
                                    std::wstring* file_name,
                                    size_t* line) {
  DCHECK(function_name != NULL);
  DCHECK(file_name != NULL);
  DCHECK(line != NULL);

  ScopedComPtr<IDiaSession> session;
  if (!GetSessionForModule(function.module, session.Receive()))
    return false;

  ScopedComPtr<IDiaSymbol> function_sym;
  HRESULT hr = session->findSymbolByRVA(function.rva,
                                        SymTagFunction,
                                        function_sym.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failure in findSymbolByRVA: " << com::LogHr(hr) << ".";
    return false;
  }

  if (function_sym.get() == NULL) {
    LOG(ERROR) << "NULL function returned from findSymbolByRVA.";
    return false;
  }

  ScopedBstr function_name_bstr;
  hr = function_sym->get_name(function_name_bstr.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failure in get_name: " << com::LogHr(hr) << ".";
    return false;
  }

  *function_name = com::ToString(function_name_bstr);

  ULONGLONG length = 0;
  hr = function_sym->get_length(&length);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failure in get_length: " << com::LogHr(hr) << ".";
    return false;
  }

  ScopedBstr file_name_bstr;
  DWORD line_number = 0;
  if (length != 0) {
    ScopedComPtr<IDiaEnumLineNumbers> enum_lines;

    hr = session->findLinesByRVA(function.rva,
                                 length,
                                 enum_lines.Receive());
    if (FAILED(hr)) {
      LOG(ERROR) << "Failure in findLinesByRVA: " << com::LogHr(hr) << ".";
      return false;
    }

    ScopedComPtr<IDiaLineNumber> line;
    ULONG fetched = 0;
    hr = enum_lines->Next(1, line.Receive(), &fetched);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failure in IDialineNumber::Next: "
                 << com::LogHr(hr) << ".";
      return false;
    }
    if (fetched == 1) {
      hr = line->get_lineNumber(&line_number);
      if (FAILED(hr)) {
        LOG(ERROR) << "Failure in get_lineNumber: " << com::LogHr(hr) << ".";
        return false;
      }
      ScopedComPtr<IDiaSourceFile> source_file;
      hr = line->get_sourceFile(source_file.Receive());
      if (FAILED(hr)) {
        LOG(ERROR) << "Failure in get_sourceFile: " << com::LogHr(hr) << ".";
        return false;
      }
      hr = source_file->get_fileName(file_name_bstr.Receive());
      if (FAILED(hr)) {
        LOG(ERROR) << "Failure in get_fileName: " << com::LogHr(hr) << ".";
        return false;
      }
    }
  }

  *file_name = com::ToString(file_name_bstr);
  *line = line_number;
  return true;
}

bool Grinder::ResolveCallers() {
  // We start by iterating all the edges, connecting them up to their caller,
  // and subtracting the edge metric(s) to compute the inclusive metrics for
  // each function.
  InvocationEdgeSet::iterator edge_it(edges_.begin());
  for (; edge_it != edges_.end(); ++edge_it) {
    InvocationEdge& edge = *edge_it;
    RVA function_rva = 0;
    if (GetInfoForCallerRVA(edge.caller, &function_rva, &edge.line)) {
      InvocationNode node;
      node.function.module = edge.caller.module;
      node.function.rva = function_rva;
      InvocationNodeSet::iterator node_it(nodes_.find(node));
      if (node_it != nodes_.end()) {
        InvocationNode& node = *node_it;

        // Hook it up to the node's list of outgoing edges.
        edge.next_call = node.first_call;
        node.first_call = &edge;

        // Make the function's cycle count exclusive, by subtracting all
        // the outbound (inclusive) cycle counts from the total.
        node.metrics.cycles_sum -= edge.metrics.cycles_sum;
      } else {
        LOG(ERROR) << "Caller function not found.";
      }
    } else {
      // TODO(siggi): The profile instrumentation currently doesn't record
      //     sufficient module information that we can resolve calls from
      //     system and dependent modules.
      LOG(WARNING) << "Found no info for module: '"
                   << edge_it->caller.module->image_file_name << "'.";
    }
  }

  return true;
}

bool Grinder::OutputData(FILE* file) {
  // Output the file header.
  ::fprintf(file, "events: Calls Cycles Cycles-Min Cycles-Max.\n");

  // Now walk the nodes and output the data.
  InvocationNodeSet::const_iterator node_it(nodes_.begin());
  for (; node_it != nodes_.end(); ++node_it) {
    const InvocationNode& node = *node_it;
    std::wstring function_name;
    std::wstring file_name;
    size_t line = 0;
    if (GetInfoForFunctionRVA(node.function,
                              &function_name,
                              &file_name,
                              &line)) {
      // Output the function information.
      ::fprintf(file, "fl=%ws\n", file_name.c_str());
      ::fprintf(file, "fn=%ws\n", function_name.c_str());
      ::fprintf(file, "%d %I64d %I64d %I64d\n", line,
                node.metrics.num_calls, node.metrics.cycles_sum,
                node.metrics.cycles_min, node.metrics.cycles_max);

      // Output the call information from this function.
      const InvocationEdge* call = node.first_call;
      for (; call != NULL; call = call->next_call) {
        if (GetInfoForFunctionRVA(call->function,
                                  &function_name,
                                  &file_name,
                                  &line)) {
          ::fprintf(file, "cfl=%ws\n", file_name.c_str());
          ::fprintf(file, "cfn=%ws\n", function_name.c_str());
          ::fprintf(file, "calls=%d %d\n", call->metrics.num_calls, line);
          ::fprintf(file, "%d %I64d %I64d %I64d\n", call->line,
                    call->metrics.num_calls, call->metrics.cycles_sum,
                    call->metrics.cycles_min, call->metrics.cycles_max);
        }
      }
    } else {
      LOG(ERROR) << "Unable to resolve function.";
      return false;
    }
  }

  return true;
}

void Grinder::OnProcessStarted(base::Time time,
                               DWORD process_id,
                               const TraceSystemInfo* data) {
  // Ignored.
}

void Grinder::OnProcessEnded(base::Time time, DWORD process_id) {
  // Ignored.
}

void Grinder::OnFunctionEntry(base::Time time,
                              DWORD process_id,
                              DWORD thread_id,
                              const TraceEnterExitEventData* data) {
  // Ignored.
}

void Grinder::OnFunctionExit(base::Time time,
                             DWORD process_id,
                             DWORD thread_id,
                             const TraceEnterExitEventData* data) {
  // Ignored.
}

void Grinder::OnBatchFunctionEntry(base::Time time,
                                   DWORD process_id,
                                   DWORD thread_id,
                                   const TraceBatchEnterData* data) {
  // Ignored.
}

void Grinder::OnProcessAttach(base::Time time,
                              DWORD process_id,
                              DWORD thread_id,
                              const TraceModuleData* data) {
  // Ignored.
}

void Grinder::OnProcessDetach(base::Time time,
                              DWORD process_id,
                              DWORD thread_id,
                              const TraceModuleData* data) {
  // Ignored.
}

void Grinder::OnThreadAttach(base::Time time,
                             DWORD process_id,
                             DWORD thread_id,
                             const TraceModuleData* data) {
  // Ignored.
}

void Grinder::OnThreadDetach(base::Time time,
                             DWORD process_id,
                             DWORD thread_id,
                             const TraceModuleData* data) {
  // Ignored.
}

void Grinder::OnInvocationBatch(base::Time time,
                                DWORD process_id,
                                DWORD thread_id,
                                size_t num_invocations,
                                const TraceBatchInvocationInfo* data) {
  // Process and aggregate the individual invocation entries.
  for (size_t i = 0; i < num_invocations; ++i) {
    const InvocationInfo& info = data->invocations[i];
    AbsoluteAddress64 function =
        reinterpret_cast<AbsoluteAddress64>(info.function);

    ModuleRVA function_rva;
    ConvertToModuleRVA(process_id, function, &function_rva);

    // We should always have module information for functions.
    DCHECK(function_rva.module != NULL);

    AbsoluteAddress64 caller =
        reinterpret_cast<AbsoluteAddress64>(info.caller);
    ModuleRVA caller_rva;
    ConvertToModuleRVA(process_id, caller, &caller_rva);

    AggregateEntry(function_rva, caller_rva, info);
  }
}

void Grinder::AggregateEntry(const ModuleRVA& function_rva,
                             const ModuleRVA& caller_rva,
                             const InvocationInfo& info) {
  InvocationNode node;
  node.function = function_rva;

  // Have we recorded this node before?
  InvocationNodeSet::iterator node_it(nodes_.find(node));
  if (node_it != nodes_.end()) {
    // Yups, we've seen this edge before.
    // Aggregate the new data with the old.
    InvocationNode& found = *node_it;
    found.metrics.num_calls += info.num_calls;
    found.metrics.cycles_min = std::min(found.metrics.cycles_min,
                                        info.cycles_min);
    found.metrics.cycles_max = std::max(found.metrics.cycles_max,
                                        info.cycles_max);
    found.metrics.cycles_sum += info.cycles_sum;
  } else {
    // Nopes, we haven't seen this pair before, insert it.
    node.metrics.num_calls = info.num_calls;
    node.metrics.cycles_min = info.cycles_min;
    node.metrics.cycles_max = info.cycles_max;
    node.metrics.cycles_sum = info.cycles_sum;

    bool inserted = nodes_.insert(node).second;
    DCHECK(inserted);
  }

  // If the caller is NULL, we can't do anything with the edge as the
  // caller is unknown, so skip recording it. The data will be aggregated
  // to the edge above.
  if (caller_rva.module != NULL) {
    InvocationEdge edge;
    edge.function = function_rva;
    edge.caller = caller_rva;

    // Have we recorded this edge before?
    InvocationEdgeSet::iterator edge_it(edges_.find(edge));
    if (edge_it != edges_.end()) {
      // Yups, we've seen this edge before.
      // Aggregate the new data with the old.
      InvocationEdge& found = *edge_it;
      found.metrics.num_calls += info.num_calls;
      found.metrics.cycles_min = std::min(found.metrics.cycles_min,
                                          info.cycles_min);
      found.metrics.cycles_max = std::max(found.metrics.cycles_max,
                                          info.cycles_max);
      found.metrics.cycles_sum += info.cycles_sum;
    } else {
      // Nopes, we haven't seen this edge before, insert it.
      edge.metrics.num_calls = info.num_calls;
      edge.metrics.cycles_min = info.cycles_min;
      edge.metrics.cycles_max = info.cycles_max;
      edge.metrics.cycles_sum = info.cycles_sum;

      bool inserted = edges_.insert(edge).second;
      DCHECK(inserted);
    }
  }
}

void Grinder::ConvertToModuleRVA(uint32 process_id,
                                 AbsoluteAddress64 addr,
                                 ModuleRVA* rva) {
  DCHECK(rva != NULL);

  const ModuleInformation* module =
      parser_->GetModuleInformation(process_id, addr);

  if (module == NULL) {
    // We have no module information for this address.
    rva->module = NULL;
    rva->rva = 0;
    return;
  }

  // Convert the address to an RVA.
  rva->rva = static_cast<RVA>(addr - module->base_address);

  // And find or record the canonical module information
  // for this module.
  ModuleInformationSet::iterator it(modules_.find(*module));
  if (it == modules_.end()) {
    it = modules_.insert(*module).first;
  }
  DCHECK(it != modules_.end());

  rva->module = &(*it);
}

namespace {

const char kUsageFormatStr[] =
    "Usage: %ls <trace files> [options]\n"
    "\n"
    "  A tool that parses profile trace files and outputs KCacheGrind-\n"
    "  compatible output files for visualization.\n"
    "\n"
    "Optional parameters\n"
    "  --output-file=<output file>\n"
    "    The location of output file. If not specified, output is to stdout.\n";

}  // namespace

void GrinderApp::PrintUsage(const FilePath& program,
                            const base::StringPiece& message) {
  if (!message.empty()) {
    ::fwrite(message.data(), 1, message.length(), out());
    ::fprintf(out(), "\n\n");
  }

  ::fprintf(out(), kUsageFormatStr, program.BaseName().value().c_str());
}

bool GrinderApp::ParseCommandLine(const CommandLine* command_line) {
  DCHECK(command_line != NULL);

  CommandLine::StringVector args = command_line->GetArgs();
  if (args.empty()) {
    PrintUsage(command_line->GetProgram(),
               "You must provide at least one trace file");
    return false;
  }

  for (size_t i = 0; i < args.size(); ++i)
    trace_files_.push_back(FilePath(args[i]));

  output_file_ = command_line->GetSwitchValuePath("output-file");

  return true;
}

int GrinderApp::Run() {
  grinder::Grinder grinder;
  trace::parser::Parser parser;

  grinder.set_parser(&parser);
  if (!parser.Init(&grinder))
    return 1;

  for (size_t i = 0; i < trace_files_.size(); ++i) {
    if (!parser.OpenTraceFile(trace_files_[i])) {
      LOG(ERROR) << "Unable to open trace file \'"
                 << trace_files_[i].value() << "'";
      return 1;
    }
  }

  if (!parser.Consume()) {
    LOG(ERROR) << "Error parsing trace files.";
    return 1;
  }

  if (!grinder.ResolveCallers()) {
    LOG(ERROR) << "Error resolving callers.";
    return 1;
  }

  FILE* output = out();
  file_util::ScopedFILE auto_close;
  if (!output_file_.empty()) {
    output = file_util::OpenFile(output_file_, "w");
    if (output == NULL) {
      LOG(ERROR) << "Unable to create output file \'"
                 << output_file_.value() << "'";
      return 1;
    }

    auto_close.reset(output);
  }

  DCHECK(output != NULL);
  if (!grinder.OutputData(output)) {
    LOG(ERROR) << "Error writing output.";
    return 1;
  }

  return 0;
}

}  // namespace grinder
