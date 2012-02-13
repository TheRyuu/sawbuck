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

#include "syzygy/playback/playback.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/common/syzygy_version.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pdb/omap.h"
#include "syzygy/pe/pe_file.h"

namespace playback {

namespace {

using testing::_;
using testing::GetSrcRelativePath;
using testing::GetExeRelativePath;
using testing::Return;

using call_trace::parser::ParseEventHandler;

class MockParseEventHandler : public testing::StrictMock<ParseEventHandler> {
 public:
  MOCK_METHOD2(OnProcessStarted, void(base::Time, DWORD));
  MOCK_METHOD2(OnProcessEnded, void(base::Time, DWORD));
  MOCK_METHOD4(OnFunctionEntry, void(base::Time, DWORD, DWORD,
               const TraceEnterExitEventData*));
  MOCK_METHOD4(OnFunctionExit, void(base::Time, DWORD, DWORD,
               const TraceEnterExitEventData*));
  MOCK_METHOD4(OnBatchFunctionEntry, void(base::Time, DWORD, DWORD,
               const TraceBatchEnterData*));
  MOCK_METHOD4(OnProcessAttach, void(base::Time, DWORD, DWORD,
               const TraceModuleData*));
  MOCK_METHOD4(OnProcessDetach, void(base::Time, DWORD, DWORD,
               const TraceModuleData*));
  MOCK_METHOD4(OnThreadAttach, void(base::Time, DWORD, DWORD,
               const TraceModuleData*));
  MOCK_METHOD4(OnThreadDetach, void(base::Time, DWORD, DWORD,
               const TraceModuleData*));
  MOCK_METHOD5(OnInvocationBatch, void(base::Time, DWORD, DWORD,
               size_t, const InvocationInfoBatch*));
};

class PlaybackTest : public testing::Test {
 public:
  PlaybackTest() : image_layout_(&block_graph_) {
  }

  void SetUp() {
    module_path_ =
        GetSrcRelativePath(L"syzygy/playback/test_data/test_dll.dll");

    instrumented_path_ =
        GetSrcRelativePath(L"syzygy/playback/test_data/instrumented_dll.dll");

    const FilePath kTraceFiles[] = {
        GetSrcRelativePath(L"syzygy/playback/test_data/trace-1.bin"),
        GetSrcRelativePath(L"syzygy/playback/test_data/trace-2.bin"),
        GetSrcRelativePath(L"syzygy/playback/test_data/trace-3.bin"),
        GetSrcRelativePath(L"syzygy/playback/test_data/trace-4.bin"),
    };
    trace_files_ = Playback::TraceFileList(kTraceFiles,
        kTraceFiles + arraysize(kTraceFiles));
  }

  bool Init() {
    playback_.reset(
        new Playback(module_path_, instrumented_path_, trace_files_));

    parse_event_handler_.reset(new MockParseEventHandler);
    parser_.reset(new Playback::Parser);
    return parser_->Init(parse_event_handler_.get());
  }

 protected:
  scoped_ptr<Playback> playback_;

  FilePath module_path_;
  FilePath instrumented_path_;
  Playback::TraceFileList trace_files_;

  pe::PEFile input_dll_;
  block_graph::BlockGraph block_graph_;
  pe::ImageLayout image_layout_;

  scoped_ptr<MockParseEventHandler> parse_event_handler_;
  scoped_ptr<Playback::Parser> parser_;
};

}  // namespace

TEST_F(PlaybackTest, OldDLLNewInstrumentedDLL) {
  instrumented_path_ =  GetSrcRelativePath(
      L"test_data/old_instrumented_dll.dll");

  EXPECT_TRUE(Init());
  EXPECT_FALSE(playback_->Init(&input_dll_, &image_layout_, parser_.get()));
}

TEST_F(PlaybackTest, NewDLLOldInstrumentedDLL) {
  module_path_ =  GetSrcRelativePath(L"test_data/old_test_dll.dll");

  EXPECT_TRUE(Init());
  EXPECT_FALSE(playback_->Init(&input_dll_, &image_layout_, parser_.get()));
}

TEST_F(PlaybackTest, BadTraceFile) {
  const FilePath kTraceFile = GetSrcRelativePath(
      L"syzygy/playback/test_data/bad-trace.bin");
  trace_files_ = Playback::TraceFileList(&kTraceFile, &kTraceFile + 1);

  EXPECT_TRUE(Init());
  EXPECT_FALSE(playback_->Init(&input_dll_, &image_layout_, parser_.get()));
}

TEST_F(PlaybackTest, SuccessfulInit) {
  EXPECT_TRUE(Init());
  EXPECT_TRUE(playback_->Init(&input_dll_, &image_layout_, parser_.get()));
}

TEST_F(PlaybackTest, ConsumeCallTraceEvents) {
  EXPECT_TRUE(Init());
  EXPECT_TRUE(playback_->Init(&input_dll_, &image_layout_, parser_.get()));

  EXPECT_CALL(*parse_event_handler_, OnProcessStarted(_, _)).Times(4);
  EXPECT_CALL(*parse_event_handler_, OnProcessEnded(_, _)).Times(4);
  EXPECT_CALL(*parse_event_handler_, OnFunctionEntry(_, _, _, _)).Times(0);
  EXPECT_CALL(*parse_event_handler_, OnFunctionExit(_, _, _, _)).Times(0);
  EXPECT_CALL(*parse_event_handler_,
              OnBatchFunctionEntry(_, _, _, _)).Times(20);
  EXPECT_CALL(*parse_event_handler_, OnProcessAttach(_, _, _, _)).Times(12);
  EXPECT_CALL(*parse_event_handler_, OnProcessDetach(_, _, _, _)).Times(12);
  EXPECT_CALL(*parse_event_handler_, OnThreadAttach(_, _, _, _)).Times(0);
  EXPECT_CALL(*parse_event_handler_, OnThreadDetach(_, _, _, _)).Times(0);
  EXPECT_CALL(*parse_event_handler_,
              OnInvocationBatch(_, _, _, _, _)).Times(0);

  EXPECT_TRUE(parser_->Consume());
}

}  // namespace playback
