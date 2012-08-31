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
#ifndef SYZYGY_GRINDER_GRINDER_APP_H_
#define SYZYGY_GRINDER_GRINDER_APP_H_

#include "base/file_path.h"
#include "syzygy/common/application.h"

namespace grinder {

// The application class that takes care of running Grinder over a set of
// profiler trace files.
class GrinderApp : public common::AppImplBase {
 public:
  GrinderApp();

  // The mode of processing we are performing.
  enum Mode {
    kProfile,
    kCoverage,
  };

  // @name Implementation of the AppImplbase interface.
  // @{
  bool ParseCommandLine(const CommandLine* command_line);
  int Run();
  // @}

  // @name Utility functions
  // @{
  void PrintUsage(const FilePath& program,
                  const base::StringPiece& message);
  // @}

 protected:
  // If @p path is an existing file, stores it in trace_files_, otherwise
  // expands it to the files it references and stores in trace_files_.
  // @returns true if path is an existing file, or a pattern that references
  //     one or more files.
  bool ExpandArgument(const FilePath& path);

  std::vector<FilePath> trace_files_;
  FilePath output_file_;
  Mode mode_;
};

}  // namespace grinder

#endif  // SYZYGY_GRINDER_GRINDER_APP_H_
