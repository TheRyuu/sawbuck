# Copyright 2012 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
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
      'target_name': 'asan_rtl_lib',
      'type': 'static_library',
      'sources': [
        'asan_flags.cc',
        'asan_flags.h',
        'asan_heap.cc',
        'asan_heap.h',
        'asan_logger.cc',
        'asan_logger.h',
        'asan_shadow.cc',
        'asan_shadow.h',
      ],
      'dependencies': [
        '<(DEPTH)/syzygy/trace/rpc/rpc.gyp:logger_rpc_lib',
        '<(DEPTH)/syzygy/trace/protocol/protocol.gyp:protocol_lib',
      ],
    },
    {
      'target_name': 'asan_rtl',
      'type': 'shared_library',
      'sources': [
        'asan_rtl.cc',
        'asan_rtl.def',
        'asan_rtl.rc',
      ],
      'dependencies': [
        'asan_rtl_lib',
        '<(DEPTH)/sawbuck/log_lib/log_lib.gyp:log_lib',
        '<(DEPTH)/syzygy/agent/common/common.gyp:agent_common_lib',
        '<(DEPTH)/syzygy/common/common.gyp:common_lib',
        '<(DEPTH)/syzygy/common/common.gyp:syzygy_version',
        '<(DEPTH)/syzygy/core/core.gyp:core_lib',
      ],
    },
    {
      'target_name': 'asan_rtl_unittests',
      'type': 'executable',
      'sources': [
        'asan_flags_unittest.cc',
        'asan_heap_unittest.cc',
        'asan_logger_unittest.cc',
        'asan_rtl_unittest.cc',
        'asan_rtl_unittests_main.cc',
        'asan_shadow_unittest.cc',
      ],
      'dependencies': [
        'asan_rtl',
        'asan_rtl_lib',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/agent/common/common.gyp:agent_common_lib',
        '<(DEPTH)/syzygy/core/core.gyp:core_unittest_utils',
        '<(DEPTH)/syzygy/trace/logger/logger.gyp:logger_lib',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
       ],
    },
  ],
}
