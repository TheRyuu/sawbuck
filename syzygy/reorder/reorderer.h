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
// This defines the pure virtual Reorderer base class. This class abstracts
// away the ETW log parsing, decomposition, Block lookup, etc, that is a routine
// part of producing a new ordering. Derived classes are to implement actual
// order generation.
#ifndef SYZYGY_REORDER_REORDERER_H_
#define SYZYGY_REORDER_REORDERER_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/win/event_trace_consumer.h"
#include "sawbuck/log_lib/kernel_log_consumer.h"
#include "syzygy/call_trace/call_trace_parser.h"
#include "syzygy/pe/decomposer.h"

namespace reorder {

typedef uint64 AbsoluteAddress64;
typedef uint64 Size64;

using core::AddressSpace;
using core::BlockGraph;
using core::RelativeAddress;
using pe::Decomposer;
using pe::PEFile;

// Class encapsulating a DLL order generator. Is itself an ETW trace consumer,
// consuming Kernel and CallTrace events, maintaining all necessary state and
// correlating TRACE_ENTRY events to modules and blocks before handing them off
// to a delegate.
class Reorderer
    : public base::win::EtwTraceConsumerBase<Reorderer>,
      public KernelModuleEvents,
      public KernelProcessEvents,
      public CallTraceEvents {
 public:
  struct Order;
  class OrderGenerator;
  class UniqueTime;

  // A bit flag of directives that the derived reorderer should attempt
  // to satisfy.
  // TODO(chrisha): Basic block reordering.
  enum FlagsEnum {
    kFlagReorderCode = 1 << 0,
    kFlagReorderData = 1 << 1,
  };
  typedef uint32 Flags;

  // This class needs to be a singleton due to the Windows ETW API. The
  // constructor will enforce this in debug builds. The module_path may be
  // left blank, in which case it will be inferred from the instrumented
  // DLL metadata.
  Reorderer(const FilePath& module_path,
            const FilePath& instrumented_path,
            const std::vector<FilePath>& trace_paths,
            Flags flags);

  virtual ~Reorderer();

  // Runs the reorderer, parsing the ETW logs and generating an ordering.
  // Returns true on success, false otherwise. @p order must not be NULL.
  bool Reorder(OrderGenerator* order_generator, Order* order);

  // Returns the reorderer directives provided at Init time.
  Flags flags() const { return flags_; }

 private:
  // This allows our parent classes to access the necessary callbacks, but
  // hides them from derived classes.
  friend base::win::EtwTraceConsumerBase<Reorderer>;

  typedef AddressSpace<AbsoluteAddress64, Size64, ModuleInformation>
      ModuleSpace;
  typedef std::map<uint32, ModuleSpace> ProcessMap;
  typedef std::set<uint32> ProcessSet;
  typedef Decomposer::DecomposedImage DecomposedImage;
  typedef KernelProcessEvents::ProcessInfo ProcessInfo;

  // The actual implementation of Reorder.
  bool ReorderImpl(Order* order);
  // Parses the instrumented DLL headers, validating that it was produced
  // by a compatible version of the toolchain, and extracting signature
  // information and metadata. Returns true on success, false otherwise.
  bool ValidateInstrumentedModuleAndParseSignature(
      PEFile::Signature* orig_signature);
  // Returns true if the given ModuleInformation matches the instrumented
  // module signature, false otherwise.
  bool MatchesInstrumentedModuleSignature(
      const ModuleInformation& module_info) const;

  // KernelModuleEvents implementation.
  virtual void OnModuleIsLoaded(DWORD process_id,
                                const base::Time& time,
                                const ModuleInformation& module_info);
  virtual void OnModuleUnload(DWORD process_id,
                              const base::Time& time,
                              const ModuleInformation& module_info);
  virtual void OnModuleLoad(DWORD process_id,
                            const base::Time& time,
                            const ModuleInformation& module_info);

  // KernelProcessEvents implementation.
  virtual void OnProcessIsRunning(const base::Time& time,
                                  const ProcessInfo& process_info);
  virtual void OnProcessStarted(const base::Time& time,
                                const ProcessInfo& process_info);
  virtual void OnProcessEnded(const base::Time& time,
                              const ProcessInfo& process_info,
                              ULONG exit_status);

  // CallTraceEvents implementation.
  virtual void OnTraceEntry(base::Time time,
                            DWORD process_id,
                            DWORD thread_id,
                            const TraceEnterExitEventData* data);
  virtual void OnTraceExit(base::Time time,
                           DWORD process_id,
                           DWORD thread_id,
                           const TraceEnterExitEventData* data);
  virtual void OnTraceBatchEnter(base::Time time,
                                 DWORD process_id,
                                 DWORD thread_id,
                                 const TraceBatchEnterData* data);

  void OnEvent(PEVENT_TRACE event);
  static void ProcessEvent(PEVENT_TRACE event);
  static bool ProcessBuffer(PEVENT_TRACE_LOGFILE buffer);

  // Given an address and a process id, returns the module in memory at that
  // address. Returns NULL if no such module exists.
  const sym_util::ModuleInformation* GetModuleInformation(
      uint32 process_id, AbsoluteAddress64 addr) const;

  KernelLogParser kernel_log_parser_;
  CallTraceParser call_trace_parser_;

  FilePath module_path_;
  FilePath instrumented_path_;
  std::vector<FilePath> trace_paths_;

  // Signature of the instrumented DLL. Used for filtering call-trace events.
  PEFile::Signature instr_signature_;
  // A set of flags controlling the reorderer behaviour.
  Flags flags_;
  // Number of CodeBlockEntry events processed.
  size_t code_block_entry_events_;
  // Is the consumer errored?
  bool consumer_errored_;
  // The time of the last processed event.
  base::Time last_event_time_;
  // For each process, we store its point of view of the world.
  ProcessMap processes_;
  // The set of processes of interest. That is, those that have had code
  // run in the instrumented module. These are the only processes for which
  // we are interested in OnProcessEnded events.
  ProcessSet matching_process_ids_;

  // The following three variables are only valid while Reorder is executing.
  // A pointer to our order generator delegate.
  OrderGenerator* order_generator_;
  // A pointer to the PE file info for the module we're reordering. This
  // is actually a pointer to a part of the output structure, but several
  // internals make use of it during processing.
  PEFile* pe_;
  // The decomposed image of the module we're reordering. This is actually
  // a pointer to an image in the output structure, but several internals
  // make use of it during processing.
  DecomposedImage* image_;

  // A cache for whether or not to reorder each section.
  typedef std::vector<bool> SectionReorderabilityCache;
  SectionReorderabilityCache section_reorderability_cache_;

  // A pointer to the only instance of a reorderer.
  static Reorderer* consumer_;

  DISALLOW_COPY_AND_ASSIGN(Reorderer);
};

// Stores order information. An order may be serialized to and from JSON,
// in the following format:
//
// {
//   'metadata': {
//     this contains toolchain information, command-line info, etc
//   },
//   'sections': {
//     'section_id': <INTEGER SECTION ID>,
//     'blocks': [
//       list of integer block addresses
//     ]
//   ]
// }
struct Reorderer::Order {
  // Constructor just sets the image reference. Note that the image must
  // outlive the Order.
  Order(PEFile& p, DecomposedImage& i) : pe(p), image(i) {}

