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
  'targets': [
    {
      'target_name': 'code_tally',
      'type': 'executable',
      'sources': [
        'code_tally.cc',
        'code_tally.h',
        'code_tally_app.cc',
        'code_tally_app.h',
        'code_tally_main.cc',
      ],
      'working_directory': '$(ConfigurationDir)',
      'action': [
        '$(TargetPath)',
        '$(OutputPath)test_dll.dll.pdb',
      ],
      'dependencies': [
        '<(src)/base/base.gyp:base',
        '<(src)/sawbuck/common/common.gyp:common',
        '<(src)/syzygy/core/core.gyp:core_lib',
        '<(src)/syzygy/pe/pe.gyp:dia_sdk',
        '<(src)/syzygy/pe/pe.gyp:pe_lib',
        '<(src)/syzygy/pe/pe.gyp:test_dll',
      ],
    },
  ]
}
