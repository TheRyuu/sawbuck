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

#include "syzygy/pe/decompose_app.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/block_graph/unittest_util.h"
#include "syzygy/common/unittest_util.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pe/pe_utils.h"
#include "syzygy/pe/unittest_util.h"

namespace pe {

using block_graph::BlockGraph;
using common::Application;
using core::RelativeAddress;
using ::testing::ScopedLogLevelSaver;

namespace {

class TestDecomposeApp : public DecomposeApp {
 public:
  // Member variables.
  using DecomposeApp::image_path_;
  using DecomposeApp::output_path_;
  using DecomposeApp::benchmark_load_;
  using DecomposeApp::strip_strings_;
};

class DecomposeAppTest : public testing::PELibUnitTest {
 public:
  typedef testing::PELibUnitTest Super;
  typedef Application<TestDecomposeApp> TestApplication;

  DecomposeAppTest()
      : cmd_line_(base::FilePath(L"decompose.exe")),
        impl_(app_.implementation()) {
  }

  void SetUp() {
    Super::SetUp();

    // Setup the IO streams.
    CreateTemporaryDir(&temp_dir_);
    stdin_path_ = temp_dir_.Append(L"NUL");
    stdout_path_ = temp_dir_.Append(L"stdout.txt");
    stderr_path_ = temp_dir_.Append(L"stderr.txt");
    InitStreams(stdin_path_, stdout_path_, stderr_path_);

    // Initialize the input and output path values.
    image_path_ = testing::GetExeTestDataRelativePath(testing::kTestDllName);
    output_path_ = temp_dir_.Append(L"output.bg");

    // Point the application at the test's command-line and IO streams.
    app_.set_command_line(&cmd_line_);
    app_.set_in(in());
    app_.set_out(out());
    app_.set_err(err());
  }

 protected:
  // The command line to be given to the application under test.
  CommandLine cmd_line_;

  // The application object under test.
  TestApplication app_;

  // A reference to the underlying application implementation for convenience.
  TestDecomposeApp& impl_;

  // A temporary folder where all IO will be stored.
  base::FilePath temp_dir_;

  // @name File paths used for the standard IO streams.
  // @{
  base::FilePath stdin_path_;
  base::FilePath stdout_path_;
  base::FilePath stderr_path_;
  // @}

  // @name Paths given as command-line parameters
  // @{
  base::FilePath image_path_;
  base::FilePath output_path_;
  // @{
};

}  // namespace

TEST_F(DecomposeAppTest, EmptyCommandLineFails) {
  ASSERT_FALSE(impl_.ParseCommandLine(&cmd_line_));
}

TEST_F(DecomposeAppTest, GetHelp) {
  cmd_line_.AppendSwitch("help");
  ASSERT_FALSE(impl_.ParseCommandLine(&cmd_line_));
}

TEST_F(DecomposeAppTest, ParseCommandLineMinimal) {
  ASSERT_TRUE(impl_.image_path_.empty());
  ASSERT_TRUE(impl_.output_path_.empty());
  ASSERT_FALSE(impl_.benchmark_load_);

  cmd_line_.AppendSwitchPath("image", image_path_);

  ASSERT_TRUE(impl_.ParseCommandLine(&cmd_line_));
  ASSERT_EQ(image_path_, impl_.image_path_);
  ASSERT_EQ(image_path_.value() + L".bg", impl_.output_path_.value());
  ASSERT_FALSE(impl_.benchmark_load_);
  ASSERT_FALSE(impl_.strip_strings_);
}

TEST_F(DecomposeAppTest, ParseCommandLineFull) {
  ASSERT_TRUE(impl_.image_path_.empty());
  ASSERT_TRUE(impl_.output_path_.empty());
  ASSERT_FALSE(impl_.benchmark_load_);

  cmd_line_.AppendSwitchPath("image", image_path_);
  cmd_line_.AppendSwitchPath("output", output_path_);
  cmd_line_.AppendSwitch("benchmark-load");
  cmd_line_.AppendSwitch("strip-strings");

  ASSERT_TRUE(impl_.ParseCommandLine(&cmd_line_));
  ASSERT_EQ(image_path_, impl_.image_path_);
  ASSERT_EQ(output_path_, impl_.output_path_);
  ASSERT_TRUE(impl_.benchmark_load_);
  ASSERT_TRUE(impl_.strip_strings_);
}

TEST_F(DecomposeAppTest, RunOnTestDll) {
  ScopedLogLevelSaver log_level_saver;
  logging::SetMinLogLevel(logging::LOG_FATAL);

  cmd_line_.AppendSwitchPath("image", image_path_);
  cmd_line_.AppendSwitchPath("output", output_path_);
  cmd_line_.AppendSwitch("benchmark-load");

  ASSERT_EQ(0, app_.Run());
}

TEST_F(DecomposeAppTest, RunOnTestDllBlockGraphOnlyNoStrings) {
  ScopedLogLevelSaver log_level_saver;
  logging::SetMinLogLevel(logging::LOG_FATAL);

  cmd_line_.AppendSwitchPath("image", image_path_);
  cmd_line_.AppendSwitchPath("output", output_path_);
  cmd_line_.AppendSwitch("benchmark-load");
  cmd_line_.AppendSwitch("strip-strings");
  cmd_line_.AppendSwitch("graph-only");

  ASSERT_EQ(0, app_.Run());
}

}  // namespace pe
