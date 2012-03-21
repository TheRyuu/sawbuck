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
//
// This defines the virtual simulator class, which analyzes trace files from
// the execution of an instrumented dll, and calls the respective events
// from a subclass of SimulatorEventHandler.

#ifndef SYZYGY_SIMULATE_SIMULATOR_H_
#define SYZYGY_SIMULATE_SIMULATOR_H_

#include "syzygy/playback/playback.h"
#include "syzygy/simulate/simulation_event_handler.h"
#include "syzygy/trace/parse/parser.h"

namespace simulate {

// This class consumes a set of call-trace logs and reports the number of
// pagefaults on them.
class Simulator : public trace::parser::ParseEventHandler {
 public:
  typedef playback::Playback Playback;
  typedef Playback::TraceFileList TraceFileList;

  // Construct a new Simulator instance.
  // @param module_path The path of the module dll.
  // @param instrumented_path The path of the instrumented dll.
  // @param trace_files A list of trace files to analyze.
  // @param simulation The simulation where the events will be fed.
  Simulator(const FilePath& module_path,
            const FilePath& instrumented_path,
            const TraceFileList& trace_files,
            SimulationEventHandler* simulation);

  // Decomposes the image, parses the trace files and captures
  // the pagefaults on them.
  // @returns true on success, false on failure.
  bool ParseTraceFiles();

 protected:
  typedef block_graph::BlockGraph BlockGraph;
  typedef pe::PEFile PEFile;
  typedef pe::ImageLayout ImageLayout;
  typedef trace::parser::Parser Parser;

  // @name ParseEventHandler implementation.
  // @{
  // The implementation for these functions is provided in here so that
  // derived classes of Simulator only need to reimplement the ones they use.
  virtual void OnProcessStarted(
      base::Time time, DWORD process_id,
      const TraceSystemInfo* data) OVERRIDE;
  virtual void OnProcessEnded(base::Time time, DWORD process_id) OVERRIDE;
  virtual void OnFunctionEntry(
      base::Time time, DWORD process_id, DWORD thread_id,
      const TraceEnterExitEventData* data) OVERRIDE;
  virtual void OnFunctionExit(
      base::Time time, DWORD process_id, DWORD thread_id,
      const TraceEnterExitEventData* data) OVERRIDE;
  virtual void OnBatchFunctionEntry(
      base::Time time, DWORD process_id, DWORD thread_id,
      const TraceBatchEnterData* data) OVERRIDE;
  virtual void OnProcessAttach(
      base::Time time, DWORD process_id, DWORD thread_id,
      const TraceModuleData* data) OVERRIDE;
  virtual void OnProcessDetach(
      base::Time time, DWORD process_id, DWORD thread_id,
      const TraceModuleData* data) OVERRIDE;
  virtual void OnThreadAttach(
      base::Time time, DWORD process_id, DWORD thread_id,
      const TraceModuleData* data) OVERRIDE;
  virtual void OnThreadDetach(
      base::Time time, DWORD process_id, DWORD thread_id,
      const TraceModuleData* data) OVERRIDE;
  virtual void OnInvocationBatch(
      base::Time time, DWORD process_id, DWORD thread_id, size_t num_batches,
      const TraceBatchInvocationInfo* data) OVERRIDE;
  // @}

  // The input files.
  FilePath module_path_;
  FilePath instrumented_path_;
  TraceFileList trace_files_;

  // The PE file and Image layout to be passed to playback_.
  BlockGraph block_graph_;
  PEFile pe_file_;
  ImageLayout image_layout_;

  // A Playback, which would decompose the given image and call the On...
  // functions on this Simulator.
  scoped_ptr<Playback> playback_;

  // The call-trace log file parser. This can be preset to a custom parser
  // prior to calling Simulator.
  scoped_ptr<Parser> parser_;

  // A pointer to a simulation, that is to be used.
  SimulationEventHandler* simulation_;
};

} // namespace simulate

#endif  // SYZYGY_SIMULATE_SIMULATOR_H_
