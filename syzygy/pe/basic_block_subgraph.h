// Copyright 2012 Google Inc.
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
//
// Declaration of BasicBlockGraph class.

#ifndef SYZYGY_PE_BASIC_BLOCK_SUBGRAPH_H_
#define SYZYGY_PE_BASIC_BLOCK_SUBGRAPH_H_

#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/string_piece.h"
#include "syzygy/block_graph/basic_block.h"
#include "syzygy/block_graph/block_graph.h"

namespace pe {

// A basic-block sub-graph describes the make-up and layout of one or
// more blocks as a set of code, data, and/or padding basic-blocks. Optionally,
// it holds a pointer to a block from which the sub-graph was originally
// derived.
//
// In manipulating the basic block sub-graph, note that the sub-graph
// acts as a basic-block factory and retains ownership of all basic-blocks
// that participate in the composition.
class BasicBlockSubGraph {
 public:
  typedef block_graph::BasicBlock BasicBlock;
  typedef BasicBlock::BasicBlockType BasicBlockType;
  typedef std::list<BasicBlock*> BasicBlockOrdering;
  typedef block_graph::BlockGraph BlockGraph;
  typedef BlockGraph::Block Block;
  typedef BlockGraph::BlockType BlockType;
  typedef BlockGraph::Offset Offset;
  typedef BlockGraph::SectionId SectionId;
  typedef BlockGraph::Size Size;
  typedef BlockGraph::BlockAttributes BlockAttributes;

  // A structure describing a block (its properties, attributes, and
  // constituent ordered basic-blocks). A given basic-block may participate
  // in at most one BlockDescription at any time.
  struct BlockDescription {
    BlockType type;
    Size alignment;
    std::string name;
    SectionId section;
    BlockAttributes attributes;
    BasicBlockOrdering basic_block_order;
  };

  typedef std::list<BlockDescription> BlockDescriptionList;
  typedef std::map<BasicBlock::BlockId, BasicBlock> BBCollection;
  typedef core::AddressSpace<Offset, size_t, BasicBlock*> BBAddressSpace;

  // Initialize a basic block sub-graph.
  BasicBlockSubGraph();

  // @name Accessors.
  // @{
  void set_original_block(const Block* block) { original_block_ = block; }
  const Block* original_block() { return original_block_; }
  BBCollection& basic_blocks() { return  basic_blocks_; }
  BBAddressSpace& original_address_space() { return original_address_space_; }
  BlockDescriptionList& block_descriptions() { return block_descriptions_; }
  // @}

  // Add a new basic block to the sub-graph.
  // @param name A textual identifier for this basic block.
  // @param type The disposition (code, data, padding) of this basic block.
  // @param offset The offset (in the original block) where this basic block
  //     originated. Set to kEphemeralSourceOffset to indicate that this is a
  //     generated basic block.
  // @param size The number of bytes this basic block occupied in the original
  //     block. Set to 0 if this is a generated basic block.
  // @param data The underlying data representing the basic block.
  // @returns A pointer to a newly allocated basic block representing the
  //     original source range [@p offset, @p offset + @p size), or NULL on
  //     ERROR. Ownership of the returned basic-block (if any) is retained
  //     by the composition.
  BasicBlock* AddBasicBlock(const base::StringPiece& name,
                            BasicBlockType type,
                            Offset offset,
                            Size size,
                            const uint8* data);

  // Returns true if the basic-block composition is valid. This tests the
  // for following conditions.
  // 1. Each basic-block is used in at most one BlockDescription.
  // 2. Each code basic-block has valid successors.
  // 3. If there is an original block, then each of it's referrers is accounted
  //    for in the new composition.
  bool IsValid() const;

 protected:
  // @name Validation Functions.
  // @{
  bool MapsBasicBlocksToAtMostOneDescription() const;
  bool HasValidSuccessors() const;
  bool HasValidReferrers() const;
  // @}

  // The original block corresponding from which this sub-graph derives. This
  // is optional, and may be NULL.
  const Block* original_block_;

  // The set of basic blocks in this sub-graph. This includes any basic-blocks
  // created during the initial decomposition process, as well as any additional
  // basic-blocks synthesized thereafter.
  BBCollection basic_blocks_;

  // The breakdown and layout of the original block into basic blocks. This
  // may reference only a subset of the blocks in basic_blocks_;
  BBAddressSpace original_address_space_;

  // A list of block descriptions for the blocks that are to be created from
  // this basic block sub-graph.
  BlockDescriptionList block_descriptions_;

  // An counter used to assign IDs to basic blocks as they are constructed.
  int next_basic_block_id_;
};

}  // namespace pe

#endif  // SYZYGY_PE_BASIC_BLOCK_SUBGRAPH_H_
