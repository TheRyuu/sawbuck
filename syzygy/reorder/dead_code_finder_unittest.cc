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

#include "syzygy/reorder/dead_code_finder.h"

#include "gtest/gtest.h"
#include "syzygy/block_graph/block_graph.h"
#include "syzygy/core/address.h"
#include "syzygy/core/random_number_generator.h"
#include "syzygy/reorder/order_generator_test.h"

namespace reorder {

class DeadCodeFinderTest : public testing::OrderGeneratorTest {
 protected:
  typedef std::set<const block_graph::BlockGraph::Block*> BlockSet;
  typedef BlockSet::iterator BlockIter;

  DeadCodeFinderTest() : random_(12345) {
  }

  DeadCodeFinder dead_code_finder_;
  BlockSet live_blocks_;
  BlockSet dead_blocks_;
  core::RandomNumberGenerator random_;
};

TEST_F(DeadCodeFinderTest, TestDLL) {
  const size_t kNumBlocks = 20;

  // Get the .text code section.
  size_t section_index = input_dll_.GetSectionIndex(".text");
  const IMAGE_SECTION_HEADER* section =
      input_dll_.section_header(section_index);
  ASSERT_TRUE(section != NULL);

  // Get a bunch of random blocks to visit (and consider live).
  ASSERT_TRUE(live_blocks_.size() == 0);
  while (live_blocks_.size() < kNumBlocks) {
    core::RelativeAddress addr(
        section->VirtualAddress + random_(section->Misc.VirtualSize));
    const block_graph::BlockGraph::Block* block =
        image_layout_.blocks.GetBlockByAddress(addr);
    ASSERT_TRUE(block->addr() <= addr);
    ASSERT_TRUE(addr <= block->addr() + block->size());
    live_blocks_.insert(block);
  }

  // Get a bunch of random blocks to NOT visit (and consider dead).
  ASSERT_TRUE(dead_blocks_.size() == 0);
  while (dead_blocks_.size() < kNumBlocks) {
    core::RelativeAddress addr(
        section->VirtualAddress + random_(section->Misc.VirtualSize));
    const block_graph::BlockGraph::Block* block =
        image_layout_.blocks.GetBlockByAddress(addr);
    if (live_blocks_.find(block) == live_blocks_.end())
      dead_blocks_.insert(block);
  }

  // Generate calls to the live blocks.
  dead_code_finder_.OnProcessStarted(1, GetSystemTime());
  for (BlockIter it = live_blocks_.begin();  it != live_blocks_.end(); ++it) {
    dead_code_finder_.OnCodeBlockEntry(
        *it, (*it)->addr(), 1, 1, GetSystemTime());
  }
  dead_code_finder_.OnProcessEnded(1, GetSystemTime());

  // Do the reordering.
  EXPECT_TRUE(dead_code_finder_.CalculateReordering(input_dll_,
                                                    image_layout_,
                                                    true,
                                                    false,
                                                    &order_));

  ExpectNoDuplicateBlocks();

  // Check the live blocks.
  for (BlockIter it = live_blocks_.begin();  it != live_blocks_.end(); ++it) {
    ASSERT_FALSE(dead_code_finder_.IsDead(*it));
  }

  // Check the dead blocks.
  for (BlockIter it = dead_blocks_.begin();  it != dead_blocks_.end(); ++it) {
    ASSERT_TRUE(dead_code_finder_.IsDead(*it));
  }

  // Check the ordering.
  ASSERT_TRUE(order_.section_block_lists.size() == 1);
  ASSERT_TRUE(order_.section_block_lists[0].size() >= dead_blocks_.size());
  typedef Reorderer::Order::BlockList BlockList;
  BlockList::iterator iter = order_.section_block_lists[0].begin();
  for (; iter != order_.section_block_lists[0].end(); ++iter) {
    ASSERT_TRUE(dead_code_finder_.IsDead(*iter));
    ASSERT_TRUE(live_blocks_.find(*iter) == live_blocks_.end());
  }
}

}  // namespace reorder
