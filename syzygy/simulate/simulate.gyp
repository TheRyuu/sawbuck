# Copyright 2012 Google Inc.
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
  'target_defaults': {
    'include_dirs': [
      '<(DEPTH)',
    ],
  },
  'targets': [
    {
      'target_name': 'simulate_lib',
      'type': 'static_library',
      'sources': [
        'simulator.cc',
        'simulator.h',
      ],
      'dependencies': [
        '<(DEPTH)/sawbuck/log_lib/log_lib.gyp:log_lib',
        '<(DEPTH)/syzygy/common/common.gyp:common_lib',
        '<(DEPTH)/syzygy/pdb/pdb.gyp:pdb_lib',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_lib',
        '<(DEPTH)/syzygy/playback/playback.gyp:playback_lib',
        '<(DEPTH)/syzygy/trace/parse/parse.gyp:parse_lib',
      ],
    },
    {
      'target_name': 'simulate',
      'type': 'executable',
      'sources': [
        'simulate_main.cc',
      ],
      'dependencies': [
        'simulate_lib',
        '<(DEPTH)/base/base.gyp:base',
      ],
    },
    {
      'target_name': 'simulate_unittests',
      'type': 'executable',
      'sources': [
        'simulate_unittests_main.cc',
        'simulate_unittest.cc',
      ],
      'dependencies': [
        'simulate_lib',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/core/core.gyp:core_unittest_utils',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_unittest_utils',
        '<(DEPTH)/syzygy/test_data/test_data.gyp:test_dll',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
      ],
    },
  ],
}
