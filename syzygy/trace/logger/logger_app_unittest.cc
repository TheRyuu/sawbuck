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

#include "syzygy/trace/logger/logger_app.h"

#include <psapi.h>
#include <userenv.h>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/file_util.h"
#include "base/process_util.h"
#include "base/scoped_temp_dir.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "base/memory/scoped_ptr.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/common/align.h"
#include "syzygy/common/unittest_util.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/trace/client/client_utils.h"
#include "syzygy/trace/parse/parse_utils.h"
#include "syzygy/trace/protocol/call_trace_defs.h"
#include "syzygy/trace/rpc/rpc_helpers.h"
#include "syzygy/trace/service/service_rpc_impl.h"
#include "syzygy/trace/service/trace_file_writer_factory.h"

using namespace trace::client;

namespace trace {
namespace logger {

namespace {

class TestLoggerApp : public LoggerApp {
 public:
  using LoggerApp::FindActionHandler;
  using LoggerApp::OpenOutputFile;
  using LoggerApp::kStart;
  using LoggerApp::kSpawn;
  using LoggerApp::kStatus;
  using LoggerApp::kStop;
  using LoggerApp::kInstanceId;
  using LoggerApp::kOutputFile;
  using LoggerApp::kStdOut;
  using LoggerApp::kStdErr;
  using LoggerApp::logger_command_line_;
  using LoggerApp::app_command_line_;
  using LoggerApp::instance_id_;
  using LoggerApp::action_;
  using LoggerApp::action_handler_;
  using LoggerApp::output_file_path_;
  using LoggerApp::append_;
};

typedef common::Application<TestLoggerApp> TestApp;

class LoggerAppTest : public testing::ApplicationTestBase {
 public:
  typedef testing::ApplicationTestBase Super;

  LoggerAppTest()
      : cmd_line_(FilePath(L"logger.exe")),
        test_impl_(test_app_.implementation()) {
  }

  void SetUp() {
    Super::SetUp();

    // Several of the tests generate progress and (deliberate) error messages
    // that would otherwise clutter the unit-test output.
    logging::SetMinLogLevel(logging::LOG_FATAL);

    // Setup the IO streams.
    CreateTemporaryDir(&temp_dir_);
    stdin_path_ = temp_dir_.Append(L"NUL");
    stdout_path_ = temp_dir_.Append(L"stdout.txt");
    stderr_path_ = temp_dir_.Append(L"stderr.txt");
    InitStreams(stdin_path_, stdout_path_, stderr_path_);

    // Initialize the (potential) input and output path values.
    instance_id_ = base::StringPrintf(L"%d", ::GetCurrentProcessId());
    output_file_path_ = temp_dir_.Append(L"output.txt");

    // Point the application at the test's command-line and IO streams.
    test_app_.set_command_line(&cmd_line_);
    test_app_.set_in(in());
    test_app_.set_out(out());
    test_app_.set_err(err());
  }

 protected:
  // Stashes the current log-level before each test instance and restores it
  // after each test completes.
  testing::ScopedLogLevelSaver log_level_saver;

  // @name The application under test.
  // @{
  TestApp test_app_;
  TestApp::Implementation& test_impl_;
  FilePath temp_dir_;
  FilePath stdin_path_;
  FilePath stdout_path_;
  FilePath stderr_path_;
  // @}

