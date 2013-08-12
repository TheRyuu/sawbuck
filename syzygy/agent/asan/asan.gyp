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
  'targets': [
    {
      'target_name': 'asan_rtl_lib',
      'type': 'static_library',
      'sources': [
        'asan_heap.cc',
        'asan_heap.h',
        'asan_logger.cc',
        'asan_logger.h',
        'asan_rtl_impl.cc',
        'asan_rtl_impl.h',
        'asan_runtime.cc',
        'asan_runtime.h',
        'asan_shadow.cc',
        'asan_shadow.h',
        'stack_capture.cc',
        'stack_capture.h',
        'stack_capture_cache.cc',
        'stack_capture_cache.h',
      ],
      'dependencies': [
        '<(src)/syzygy/trace/common/common.gyp:trace_common_lib',
        '<(src)/syzygy/trace/rpc/rpc.gyp:logger_rpc_lib',
        '<(src)/syzygy/trace/protocol/protocol.gyp:protocol_lib',
      ],
    },
    {
      'target_name': 'asan_rtl',
      'type': 'loadable_module',
      'sources': [
        'asan_rtl.cc',
        'asan_rtl.def',
        'asan_rtl.rc',
      ],
      'dependencies': [
        'asan_rtl_lib',
        '<(src)/sawbuck/log_lib/log_lib.gyp:log_lib',
        '<(src)/syzygy/agent/common/common.gyp:agent_common_lib',
        '<(src)/syzygy/common/common.gyp:common_lib',
        '<(src)/syzygy/common/common.gyp:syzygy_version',
        '<(src)/syzygy/core/core.gyp:core_lib',
      ],
      'configurations': {
        # Override the default imports list at source - apparently the
        # configuration inheritance hierarchy is traversed and settings merged
        # for each target. It's not sufficient here to e.g. override the
        # desired settings in the final, assembled configuration such as
        # 'Debug' or 'Release', as that will only alter their contribution to
        # the project.
        # Note that this is brittle to changes in build/common.gypi.
        'Common_Base': {
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalDependencies=': [],
            },
          },
        },
      },
      'msvs_settings': {
        'VCLinkerTool': {
          # Link against the XP-constrained user32 import libraries for
          # kernel32 and user32 of the platform-SDK provided one to avoid
          # inadvertently taking dependencies on post-XP user32 exports.
          'IgnoreDefaultLibraryNames': [
            'user32.lib',
            'kernel32.lib',
          ],
          'AdditionalDependencies=': [
            # Custom import libs.
            'user32.winxp.lib',
            'kernel32.winxp.lib',

            # SDK import libs.
            'dbghelp.lib',
            'psapi.lib',
            'rpcrt4.lib',
          ],
          'AdditionalLibraryDirectories': [
            '<(src)/build/win/importlibs/x86',
            '<(src)/syzygy/build/importlibs/x86',
          ],
          # This module should delay load nothing.
          'DelayLoadDLLs=': [
          ],
        },
      },
    },
    {
      'target_name': 'asan_rtl_unittests',
      'type': 'executable',
      'sources': [
        'asan_heap_unittest.cc',
        'asan_logger_unittest.cc',
        'asan_runtime_unittest.cc',
        'asan_rtl_impl_unittest.cc',
        'asan_rtl_unittest.cc',
        'asan_rtl_unittests_main.cc',
        'asan_shadow_unittest.cc',
        'stack_capture_unittest.cc',
        'stack_capture_cache_unittest.cc',
        'unittest_util.cc',
        'unittest_util.h',
      ],
      'dependencies': [
        'asan_rtl',
        'asan_rtl_lib',
        '<(src)/base/base.gyp:base',
        '<(src)/syzygy/agent/common/common.gyp:agent_common_lib',
        '<(src)/syzygy/core/core.gyp:core_unittest_utils',
        '<(src)/syzygy/trace/agent_logger/agent_logger.gyp:agent_logger_lib',
        '<(src)/testing/gmock.gyp:gmock',
        '<(src)/testing/gtest.gyp:gtest',
       ],
      'msvs_settings': {
        'VCLinkerTool': {
          # Disable support for large address spaces.
          'LargeAddressAware': 1,
        },
      },
    },
  ],
}
