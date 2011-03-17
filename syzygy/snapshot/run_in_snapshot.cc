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
// A command line utility that'll create snapshot of a given volume, and map it
// to a drive letter, then run a command while the snapshot is mounted. This
// is handy to simulate cold-start conditions, as a newly created and mounted
// snapshot will be as cold as cold gets.
#include <iostream>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <atlbase.h>
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/process_util.h"
#include "base/utf_string_conversions.h"

const char kHelp[] =
  "Available options:\n"
  "  --volume=<volume> the volume to mount, e.g. C:\\\n"
  "  --snapshot=<drive letter> the drive letter to mount the snapshot on, "
      "e.g. M:\n"
  "  --cmd=<command line> the command to run, e.g. "
      "\"notepad M:\\temp\\foo.txt\"\n";

int Usage() {
  CommandLine* cmd_line = CommandLine::ForCurrentProcess();

  std::wcout << L"Usage: " << cmd_line->GetProgram().BaseName().value().c_str()
      << " [options]\n" << std::endl;
  std::cout << kHelp;

  return 1;
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  CommandLine::Init(argc, argv);

  CommandLine* cmd_line = CommandLine::ForCurrentProcess();

  std::wstring volume = cmd_line->GetSwitchValueNative("volume");
  std::wstring snapshot = cmd_line->GetSwitchValueNative("snapshot");
  std::wstring cmd = cmd_line->GetSwitchValueNative("cmd");
  if (volume.empty() || snapshot.empty() || cmd.empty()) {
    return Usage();
  }

  // Initialize COM and open ourselves wide for callbacks by
  // CoInitializeSecurity.
  HRESULT hr = ::CoInitialize(NULL);
  if (SUCCEEDED(hr)) {
    hr = ::CoInitializeSecurity(
        NULL,  //  Allow *all* VSS writers to communicate back!
        -1,  //  Default COM authentication service
        NULL,  //  Default COM authorization service
        NULL,  //  reserved parameter
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY,  //  Strongest COM authentication level
        RPC_C_IMP_LEVEL_IDENTIFY,  //  Minimal impersonation abilities
        NULL,  //  Default COM authentication settings
        EOAC_NONE,  //  No special options
        NULL);  //  Reserved parameter
  }

  DCHECK(SUCCEEDED(hr));
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to initialize COM";
    return 1;
  }

  CComPtr<IVssBackupComponents> comp;
  hr = ::CreateVssBackupComponents(&comp);
  if (SUCCEEDED(hr))
    hr = comp->InitializeForBackup(NULL);
  if (SUCCEEDED(hr))
    hr = comp->SetBackupState(true, true, VSS_BT_COPY, false);

  DCHECK(SUCCEEDED(hr));
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to initialize snapshot, error " << hr;
    return 1;
  }

  CComPtr<IVssAsync> async;
  hr = comp->GatherWriterMetadata(&async);
  if (SUCCEEDED(hr))
    hr = async->Wait();
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to gather write data, error " << hr;
    return 1;
  }

  VSS_ID id = {};
  hr = comp->StartSnapshotSet(&id);

  VSS_ID dummy = {};
  if (SUCCEEDED(hr)) {
    hr = comp->AddToSnapshotSet(const_cast<LPWSTR>(volume.c_str()),
                                GUID_NULL,
                                &dummy);
  }

  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to start snapshot, error " << hr;
    return 1;
  }

  async.Release();
  hr = comp->PrepareForBackup(&async);
  if (SUCCEEDED(hr))
    hr = async->Wait();

  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to prepare for backup, error " << hr;
    return 1;
  }

  async.Release();
  hr = comp->DoSnapshotSet(&async);
  if (SUCCEEDED(hr))
    hr = async->Wait();

  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to do snapshot, error " << hr;
    return 1;
  }

  CComPtr<IVssEnumObject> enum_snapshots;
  hr = comp->Query(GUID_NULL,
                   VSS_OBJECT_NONE,
                   VSS_OBJECT_SNAPSHOT,
                   &enum_snapshots);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to query snapshot, error " << hr;
    return 1;
  }

  VSS_OBJECT_PROP prop;
  ULONG fetched = 0;
  hr = enum_snapshots->Next(1, &prop, &fetched);
  if (FAILED(hr) || hr == S_FALSE) {
    LOG(ERROR) << "Failed to retrieve snapshot volume, error " << hr;
    return 1;
  }

  // Bind the snapshot to a drive letter.
  BOOL defined = ::DefineDosDevice(0,
                                   snapshot.c_str(),
                                   prop.Obj.Snap.m_pwszSnapshotDeviceObject);
  if (!defined) {
    LOG(ERROR) << "Failed to assign a drive letter to snapshot";
    return 1;
  }
  ::VssFreeSnapshotProperties(&prop.Obj.Snap);

  int ret = 0;
  if (!base::LaunchApp(CommandLine::FromString(cmd), true, false, NULL)) {
    LOG(ERROR) << "Unable to launch application";
    ret = 1;
  }

  // Remove the drive mapping.
  ::DefineDosDevice(DDD_REMOVE_DEFINITION, snapshot.c_str(), NULL);

  return ret;
}
