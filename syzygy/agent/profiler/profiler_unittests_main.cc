// Copyright 2012 Google Inc. All Rights Reserved.
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
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/win/scoped_com_initializer.h"
#include "gtest/gtest.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  CommandLine::Init(argc, argv);
  base::AtExitManager at_exit;
  // Run these tests CoInitialized, as otherwise the CoInit state of the main
  // thread will Yo-Yo, leading to repeated loading and unloading of arbitrary
  // COM DLLs on arbitrary threads, which leads to flakiness.
  base::win::ScopedCOMInitializer com_initializer;

  return RUN_ALL_TESTS();
}
