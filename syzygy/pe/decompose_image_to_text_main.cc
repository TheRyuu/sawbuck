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
// Decomposes an image, then dumps the blocks and references to stdout.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "syzygy/pe/decompose_image_to_text_app.h"

int main(int argc, const char* const* argv) {
  base::AtExitManager at_exit_manager;
  CommandLine::Init(argc, argv);
  return common::Application<pe::DecomposeImageToTextApp>().Run();
}
