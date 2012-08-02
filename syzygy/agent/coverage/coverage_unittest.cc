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
// Coverage client unittests.

#include "syzygy/agent/coverage/coverage.h"

#include "base/file_util.h"
#include "base/scoped_temp_dir.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/agent/coverage/coverage_constants.h"
#include "syzygy/agent/coverage/coverage_data.h"
#include "syzygy/trace/common/unittest_util.h"
#include "syzygy/trace/parse/unittest_util.h"

namespace agent {
namespace coverage {

namespace {

using testing::_;
using testing::StrictMockParseEventHandler;
using trace::parser::Parser;

// This is the static basic-block frequency array that our coverage
// instrumentation will point to.
const uint32 kBasicBlockCount = 2;
uint8 bb_seen_array[kBasicBlockCount] = {};

// Force ourselves to have a .coverage section identical to one that would be
// injected by the coverage instrumentation transform.
#pragma data_seg(push, old_seg)
#pragma data_seg(".cover")
CoverageData coverage_data = {
    kCoverageClientMagic,
    kCoverageClientVersion,
    0,
    kBasicBlockCount,
    bb_seen_array };
#pragma data_seg(pop, old_seg)

MATCHER_P(ModuleAtAddress, module, "") {
  return arg->module_base_addr == module;
}

MATCHER_P3(CoverageDataMatches, module, bb_count, bb_freqs, "") {
  if (arg->module_base_addr != module)
    return false;

  if (arg->frequency_size != 1)
    return false;

  if (arg->basic_block_count != bb_count)
    return false;

  return ::memcmp(bb_freqs, arg->frequency_data, bb_count) == 0;
}

class CoverageClientTest : public testing::Test {
 public:
  CoverageClientTest()
      : module_(NULL) {
  }

  virtual void SetUp() OVERRIDE {
    testing::Test::SetUp();

    coverage_data.basic_block_seen_array = bb_seen_array;
    ::memset(bb_seen_array, 0, sizeof(bb_seen_array));

    // Call trace files will be stuffed here.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    service_.SetEnvironment();
  }

  virtual void TearDown() OVERRIDE {
    UnloadDll();

    // Stop the call trace service.
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

  void LoadDll() {
    ASSERT_TRUE(module_ == NULL);
    ASSERT_TRUE(_indirect_penter_dllmain_ == NULL);
    static const wchar_t kCallTraceDll[] = L"coverage_client.dll";
    ASSERT_EQ(NULL, ::GetModuleHandle(kCallTraceDll));
    module_ = ::LoadLibrary(kCallTraceDll);
    ASSERT_TRUE(module_ != NULL);

    _indirect_penter_dllmain_ =
        ::GetProcAddress(module_, "_indirect_penter_dllmain");
    ASSERT_TRUE(_indirect_penter_dllmain_ != NULL);
  }

  void UnloadDll() {
    if (module_ != NULL) {
      ASSERT_TRUE(::FreeLibrary(module_));
      module_ = NULL;
      _indirect_penter_dllmain_ = NULL;
    }
  }

  static BOOL WINAPI IndirectDllMain(HMODULE module,
                                     DWORD reason,
                                     LPVOID reserved);
  static BOOL WINAPI DllMainThunk(HMODULE module,
                                  DWORD reason,
                                  LPVOID reserved);

 protected:
  // The directory where trace file output will be written.
  ScopedTempDir temp_dir_;

  // The handler to which the trace file parser will delegate events.
  StrictMockParseEventHandler handler_;

  // Our call trace service process instance.
  testing::CallTraceService service_;

 private:
  HMODULE module_;
  static FARPROC _indirect_penter_dllmain_;
};

FARPROC CoverageClientTest::_indirect_penter_dllmain_ = NULL;

BOOL WINAPI CoverageClientTest::IndirectDllMain(HMODULE module,
                                                DWORD reason,
                                                LPVOID reserved) {
  return TRUE;
}

BOOL __declspec(naked) WINAPI CoverageClientTest::DllMainThunk(
    HMODULE module, DWORD reason, LPVOID reserved) {
  __asm {
    push IndirectDllMain
    jmp _indirect_penter_dllmain_
  }
}

void VisitBlock(size_t i) {
  EXPECT_GT(coverage_data.basic_block_count, i);
  coverage_data.basic_block_seen_array[i] = 1;
}

}  // namespace

TEST_F(CoverageClientTest, NoServerNoCrash) {
  ASSERT_NO_FATAL_FAILURE(LoadDll());

  const uint8* data = coverage_data.basic_block_seen_array;
  EXPECT_TRUE(DllMainThunk(::GetModuleHandle(NULL), DLL_PROCESS_ATTACH, NULL));

  // There should be no allocation.
  ASSERT_EQ(data, coverage_data.basic_block_seen_array);

  // Visiting blocks should not fail.
  VisitBlock(0);
  VisitBlock(1);

  // Unload the DLL and stop the service.
  ASSERT_NO_FATAL_FAILURE(UnloadDll());

  // Replay the log. There should be none as we didn't initialize the client.
  ASSERT_NO_FATAL_FAILURE(ReplayLogs(0));
}

TEST_F(CoverageClientTest, VisitOneBB) {
  ASSERT_NO_FATAL_FAILURE(StartService());
  ASSERT_NO_FATAL_FAILURE(LoadDll());

  HMODULE self = ::GetModuleHandle(NULL);
  DWORD process_id = ::GetCurrentProcessId();
  DWORD thread_id = ::GetCurrentThreadId();

  const uint8* data = coverage_data.basic_block_seen_array;
  EXPECT_TRUE(DllMainThunk(self, DLL_PROCESS_ATTACH, NULL));

  // There should have been an allocation.
  ASSERT_NE(data, coverage_data.basic_block_seen_array);
  data = coverage_data.basic_block_seen_array;

  // Calling the entry thunk repeatedly should not fail, and should not cause
  // a reallocation.
  EXPECT_TRUE(DllMainThunk(self, DLL_PROCESS_ATTACH, NULL));
  ASSERT_EQ(data, coverage_data.basic_block_seen_array);

  VisitBlock(0);

  // Unload the DLL and stop the service.
  ASSERT_NO_FATAL_FAILURE(UnloadDll());

  const uint8 kExpectedCoverageData[kBasicBlockCount] = { 1, 0 };

  // Set up expectations for what should be in the trace.
  EXPECT_CALL(handler_, OnProcessStarted(_, process_id, _));
  EXPECT_CALL(handler_, OnProcessAttach(_,
                                        process_id,
                                        thread_id,
                                        ModuleAtAddress(self)));;
  EXPECT_CALL(handler_, OnBasicBlockFrequency(
      _,
      process_id,
      thread_id,
      CoverageDataMatches(self, kBasicBlockCount, kExpectedCoverageData)));
  EXPECT_CALL(handler_, OnProcessEnded(_, process_id));

  // Replay the log.
  ASSERT_NO_FATAL_FAILURE(ReplayLogs(1));
}

}  // namespace coverage
}  // namespace agent
