// Copyright 2011 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "syzygy/block_graph/transform.h"

namespace block_graph {

bool ApplyTransform(BlockGraphTransformInterface* transform,
                    BlockGraph* block_graph,
                    BlockGraph::Block* header_block) {
  DCHECK(transform != NULL);
  DCHECK(transform->name() != NULL);
  DCHECK_GT(strlen(transform->name()), 0u);
  DCHECK(block_graph != NULL);
  DCHECK(header_block != NULL);

  // Get the ID of the header block. As a sanity check we want to ensure
  // that it still exists after the transform.
  BlockGraph::BlockId header_block_id = header_block->id();

  if (!transform->Apply(block_graph, header_block)) {
    LOG(ERROR) << "Transform \"" << transform->name() << "\" failed.";
    return false;
  }

  // Ensure that the header block still exists. If it was changed, it needs
  // to have been changed in place.
  BlockGraph::Block* block = block_graph->GetBlockById(header_block_id);
  if (block == NULL) {
    LOG(ERROR) << "Header block not found after \"" << transform->name()
               << "\" transform.";
    return false;
  }
  DCHECK_EQ(header_block, block);

  return true;
}

}  // namespace block_graph
