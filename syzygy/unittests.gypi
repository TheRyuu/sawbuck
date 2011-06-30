# Copyright 2011 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Unittests should be added to this file so that they are discovered by
# the unittest infrastructure. Each unit-test should be a target of a
# dependency, and should correspond to an executable that will be created
# in the output directory. For example:
#
#   '<(DEPTH)/syzygy/call_trace/call_trace.gyp:call_trace_unittests'
#
# The target of this dependency rule is 'call_trace_unittests', and it
# corresponds to the executable '<project_dir>/Debug/call_trace_unittests.exe'.
# (Or 'Release' instead of 'Debug', as the case may be.)
{
  'variables': {
    'unittests': [
        '<(DEPTH)/syzygy/call_trace/call_trace.gyp:call_trace_unittests',
        '<(DEPTH)/syzygy/common/common.gyp:common_unittests',
        '<(DEPTH)/syzygy/core/core.gyp:core_unittests',
        '<(DEPTH)/syzygy/instrument/instrument.gyp:instrument_unittests',
        '<(DEPTH)/syzygy/pdb/pdb.gyp:pdb_unittests',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_unittests',
        '<(DEPTH)/syzygy/relink/relink.gyp:relink_unittests',
    ],
  }
}
