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

# Unittests should be added to this file so that they are discovered by
# the unittest infrastructure. Each unit-test should be a target of a
# dependency, and should correspond to an executable that will be created
# in the output directory. For example:
#
#   '<(DEPTH)/syzygy/pdb/pdb.gyp:pdb_unittests',
#
# The target of this dependency rule is 'pdb_unittests', and it
# corresponds to the executable '<project_dir>/Debug/pdb_unittests.exe'.
# (Or 'Release' instead of 'Debug', as the case may be.)
{
  'variables': {
    'unittests': [
      '<(DEPTH)/syzygy/agent/common/common.gyp:agent_common_unittests',
      '<(DEPTH)/syzygy/agent/profiler/profiler.gyp:profile_unittests',
      '<(DEPTH)/syzygy/block_graph/block_graph.gyp:block_graph_unittests',
      '<(DEPTH)/syzygy/common/common.gyp:common_unittests',
      '<(DEPTH)/syzygy/core/core.gyp:core_unittests',
      '<(DEPTH)/syzygy/trace/parse/parse.gyp:parse_unittests',
      '<(DEPTH)/syzygy/trace/service/service.gyp:rpc_service_unittests',
      '<(DEPTH)/syzygy/instrument/instrument.gyp:instrument_unittests',
      '<(DEPTH)/syzygy/pdb/pdb.gyp:pdb_unittests',
      '<(DEPTH)/syzygy/pe/pe.gyp:pe_unittests',
      '<(DEPTH)/syzygy/pe/transforms/pe_transforms.gyp:pe_transforms_unittests',
      '<(DEPTH)/syzygy/relink/relink.gyp:relink_unittests',
      '<(DEPTH)/syzygy/reorder/reorder.gyp:reorder_unittests',
      '<(DEPTH)/syzygy/wsdump/wsdump.gyp:wsdump_unittests',
    ],
  }
}
