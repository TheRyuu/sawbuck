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
// Declares the entry thunk instrumenter.
#ifndef SYZYGY_INSTRUMENT_INSTRUMENTERS_ENTRY_THUNK_INSTRUMENTER_H_
#define SYZYGY_INSTRUMENT_INSTRUMENTERS_ENTRY_THUNK_INSTRUMENTER_H_

#include <string>

#include "base/command_line.h"
#include "syzygy/instrument/instrumenters/instrumenter_with_agent.h"
#include "syzygy/instrument/transforms/entry_thunk_transform.h"
#include "syzygy/instrument/transforms/thunk_import_references_transform.h"
#include "syzygy/pe/pe_relinker.h"

namespace instrument {
namespace instrumenters {

class EntryThunkInstrumenter : public InstrumenterWithAgent {
 public:
  // The mode for this instrumenter.
  enum Mode {
    INVALID_MODE,
    CALL_TRACE,
    PROFILE,
  };

  explicit EntryThunkInstrumenter(Mode instrumentation_mode);

  ~EntryThunkInstrumenter() { }

  // Returns the instrumentation mode.
  Mode instrumentation_mode() { return instrumentation_mode_; }

 protected:
  // The name of the agents for the different mode of instrumentation.
  static const char kAgentDllProfile[];
  static const char kAgentDllRpc[];

  // @name InstrumenterWithAgent overrides.
  // @{
  virtual bool InstrumentImpl() OVERRIDE;
  virtual const char* InstrumentationMode() OVERRIDE;
  virtual bool ParseAdditionalCommandLineArguments(
     const CommandLine* command_line) OVERRIDE;
  // @}

  // @name Command-line parameters.
  // @{
  bool instrument_unsafe_references_;
  bool module_entry_only_;
  bool thunk_imports_;
  // @}

  // The instrumentation mode.
  Mode instrumentation_mode_;

  // The transforms for this agent.
  scoped_ptr<instrument::transforms::EntryThunkTransform>
      entry_thunk_transform_;
  scoped_ptr<instrument::transforms::ThunkImportReferencesTransform>
      import_thunk_tx_;
};

}  // namespace instrumenters
}  // namespace instrument

#endif  // SYZYGY_INSTRUMENT_INSTRUMENTERS_ENTRY_THUNK_INSTRUMENTER_H_
