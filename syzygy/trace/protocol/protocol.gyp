# Copyright 2012 Google Inc. All Rights Reserved.
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

{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'protocol_lib',
      'type': 'static_library',
      'sources': [
        'call_trace_defs.cc',
        'call_trace_defs.h',
      ],
      'dependencies': [
        '<(src)/base/base.gyp:base',
      ],
    },
    {
      'target_name': 'protocol_unittests',
      'type': 'executable',
      'sources': [
        'call_trace_defs_unittest.cc',
        'protocol_unittests_main.cc',
      ],
      'dependencies': [
        'protocol_lib',
        '<(src)/testing/gtest.gyp:gtest',
      ],
    },
  ],
}
