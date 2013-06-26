# Copyright 2013 Google Inc. All Rights Reserved.
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
      'target_name': 'sampler_lib',
      'type': 'static_library',
      'sources': [
        'sampler_app.cc',
        'sampler_app.h',
      ],
      'dependencies': [
        '<(src)/base/base.gyp:base',
        '<(src)/syzygy/common/common.gyp:common_lib',
        '<(src)/syzygy/pe/pe.gyp:pe_lib',
      ],
    },
    {
      'target_name': 'sampler',
      'type': 'executable',
      'sources': [
        'sampler_main.cc',
        'sampler.rc',
      ],
      'dependencies': [
        'sampler_lib',
      ],
    },
    {
      'target_name': 'sampler_unittests',
      'type': 'executable',
      'sources': [
        'sampler_app_unittest.cc',
        'sampler_unittests_main.cc',
      ],
      'dependencies': [
        'sampler_lib',
        '<(src)/base/base.gyp:base',
        '<(src)/testing/gmock.gyp:gmock',
        '<(src)/testing/gtest.gyp:gtest',
        '<(src)/syzygy/pe/pe.gyp:pe_unittest_utils',
      ],
    },
  ],
}
