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
  'includes': [
    'unittests.gypi',
  ],
  'targets': [
    {
      'target_name': 'build_all',
      'type': 'none',
      'dependencies': [
        'agent/asan/asan.gyp:*',
        'agent/call_trace/call_trace.gyp:*',
        'agent/common/common.gyp:*',
        'agent/coverage/coverage.gyp:*',
        'agent/profiler/profiler.gyp:*',
        'block_graph/block_graph.gyp:*',
        'block_graph/transforms/block_graph_transforms.gyp:*',
        'block_graph/orderers/block_graph_orderers.gyp:*',
        'common/common.gyp:*',
        'core/core.gyp:*',
        'experimental/experimental.gyp:*',
        'grinder/grinder.gyp:*',
        'instrument/instrument.gyp:*',
        'installer/installer.gyp:*',
        'pdb/pdb.gyp:*',
        'pe/pe.gyp:*',
        'pe/orderers/pe_orderers.gyp:*',
        'pe/transforms/pe_transforms.gyp:*',
        'playback/playback.gyp:*',
        'py/py.gyp:*',
        'py/etw_db/etw_db.gyp:*',
        'relink/relink.gyp:*',
        'reorder/reorder.gyp:*',
        'scripts/scripts.gyp:*',
        'scripts/benchmark/benchmark.gyp:*',
        'scripts/graph/graph.gyp:*',
        'simulate/simulate.gyp:*',
        'snapshot/snapshot.gyp:*',
        'test_data/test_data.gyp:*',
        'trace/client/client.gyp:*',
        'trace/etw_control/etw_control.gyp:*',
        'trace/parse/parse.gyp:*',
        'trace/protocol/protocol.gyp:*',
        'trace/rpc/rpc.gyp:*',
        'trace/service/service.gyp:*',
        'wsdump/wsdump.gyp:*',
      ],
    },
    {
      'target_name': 'build_docs',
      'type': 'none',
      'sources': [
        'build/doxyfile',
        'build/run_doxygen.py',
      ],
      'actions': [
        {
          'action_name': 'Run Doxygen',
          'msvs_cygwin_shell': 0,
          'inputs': [],
          'outputs': [
             'THIS_OUTPUT_IS_NEVER_GENERATED.TXT',
          ],
          'action': [
            'python',
            'build/run_doxygen.py',
          ],
        },
      ],
    },
    {
      # New unittests should be added to unittests.gypi.
      'target_name': 'build_unittests',
      'type': 'none',
      'dependencies': [
        '<@(unittests)',
      ],
    },
  ],
}