  // Stores the PE file info associated with the DLL to reorder.
  PEFile& pe;

  // Stores the decomposed image associated with the DLL to reorder.
  DecomposedImage& image;

  // A comment describing the ordering.
  std::string comment;

  // An ordering of blocks. This list need not be exhaustive, but each
  // block should only appear once within it. We currently constrain ourselves
  // to keep blocks in the same section from which they originate. Thus, we
  // separate the order information per section, with the section IDs coming
  // from the DecomposedImage of the original module.
  typedef std::vector<const BlockGraph::Block*> BlockList;
  typedef std::map<size_t, BlockList> BlockListMap;
  BlockListMap section_block_lists;

  // Serializes the order to JSON. Returns true on success, false otherwise.
  // The serialization simply consists of the start addresses of each block
  // in a JSON list. Pretty-printing adds further information from the
  // BlockGraph via inline comments.
  bool SerializeToJSON(const FilePath& path, bool pretty_print) const;
  bool SerializeToJSON(FILE* file, bool pretty_print) const;

  // Loads an ordering from a JSON file. 'pe' and 'image' must already be
  // populated prior to calling this.
  bool LoadFromJSON(const FilePath& path);

  // Extracts the name of the original module from an order file. This is
  // used to guess the value of --input-dll.
  static bool GetOriginalModulePath(const FilePath& path, FilePath* module);

