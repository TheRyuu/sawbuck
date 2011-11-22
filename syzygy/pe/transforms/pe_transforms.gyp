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
      'target_name': 'pe_transforms_lib',
      'type': 'static_library',
      'sources': [
        'add_metadata_transform.cc',
        'add_metadata_transform.h',
      ],
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/block_graph/block_graph.gyp:block_graph_lib',
        '<(DEPTH)/syzygy/common/common.gyp:common_lib',
        '<(DEPTH)/syzygy/core/core.gyp:core_lib',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_lib',
      ],
    },
    {
      'target_name': 'pe_transforms_unittests',
      'type': 'executable',
      'sources': [
        'add_metadata_transform_unittest.cc',
        'pe_transforms_unittests_main.cc',
      ],
      'dependencies': [
        'pe_transforms_lib',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_unittest_utils',
        '<(DEPTH)/syzygy/pe/pe.gyp:test_dll',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
      ],
    },
  ],
}
