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
// Common unittest fixtures and utilities for the ASAN runtime library.

#include "syzygy/agent/asan/unittest_util.h"

#include "base/environment.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "syzygy/trace/protocol/call_trace_defs.h"

namespace testing {

TestWithAsanLogger::TestWithAsanLogger()
    : log_service_instance_(&log_service_), log_contents_read_(false) {
}

void TestWithAsanLogger::SetUp() {
  // Create and open the log file.
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  log_file_path_ = temp_dir_.path().Append(L"log.txt");
  log_file_.reset(file_util::OpenFile(log_file_path_, "wb"));

  // Configure the environment (to pass the instance id to the agent DLL).
  std::string instance_id = base::UintToString(::GetCurrentProcessId());
  scoped_ptr<base::Environment> env(base::Environment::Create());
  env->SetVar(kSyzygyRpcInstanceIdEnvVar, instance_id);

  // Configure and start the log service.
  instance_id_ = UTF8ToWide(instance_id);
  log_service_.set_instance_id(instance_id_);
  log_service_.set_destination(log_file_.get());
  ASSERT_TRUE(log_service_.Start());

  log_contents_read_ = false;
}

void TestWithAsanLogger::TearDown() {
  log_service_.Stop();
  log_service_.RunToCompletion();
  log_file_.reset(NULL);
  LogContains("");
}

bool TestWithAsanLogger::LogContains(const base::StringPiece& message) {
  if (!log_contents_read_) {
    std::string contents;
    CHECK(file_util::ReadFileToString(log_file_path_, &log_contents_));
  }
  return log_contents_.find(message.as_string()) != std::string::npos;
}

}  // namespace testing