  // Estimates the number of hard faults that would be seen, both before and
  // after ordering. This assumes that everything in |blocks| is actually
  // visited.
  bool OutputFaultEstimates(const FilePath& path) const;
  bool OutputFaultEstimates(FILE* file) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(Order);
};

// The actual class that does the work, an order generator. It receives
// call trace events (already mapped to blocks in a disassembled image),
// and is asked to build an ordering.
class Reorderer::OrderGenerator {
 public:
  explicit OrderGenerator(const char* name) : name_(name) {}

  virtual ~OrderGenerator() {}

  // Accessor.
  const std::string& name() const { return name_; }

  // The derived class may implement this callback, which indicates when a
  // process invoking the instrumented module was started.
  virtual bool OnProcessStarted(uint32 process_id,
                                const UniqueTime& time) { return true; }

  // The derived class may implement this callback, which provides
  // information on the end of processes invoking the instrumented module.
  // Processes whose lifespan exceed the logging period will not receive
  // OnProcessEnded events.
  virtual bool OnProcessEnded(uint32 process_id,
                              const UniqueTime& time) { return true; }

  // The derived class shall implement this callback, which receives
  // TRACE_ENTRY events for the module that is being reordered. Returns true
  // on success, false on error. If this returns false, no further callbacks
  // will be processed.
  virtual bool OnCodeBlockEntry(const BlockGraph::Block* block,
                                RelativeAddress address,
                                uint32 process_id,
                                uint32 thread_id,
                                const UniqueTime& time) = 0;

  // The derived class shall implement this function, which actually produces
  // the reordering. When this is called, the callee can be assured that the
  // DecomposedImage is populated and all traces have been parsed. This must
  // return true on success, false otherwise.
  virtual bool CalculateReordering(bool reorder_code,
                                   bool reorder_data,
                                   Order* order) = 0;

 private:
  const std::string name_;

  DISALLOW_COPY_AND_ASSIGN(OrderGenerator);
};

// A unique time class. No two instances of this class will ever be equal
// This allows events that map to the same time (down to the resolution reported
// to us) to still maintain a unique temporal ordering. This is done by using
// a secondary counter value. It is necessary because we often get buffers full
// of events that have the same time indicated, but that we know to be in the
// temporal order in which they are stored in the buffer.
class Reorderer::UniqueTime {
 public:
  // This class has a copy-constructor and is assignable in order to be STL
  // container compatible.
  UniqueTime();
  UniqueTime(const UniqueTime& other);
  explicit UniqueTime(const base::Time& time);

  UniqueTime& operator=(const UniqueTime& rhs);

  const base::Time& time() const { return time_; }
  size_t id() const { return id_; }

  // Compares two UniqueTime objects, returning a value from the set {-1, 0, 1}.
  int compare(const UniqueTime& rhs) const;

  // Standard comparison operators.
  bool operator<(const UniqueTime& rhs) const { return compare(rhs) < 0; }
  bool operator>(const UniqueTime& rhs) const { return compare(rhs) > 0; }
  bool operator<=(const UniqueTime& rhs) const { return compare(rhs) <= 0; }
  bool operator>=(const UniqueTime& rhs) const { return compare(rhs) >= 0; }
  bool operator==(const UniqueTime& rhs) const { return compare(rhs) == 0; }
  bool operator!=(const UniqueTime& rhs) const { return compare(rhs) != 0; }

 private:
  base::Time time_;
  size_t id_;

  // Stores the next id that will be used in constructing a unique time object.
  static size_t next_id_;
};

}  // namespace reorder

#endif  // SYZYGY_REORDER_REORDERER_H_
