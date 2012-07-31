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
      'target_name': 'coverage_lib',
      'type': 'static_library',
      'sources': [
        'coverage_constants.cc',
        'coverage_constants.h',
        'coverage_data.h',
        'coverage_transform.cc',
        'coverage_transform.h',
      ],
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/block_graph/block_graph.gyp:block_graph_lib',
        '<(DEPTH)/syzygy/block_graph/transforms/block_graph_transforms.gyp:'
            'block_graph_transforms_lib',
        '<(DEPTH)/syzygy/common/common.gyp:common_lib',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_lib',
        '<(DEPTH)/syzygy/pe/transforms/pe_transforms.gyp:pe_transforms_lib',
      ],
    },
    {
      'target_name': 'coverage_unittests',
      'type': 'executable',
      'sources': [
        'coverage_transform_unittest.cc',
        'coverage_unittests_main.cc',
      ],
      'dependencies': [
        'coverage_lib',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/core/core.gyp:core_unittest_utils',
        '<(DEPTH)/syzygy/agent/common/common.gyp:agent_common_lib',
        '<(DEPTH)/syzygy/trace/parse/parse.gyp:parse_lib',
        '<(DEPTH)/syzygy/trace/parse/parse.gyp:parse_unittest_utils',
        '<(DEPTH)/syzygy/trace/common/common.gyp:trace_unittest_utils',
        '<(DEPTH)/syzygy/trace/service/service.gyp:call_trace_service_exe',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_lib',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_unittest_utils',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        '<(DEPTH)/testing/gmock.gyp:gmock',
       ],
    },
  ],
}
