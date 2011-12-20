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
// Profiler unittests.
#include "syzygy/call_trace/profiler.h"

#include "base/file_util.h"
#include "base/scoped_temp_dir.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/call_trace/parser.h"
#include "syzygy/call_trace/service.h"

namespace call_trace {
namespace client {

namespace {

using call_trace::service::Service;
using call_trace::parser::Parser;
using call_trace::parser::ParseEventHandler;
using file_util::FileEnumerator;
using testing::_;
using testing::Return;
using testing::StrictMock;

class MockParseEventHandler : public ParseEventHandler {
 public:
  MOCK_METHOD2(OnProcessStarted, void (base::Time time, DWORD process_id));
  MOCK_METHOD2(OnProcessEnded, void (base::Time time, DWORD process_id));
  MOCK_METHOD4(OnFunctionEntry, void (base::Time time,
                                      DWORD process_id,
                                      DWORD thread_id,
                                      const TraceEnterExitEventData* data));
  MOCK_METHOD4(OnFunctionExit, void (base::Time time,
                                     DWORD process_id,
                                     DWORD thread_id,
                                     const TraceEnterExitEventData* data));
  MOCK_METHOD4(OnBatchFunctionEntry, void (base::Time time,
                                           DWORD process_id,
                                           DWORD thread_id,
                                           const TraceBatchEnterData* data));
  MOCK_METHOD4(OnProcessAttach, void (base::Time time,
                                      DWORD process_id,
                                      DWORD thread_id,
                                      const TraceModuleData* data));
  MOCK_METHOD4(OnProcessDetach, void (base::Time time,
                                      DWORD process_id,
                                      DWORD thread_id,
                                      const TraceModuleData* data));
  MOCK_METHOD4(OnThreadAttach, void (base::Time time,
                                     DWORD process_id,
                                     DWORD thread_id,
                                     const TraceModuleData* data));
  MOCK_METHOD4(OnThreadDetach, void (base::Time time,
                                     DWORD process_id,
                                     DWORD thread_id,
                                     const TraceModuleData* data));
  MOCK_METHOD5(OnInvocationBatch, void (base::Time time,
                                        DWORD process_id,
                                        DWORD thread_id,
                                        size_t num_batches,
                                        const InvocationInfoBatch* data));
};

class ProfilerTest : public testing::Test {
 public:
  ProfilerTest() : module_(NULL) {
  }

  virtual void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    Service::Instance().set_trace_directory(temp_dir_.path());
  }

  virtual void TearDown() {
    UnloadDll();
    Service::Instance().Stop();
  }

  void ReplayLogs() {
    // Stop the service if it's running.
    Service::Instance().Stop();

    Parser parser;
    parser.Init(&handler_);

    // Queue up the trace file(s) we engendered.
    file_util::FileEnumerator enumerator(temp_dir_.path(),
                                         false,
                                         FileEnumerator::FILES);

    while (true) {
      FilePath trace_file = enumerator.Next();
      if (trace_file.empty())
        break;
      ASSERT_TRUE(parser.OpenTraceFile(trace_file));
    }

    ASSERT_TRUE(parser.Consume());
  }

  // TODO(siggi): These are shareable with the other instrumentation DLL tests.
  //    Move them to a shared fixture superclass.
  void LoadDll() {
    ASSERT_TRUE(module_ == NULL);
    const wchar_t* call_trace_dll = L"profile_client.dll";
    ASSERT_EQ(NULL, ::GetModuleHandle(call_trace_dll));
    module_ = ::LoadLibrary(call_trace_dll);
    ASSERT_TRUE(module_ != NULL);
    _indirect_penter_dllmain_ =
        GetProcAddress(module_, "_indirect_penter_dllmain");
    _indirect_penter_ = GetProcAddress(module_, "_indirect_penter");

    ASSERT_TRUE(_indirect_penter_dllmain_ != NULL);
    ASSERT_TRUE(_indirect_penter_ != NULL);
  }

  void UnloadDll() {
    if (module_ != NULL) {
      ASSERT_TRUE(::FreeLibrary(module_));
      module_ = NULL;
      _indirect_penter_ = NULL;
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
  StrictMock<MockParseEventHandler> handler_;

 private:
  ScopedTempDir temp_dir_;
  HMODULE module_;
  static FARPROC _indirect_penter_;
  static FARPROC _indirect_penter_dllmain_;
};

FARPROC ProfilerTest::_indirect_penter_ = NULL;
FARPROC ProfilerTest::_indirect_penter_dllmain_ = NULL;

BOOL WINAPI ProfilerTest::IndirectDllMain(HMODULE module,
                                          DWORD reason,
                                          LPVOID reserved) {
  return TRUE;
}

BOOL __declspec(naked) WINAPI ProfilerTest::DllMainThunk(HMODULE module,
                                                         DWORD reason,
                                                         LPVOID reserved) {
  __asm {
    push IndirectDllMain
    jmp _indirect_penter_dllmain_
  }
}

}  // namespace

TEST_F(ProfilerTest, NoServerNoCrash) {
  ASSERT_NO_FATAL_FAILURE(LoadDll());

  EXPECT_TRUE(DllMainThunk(NULL, DLL_PROCESS_ATTACH, NULL));
}

TEST_F(ProfilerTest, RecordsModuleAndFunctions) {
  // Spin up the RPC service.
  ASSERT_TRUE(Service::Instance().Start(true));

  // Get our own module handle.
  HMODULE self_module = ::GetModuleHandle(NULL);

  ASSERT_NO_FATAL_FAILURE(LoadDll());
  EXPECT_TRUE(DllMainThunk(self_module, DLL_PROCESS_ATTACH, NULL));
  ASSERT_NO_FATAL_FAILURE(UnloadDll());

  EXPECT_CALL(handler_, OnProcessStarted(_, ::GetCurrentProcessId()));
  EXPECT_CALL(handler_, OnProcessAttach(_,
                                       ::GetCurrentProcessId(),
                                       ::GetCurrentThreadId(),
                                       _));
  // TODO(siggi): Match harder here.
  EXPECT_CALL(handler_, OnInvocationBatch(_,
                                       ::GetCurrentProcessId(),
                                       ::GetCurrentThreadId(),
                                       1,
                                       _));
  EXPECT_CALL(handler_, OnProcessEnded(_, ::GetCurrentProcessId()));

  // Replay the log.
  ASSERT_NO_FATAL_FAILURE(ReplayLogs());
}

}  // namespace client
}  // namespace call_trace
