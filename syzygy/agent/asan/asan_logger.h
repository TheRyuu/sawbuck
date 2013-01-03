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

#ifndef SYZYGY_AGENT_ASAN_ASAN_LOGGER_H_
#define SYZYGY_AGENT_ASAN_ASAN_LOGGER_H_

#include <string>

#include "base/logging.h"
#include "syzygy/trace/rpc/rpc_helpers.h"

namespace agent {
namespace asan {

// A wrapper class to manage the singleton ASAN RPC logger instance.
class AsanLogger {
 public:
  AsanLogger();

  // Set the RPC instance ID to use. If an instance-id is to be used by the
  // logger, it must be set before calling Init().
  void set_instance_id(const base::StringPiece16& instance_id) {
    DCHECK(rpc_binding_.Get() == NULL);
    instance_id_.assign(instance_id.begin(), instance_id.end());
  }

  // Initialize the logger.
  void Init();

  // Write a @p message to the logger.
  void Write(const std::string& message);

  // Write a @p message to the logger, with an optional stack @p trace
  // containing @p trace_length elements.
  void WriteWithStackTrace(const std::string& message,
                           const void* const* trace_data,
                           size_t trace_length);

 protected:
  // The RPC binding.
  trace::client::ScopedRpcBinding rpc_binding_;

  // The logger's instance id.
  std::wstring instance_id_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AsanLogger);
};

}  // namespace asan
}  // namespace agent

#endif  // SYZYGY_AGENT_ASAN_ASAN_LOGGER_H_