  // @name Command-line and parameters.
  // @{
  CommandLine cmd_line_;
  std::wstring instance_id_;
  FilePath output_file_path_;
  // @}
};

}  // namespace

TEST_F(LoggerAppTest, GetHelp) {
  cmd_line_.AppendSwitch("help");
  ASSERT_FALSE(test_impl_.ParseCommandLine(&cmd_line_));
}

TEST_F(LoggerAppTest, EmptyCommandLineFails) {
  ASSERT_FALSE(test_impl_.ParseCommandLine(&cmd_line_));
}

TEST_F(LoggerAppTest, ParseBasicStart) {
  cmd_line_.AppendSwitchPath(
      TestLoggerApp::kOutputFile, output_file_path_);
  cmd_line_.AppendSwitchNative(
      TestLoggerApp::kInstanceId, instance_id_);
  cmd_line_.AppendArgNative(TestLoggerApp::kStart);
  ASSERT_TRUE(test_impl_.ParseCommandLine(&cmd_line_));
  EXPECT_EQ(output_file_path_, test_impl_.output_file_path_);
  EXPECT_EQ(instance_id_, test_impl_.instance_id_);
  EXPECT_EQ(std::wstring(TestLoggerApp::kStart), test_impl_.action_);
  EXPECT_TRUE(test_impl_.app_command_line_.get() == NULL);
  EXPECT_EQ(&TestLoggerApp::Start, test_impl_.action_handler_);
}

TEST_F(LoggerAppTest, ParseStartWithCommand) {
  const FilePath kFooExe(L"foo.exe");
  const std::wstring kSwitchName(L"switch");
  const std::wstring kSwitchValue(L"value");
  const std::wstring kDash(L"--");

  cmd_line_.AppendSwitchPath(
      TestLoggerApp::kOutputFile, output_file_path_);
  cmd_line_.AppendSwitchNative(
      TestLoggerApp::kInstanceId, instance_id_);
  cmd_line_.AppendArgNative(TestLoggerApp::kStart);
  cmd_line_.AppendArgNative(kDash);
  cmd_line_.AppendArgNative(kFooExe.value());
  cmd_line_.AppendArgNative(kDash + kSwitchName + L"=" + kSwitchValue);
  ASSERT_TRUE(test_impl_.ParseCommandLine(&cmd_line_));
  EXPECT_EQ(output_file_path_, test_impl_.output_file_path_);
  EXPECT_EQ(instance_id_, test_impl_.instance_id_);
  EXPECT_EQ(std::wstring(TestLoggerApp::kStart), test_impl_.action_);
  const CommandLine* app_cmd_line = test_impl_.app_command_line_.get();
  ASSERT_TRUE(app_cmd_line != NULL);
  EXPECT_EQ(kFooExe, app_cmd_line->GetProgram());
  EXPECT_EQ(kSwitchValue,
            app_cmd_line->GetSwitchValueNative(WideToASCII(kSwitchName)));
  EXPECT_EQ(&TestLoggerApp::Start, test_impl_.action_handler_);
}

TEST_F(LoggerAppTest, ParseSpawn) {
  cmd_line_.AppendSwitchNative(
      TestLoggerApp::kInstanceId, instance_id_);
  cmd_line_.AppendArgNative(TestLoggerApp::kSpawn);
  ASSERT_TRUE(test_impl_.ParseCommandLine(&cmd_line_));
  EXPECT_TRUE(test_impl_.output_file_path_.empty());
  EXPECT_EQ(instance_id_, test_impl_.instance_id_);
  EXPECT_EQ(std::wstring(TestLoggerApp::kSpawn), test_impl_.action_);
  EXPECT_EQ(NULL, test_impl_.app_command_line_.get());
  EXPECT_EQ(&TestLoggerApp::Spawn, test_impl_.action_handler_);
}

TEST_F(LoggerAppTest, ParseStop) {
  cmd_line_.AppendSwitchNative(
      TestLoggerApp::kInstanceId, instance_id_);
  cmd_line_.AppendArgNative(TestLoggerApp::kStop);
  ASSERT_TRUE(test_impl_.ParseCommandLine(&cmd_line_));
  EXPECT_TRUE(test_impl_.output_file_path_.empty());
  EXPECT_EQ(instance_id_, test_impl_.instance_id_);
  EXPECT_EQ(std::wstring(TestLoggerApp::kStop), test_impl_.action_);
  EXPECT_EQ(NULL, test_impl_.app_command_line_.get());
  EXPECT_EQ(&TestLoggerApp::Stop, test_impl_.action_handler_);
}

TEST_F(LoggerAppTest, ParseStatus) {
  cmd_line_.AppendSwitchNative(
      TestLoggerApp::kInstanceId, instance_id_);
  cmd_line_.AppendArgNative(TestLoggerApp::kStatus);
  ASSERT_TRUE(test_impl_.ParseCommandLine(&cmd_line_));
  EXPECT_TRUE(test_impl_.output_file_path_.empty());
  EXPECT_EQ(instance_id_, test_impl_.instance_id_);
  EXPECT_EQ(std::wstring(TestLoggerApp::kStatus), test_impl_.action_);
  EXPECT_EQ(NULL, test_impl_.app_command_line_.get());
  EXPECT_EQ(&TestLoggerApp::Status, test_impl_.action_handler_);
}

TEST_F(LoggerAppTest, ParseUnknown) {
  const std::wstring kUnknown(L"unknown");
  cmd_line_.AppendSwitchNative(
      TestLoggerApp::kInstanceId, instance_id_);
  cmd_line_.AppendArgNative(kUnknown);
  ASSERT_FALSE(test_impl_.ParseCommandLine(&cmd_line_));
  EXPECT_TRUE(test_impl_.output_file_path_.empty());
  EXPECT_EQ(instance_id_, test_impl_.instance_id_);
  EXPECT_EQ(kUnknown, test_impl_.action_);
  EXPECT_EQ(NULL, test_impl_.app_command_line_.get());
  EXPECT_TRUE(test_impl_.action_handler_ == NULL);
}

TEST_F(LoggerAppTest, StartEndToEnd) {
  cmd_line_.AppendSwitchNative(
      TestLoggerApp::kInstanceId, instance_id_);
  cmd_line_.AppendArgNative(TestLoggerApp::kStart);
  cmd_line_.AppendArgNative(L"--");
  cmd_line_.AppendArgNative(L"cmd.exe");
  cmd_line_.AppendArgNative(L"/C");
  cmd_line_.AppendArgNative(L"date /t");

  ASSERT_EQ(0, test_app_.Run());
}

TEST_F(LoggerAppTest, SpawnStatusAndStop) {
  // TODO(rogerm): Write me!
}

}  // namespace logger
}  // namespace trace
