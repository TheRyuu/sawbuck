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

#include "syzygy/trace/common/service.h"

#include "base/logging.h"

namespace trace {
namespace common {

Service::Service(const base::StringPiece16& name)
    : name_(name.begin(), name.end()),
      state_(kUnused),
      owning_thread_id_(base::PlatformThread::CurrentId()) {
  DCHECK(!name.empty());
}

Service::~Service() {
}

void Service::set_instance_id(const base::StringPiece16& instance_id) {
  DCHECK_EQ(kUnused, state());
  instance_id_.assign(instance_id.begin(), instance_id.end());
}

void Service::set_started_callback(ServiceCallback callback) {
  DCHECK_EQ(kUnused, state());
  started_callback_ = callback;
}

void Service::set_interrupted_callback(ServiceCallback callback) {
  DCHECK_EQ(kUnused, state());
  interrupted_callback_ = callback;
}

void Service::set_stopped_callback(ServiceCallback callback) {
  DCHECK_EQ(kUnused, state());
  stopped_callback_ = callback;
}

Service::State Service::state() {
  base::AutoLock auto_lock(lock_);
  return state_;
}

bool Service::Start() {
  DCHECK_EQ(owning_thread_id_, base::PlatformThread::CurrentId());
  DCHECK(state() == kUnused || state() == kStopped);

  LOG(INFO) << "Starting the " << name_ << " service with instance ID \""
            << instance_id_ << "\".";

  if (!StartImpl()) {
    LOG(ERROR) << "Failed to stop " << name_ << " service with instance ID \""
            << instance_id_ << "\".";
    set_state(kErrored);
    return false;
  }

  return true;
}

bool Service::Stop() {
  // We don't use state() and set_state() so as to avoid grabbing the lock
  // twice.
  {
    base::AutoLock auto_lock(lock_);

    // No need to try to stop things again.
    if (state_ == kStopping || state_ == kStopped)
      return true;

    LOG(INFO) << "Stopping the " << name_ << " service with instance ID \""
              << instance_id_ << "\".";

    OnStateChange(state_, kStopping);
    state_ = kStopping;
  }

  if (!StopImpl()) {
    LOG(ERROR) << "Failed to stop " << name_ << " service with instance ID \""
               << instance_id_ << "\".";
    set_state(kErrored);
    return false;
  }

  return true;
}

bool Service::Join() {
  DCHECK_EQ(owning_thread_id_, base::PlatformThread::CurrentId());
  DCHECK_EQ(kRunning, state());

  LOG(INFO) << "Joining the " << name_ << " service with instance ID \""
            << instance_id_ << "\".";

  if (!JoinImpl()) {
    LOG(ERROR) << "Failed to join " << name_ << " service with instance ID \""
               << instance_id_ << "\".";
    set_state(kErrored);
    return false;
  }

  // We expect the service implementation to have transitioned us to the stopped
  // state as it finished work.
  DCHECK_EQ(kStopped, state());

  return true;
}

bool Service::OnInitialized() {
  DCHECK_EQ(kUnused, state());
  set_state(kInitialized);
  return true;
}

bool Service::OnStarted() {
  DCHECK_EQ(kInitialized, state());
  if (!started_callback_.is_null() && !started_callback_.Run(this))
    return false;
  set_state(kRunning);
  return true;
}

bool Service::OnInterrupted() {
  // A service can be interrupted from another thread, another instance of
  // Service, another process, etc. Thus, it's completely valid for a Service
  // instance to be interrupted in most any state, except stopping or stopped.
  DCHECK(state() == kUnused || state() == kInitialized || state() == kRunning);
  if (!interrupted_callback_.is_null() && !interrupted_callback_.Run(this))
    return false;
  return true;
}

bool Service::OnStopped() {
  DCHECK_EQ(kStopping, state());
  if (!started_callback_.is_null() && !stopped_callback_.Run(this))
    return false;
  set_state(kStopped);
  return true;
}

void Service::set_state(State state) {
  base::AutoLock auto_lock(lock_);
  OnStateChange(state_, state);
  state_ = state;
}

}  // namespace common
}  // namespace trace
