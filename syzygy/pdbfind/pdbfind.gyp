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
  'targets': [
    {
      'target_name': 'pdbfind_lib',
      'type': 'static_library',
      'sources': [
        'pdbfind_app.cc',
        'pdbfind_app.h',
      ],
      'dependencies': [
        '<(src)/base/base.gyp:base',
        '<(src)/syzygy/pe/pe.gyp:pe_lib',
      ],
    },
    {
      'target_name': 'pdbfind',
      'type': 'executable',
      'sources': [
        'pdbfind_main.cc',
        'pdbfind.rc',
      ],
      'dependencies': [
        'pdbfind_lib',
      ],
      'run_as': {
        'action': [
          '$(TargetPath)',
          '$(OutDir)\\test_dll.dll',
        ]
      },
    },
    {
      'target_name': 'pdbfind_unittests',
      'type': 'executable',
      'sources': [
        'pdbfind_app_unittest.cc',
        'pdbfind_unittests_main.cc',
      ],
      'dependencies': [
        'pdbfind_lib',
        '<(src)/base/base.gyp:base',
        '<(src)/testing/gmock.gyp:gmock',
        '<(src)/testing/gtest.gyp:gtest',
        '<(src)/syzygy/core/core.gyp:core_unittest_utils',
        '<(src)/syzygy/pe/pe.gyp:pe_unittest_utils',
        '<(src)/syzygy/pe/pe.gyp:test_dll',
      ],
    },
  ],
}
