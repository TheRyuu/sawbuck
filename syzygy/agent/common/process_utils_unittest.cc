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

#include "syzygy/agent/common/process_utils.h"

#include <algorithm>

#include "base/environment.h"
#include "base/file_util.h"
#include "base/scoped_temp_dir.h"
#include "base/utf_string_conversions.h"
#include "gtest/gtest.h"
#include "syzygy/trace/client/rpc_session.h"
#include "syzygy/trace/common/unittest_util.h"
#include "syzygy/trace/parse/unittest_util.h"

namespace agent {
namespace common {

namespace {

using testing::_;
using testing::StrictMockParseEventHandler;
using trace::parser::Parser;

MATCHER_P(ModuleAtAddress, module, "") {
  return arg->module_base_addr == module;
}

// TODO(chrisha): Create agent_test_utils, and centralize the basic
//     functionality of this fixture. All agents can then use that.
//     Don't forget the 'ModuleAtAddress' above as well.
class ProcessUtilsTest : public testing::Test {
 public:
  virtual void SetUp() OVERRIDE {
    testing::Test::SetUp();

    // Call trace files will be stuffed here.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    service_.SetEnvironment();
  }

  virtual void TearDown() OVERRIDE {
    service_.Stop();
  }

  void StartService() {
    service_.Start(temp_dir_.path());
  }

  void StopService() {
    service_.Stop();
  }

  void ReplayLogs(size_t files_expected) {
    // Stop the service if it's running.
    ASSERT_NO_FATAL_FAILURE(StopService());

    Parser parser;
    ASSERT_TRUE(parser.Init(&handler_));

    // Queue up the trace file(s) we engendered.
    file_util::FileEnumerator enumerator(temp_dir_.path(),
                                         false,
                                         file_util::FileEnumerator::FILES);
    size_t num_files = 0;
    while (true) {
      FilePath trace_file = enumerator.Next();
      if (trace_file.empty())
        break;
      ASSERT_TRUE(parser.OpenTraceFile(trace_file));
      ++num_files;
    }

    EXPECT_EQ(files_expected, num_files);

    if (num_files > 0)
      ASSERT_TRUE(parser.Consume());
  }

 protected:
  // The directory where trace file output will be written.
  ScopedTempDir temp_dir_;

  // The handler to which the trace file parser will delegate events.
  StrictMockParseEventHandler handler_;

  // Our call trace service process instance.
  testing::CallTraceService service_;
};

}  // namespace

TEST_F(ProcessUtilsTest, GetProcessModules) {
  ModuleVector modules;

  GetProcessModules(&modules);

  // Make sure our own module is in the list.
  HMODULE exe_module = ::GetModuleHandle(NULL);
  EXPECT_TRUE(
      std::find(modules.begin(), modules.end(), exe_module) != modules.end());

  // We have some imports, so there should be
  // more than just our own module here.
  EXPECT_LT(1U, modules.size());
}

TEST_F(ProcessUtilsTest, LogModule) {
  ASSERT_NO_FATAL_FAILURE(StartService());

  // Initialize the session.
  trace::client::RpcSession session;
  trace::client::TraceFileSegment segment;
  scoped_ptr<base::Environment> env(base::Environment::Create());
  std::string id;
  env->GetVar(::kSyzygyRpcInstanceIdEnvVar, &id);
  session.set_instance_id(UTF8ToWide(id));
  session.CreateSession(&segment);

  HMODULE self = ::GetModuleHandle(NULL);
  DWORD process_id = ::GetCurrentProcessId();
  DWORD thread_id = ::GetCurrentThreadId();

  ASSERT_TRUE(LogModule(self, &session, &segment));

  ASSERT_NO_FATAL_FAILURE(StopService());

  EXPECT_CALL(handler_, OnProcessStarted(_, process_id, _));
  EXPECT_CALL(handler_, OnProcessAttach(_,
                                        process_id,
                                        thread_id,
                                        ModuleAtAddress(self)));;
  EXPECT_CALL(handler_, OnProcessEnded(_, process_id));

  ASSERT_NO_FATAL_FAILURE(ReplayLogs(1));
}

}  // namespace common
}  // namespace agent
