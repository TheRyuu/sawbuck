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

// This file implements the RPC stubs which bind the LoggerService RPC
// handlers to a Logger instance.

#include "syzygy/trace/logger/logger_rpc_impl.h"

#include <windows.h>
#include <winnt.h>

#include "base/process.h"
#include "base/win/scoped_handle.h"
#include "sawbuck/common/com_utils.h"
#include "syzygy/trace/logger/logger.h"
#include "syzygy/trace/rpc/logger_rpc.h"

namespace {

using base::ProcessId;
using base::win::ScopedHandle;
using trace::logger::RpcLoggerInstanceManager;
using trace::logger::Logger;

bool GetClientProcessHandle(handle_t binding, ScopedHandle* handle) {
  DCHECK(handle != NULL);

  // Get the RPC call attributes.
  static const int kVersion = 2;
  RPC_CALL_ATTRIBUTES_V2 attribs = { kVersion, RPC_QUERY_CLIENT_PID };
  RPC_STATUS status = RpcServerInqCallAttributes(binding, &attribs);
  if (status != RPC_S_OK) {
    LOG(ERROR) << "Failed to query RPC call attributes: "
               << com::LogWe(status) << ".";
    return false;
  }

  // Extract the process id.
  ProcessId pid = reinterpret_cast<ProcessId>(attribs.ClientPID);

  // Open and return the handle to the process.
  static const DWORD kFlags =
      PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
  handle->Set(::OpenProcess(kFlags, FALSE, pid));
  if (!handle->IsValid()) {
    DWORD error = ::GetLastError();
    LOG(ERROR) << "Failed to open PID=" << pid << ": " << com::LogWe(error)
               << ".";
    return false;
  }

  // And we're done.
  return true;
}

void InitContext(const ExecutionContext* ext_ctx, CONTEXT* ctx) {
  DCHECK(ext_ctx != NULL);
  DCHECK(ctx != NULL);

  ::memset(ctx, 0, sizeof(*ctx));
  ctx->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

  // Populate the integer registers.
  ctx->Edi = ext_ctx->edi;
  ctx->Esi = ext_ctx->esi;
  ctx->Ebx = ext_ctx->ebx;
  ctx->Edx = ext_ctx->edx;
  ctx->Ecx = ext_ctx->ecx;
  ctx->Eax = ext_ctx->eax;

  // Populate the control registers.
  ctx->Ebp = ext_ctx->ebp;
  ctx->Eip = ext_ctx->eip;
  ctx->SegCs = ext_ctx->seg_cs;
  ctx->EFlags = ext_ctx->eflags;
  ctx->Esp = ext_ctx->esp;
  ctx->SegSs = ext_ctx->seg_ss;
}

}  // namespace

// The instance to which the RPC callbacks are bound.
Logger* RpcLoggerInstanceManager::instance_ = NULL;

// RPC entrypoint for Logger::Write().
boolean LoggerService_Write(
    /* [in] */ handle_t binding,
    /* [string][in] */ const unsigned char *text) {
  if (binding == NULL || text == NULL) {
    LOG(ERROR) << "Invalid input parameter(s).";
    return false;
  }

  // Get the logger instance.
  Logger* instance = RpcLoggerInstanceManager::GetInstance();

  // Write the log message.
  std::string message(reinterpret_cast<const char*>(text));
  if (!instance->Write(message))
    return false;

  // And we're done.
  return true;
}

boolean LoggerService_WriteWithContext(
  /* [in] */ handle_t binding,
  /* [in, string] */ const unsigned char* text,
  /* [in */ const ExecutionContext* exc_context ) {
  if (binding == NULL || text == NULL || exc_context == NULL) {
    LOG(ERROR) << "Invalid input parameter(s).";
    return false;
  }

  // Get the PID of the caller.
  ScopedHandle handle;
  if (!GetClientProcessHandle(binding, &handle))
    return false;

  // Get the logger instance.
  Logger* instance = RpcLoggerInstanceManager::GetInstance();

  // Capture the stack trace for the caller's context.
  CONTEXT context = {};
  InitContext(exc_context, &context);
  std::vector<DWORD> trace_data;
  if (!instance->CaptureRemoteTrace(handle, &context, &trace_data)) {
    return false;
  }

  // Create the log message.
  std::string message(reinterpret_cast<const char*>(text));
  if (!instance->AppendTrace(
          handle, trace_data.data(), trace_data.size(), &message)) {
    return false;
  }

  // Write the log message.
  if (!instance->Write(message))
    return false;

  // And we're done.
  return true;
}

boolean LoggerService_WriteWithTrace(
  /* [in] */ handle_t binding,
  /* [in, string] */ const unsigned char* text,
  /* [in, size_is(trace_length)] */ const unsigned long* trace_data,
  /* [in] */ LONG trace_length) {
  if (binding == NULL || text == NULL || trace_data == NULL) {
    LOG(ERROR) << "Invalid input parameter(s).";
    return false;
  }

  // Get the PID of the caller.
  ScopedHandle handle;
  if (!GetClientProcessHandle(binding, &handle))
    return false;

  // Get the logger instance.
  Logger* instance = RpcLoggerInstanceManager::GetInstance();

  // Create the log message.
  std::string message(reinterpret_cast<const char*>(text));
  if (!instance->AppendTrace(handle, trace_data, trace_length, &message))
    return false;

  // Write the log message.
  if (!instance->Write(message))
    return false;

  // And we're done.
  return true;
}

// RPC entrypoint for Logger::Stop().
boolean LoggerService_Stop(/* [in] */ handle_t binding) {
  if (binding == NULL) {
    LOG(ERROR) << "Invalid input parameter(s).";
    return false;
  }

  Logger* instance = RpcLoggerInstanceManager::GetInstance();
  if (!instance->Stop())
    return false;

  return true;
}
