// Copyright 2010 Google Inc.
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
// Implementation of call trace log parsing.
#include "sawbuck/call_trace/call_trace_parser.h"
#include "base/logging.h"
#include "sawbuck/log_lib/buffer_parser.h"

CallTraceParser::CallTraceParser() : call_trace_events_(NULL) {
}

CallTraceParser::~CallTraceParser() {
}

bool CallTraceParser::ProcessEntryExitEvent(EVENT_TRACE* event,
                                            TraceEventType type) {
  if (call_trace_events_ == NULL)
    return false;

  BinaryBufferReader reader(event->MofData, event->MofLength);
  const TraceEnterExitEventData* data = NULL;

  if (!reader.Read(FIELD_OFFSET(TraceEnterExitEventData, traces), &data)) {
    LOG(ERROR) << "Short event header.";
    return false;
  }

  if (!reader.Consume(data->num_traces * sizeof(data->traces[0]))) {
    LOG(ERROR) << "Short event tail.";
    return false;
  }

  base::Time time(base::Time::FromFileTime(
      reinterpret_cast<FILETIME&>(event->Header.TimeStamp)));
  DWORD process_id = event->Header.ProcessId;
  DWORD thread_id = event->Header.ThreadId;

  switch (type) {
    case TRACE_ENTER_EVENT:
      call_trace_events_->OnTraceEntry(time, process_id, thread_id, data);
      break;

    case TRACE_EXIT_EVENT:
      call_trace_events_->OnTraceExit(time, process_id, thread_id, data);
      break;

    default:
      NOTREACHED() << "Impossible event type";
      return false;
  }

  return true;
}

bool CallTraceParser::ProcessBatchEnterEvent(EVENT_TRACE* event) {
  if (call_trace_events_ == NULL)
    return false;

  BinaryBufferReader reader(event->MofData, event->MofLength);
  const TraceBatchEnterData* data = NULL;
  if (!reader.Read(sizeof(data->num_functions), &data)) {
    LOG(ERROR) << "Short or empty batch event.";
    return false;
  }

  if (!reader.Consume(data->num_functions * sizeof(data->functions[0]))) {
    LOG(ERROR) << "Short batch event data.";
    return false;
  }

  base::Time time(base::Time::FromFileTime(
      reinterpret_cast<FILETIME&>(event->Header.TimeStamp)));
  DWORD process_id = event->Header.ProcessId;
  DWORD thread_id = event->Header.ThreadId;
  call_trace_events_->OnTraceBatchEnter(time, process_id, thread_id, data);

  return true;
}

bool CallTraceParser::ProcessOneEvent(EVENT_TRACE* event) {
  if (kCallTraceEventClass == event->Header.Guid) {
    TraceEventType type =
        static_cast<TraceEventType>(event->Header.Class.Type);
    switch (type) {
      case TRACE_ENTER_EVENT:
      case TRACE_EXIT_EVENT:
        return ProcessEntryExitEvent(event, type);

      case TRACE_BATCH_ENTER:
        return ProcessBatchEnterEvent(event);

      case TRACE_PROCESS_ATTACH_EVENT:
      case TRACE_PROCESS_DETACH_EVENT:
      case TRACE_THREAD_ATTACH_EVENT:
      case TRACE_THREAD_DETACH_EVENT:
      case TRACE_MODULE_EVENT:
        // TODO(siggi): Handle these.
        break;

      default:
        NOTREACHED() << "Unknown event type encountered.";
        break;
    }
  }

  return false;
}
