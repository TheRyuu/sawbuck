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
#include "syzygy/reorder/linear_order_generator.h"

#include <algorithm>

namespace reorder {

namespace {

// This is effectively arbitrarily chosen for now. Higher is better from
// benchmarked reorderings. This value is effectively 'infinite' as far as the
// chrome data goes.
const size_t kDataRecursionDepth = 100;

typedef LinearOrderGenerator::BlockCall BlockCall;

// Comparator for sorting BlockCalls by increasing time.
struct BlockCallSortIncrTime {
  bool operator()(const BlockCall& bc1, const BlockCall& bc2) {
    return bc1.time < bc2.time;
  }
};

// Used for aggregating block call information across multiple runs.
struct AverageBlockCall {
  const BlockGraph::Block* block;
  size_t sum_order;
  // The number of runs of the instrumented binary in which this block was
  // seen, NOT the number of times it was seen called in aggregate.
  size_t call_count;
  // This is only meaningful if call_count == 1.
  uint32_t process_group_id;

  double AverageOrder() const {
    DCHECK(call_count > 0);
    return ((double)sum_order) / call_count;
  }
};

// Sorts by decreasing call count. Anything with more than one call count
// is sorted with a secondary key of increasing order. Anything with a single
// call count has a secondary key of process_group_id, and a tertiary key of
// increasing order.
struct AverageBlockCallSort {
  bool operator()(const AverageBlockCall& abc1, const AverageBlockCall& abc2) {
    if (abc1.call_count != abc2.call_count)
      return abc1.call_count > abc2.call_count;

    if (abc1.call_count > 1 || abc1.process_group_id == abc2.process_group_id)
      return abc1.AverageOrder() < abc2.AverageOrder();

    return abc1.process_group_id < abc2.process_group_id;
  }
};

// Extract the values of a map to a vector.
template<typename K, typename V> void MapToVector(const std::map<K, V> map,
                                                  std::vector<V>* vector) {
  DCHECK(vector != NULL);

  vector->clear();
  std::map<K, V>::const_iterator it = map.begin();
  for (; it != map.end(); ++it)
    vector->push_back(it->second);
}

}  // namespace

LinearOrderGenerator::LinearOrderGenerator()
    : Reorderer::OrderGenerator("Linear Order Generator"),
      active_process_count_(0),
      next_process_group_id_(0) {
}

LinearOrderGenerator::~LinearOrderGenerator() {
}

bool LinearOrderGenerator::OnProcessStarted(const Reorderer& reorderer,
                                            uint32 process_id,
                                            const UniqueTime& time) {
  if (active_process_count_ == 0) {
    if (!CloseProcessGroup())
      return false;
  }

  ++active_process_count_;

  return true;
}

bool LinearOrderGenerator::OnProcessEnded(const Reorderer& reorderer,
                                          uint32 process_id,
                                          const UniqueTime& time) {
  DCHECK_LT(0U, active_process_count_);
  --active_process_count_;
  return true;
}

bool LinearOrderGenerator::OnCodeBlockEntry(const Reorderer& reorderer,
                                            const BlockGraph::Block* block,
                                            RelativeAddress address,
                                            uint32 process_id,
                                            uint32 thread_id,
                                            const UniqueTime& time) {
  if (!reorderer.MustReorder(block))
    return true;

  return TouchBlock(BlockCall(block, process_id, thread_id, time));
}

bool LinearOrderGenerator::CalculateReordering(const Reorderer& reorderer,
                                               Order* order) {
  DCHECK(order != NULL);

  // Ensure the last running process group is closed.
  if (!CloseProcessGroup())
    return false;

  LOG(INFO) << "Encountered " << process_group_calls_.size()
      << " process groups.";

  // Aggregate the block calls.
  std::map<const BlockGraph::Block*, AverageBlockCall> average_block_call_map;
  ProcessGroupBlockCalls::const_iterator it = process_group_calls_.begin();
  for (; it != process_group_calls_.end(); ++it) {
    for (size_t i = 0; i < it->second.size(); ++i) {
      const BlockCall& block_call = it->second[i];
      AverageBlockCall& average_block_call =
          average_block_call_map[block_call.block];
      average_block_call.block = block_call.block;
      average_block_call.sum_order += i;
      ++average_block_call.call_count;
      average_block_call.process_group_id = it->first;
    }
  }

  // Now create a sorted list.
  std::vector<AverageBlockCall> average_block_calls;
  MapToVector(average_block_call_map, &average_block_calls);
  average_block_call_map.clear();
  std::sort(average_block_calls.begin(), average_block_calls.end(),
            AverageBlockCallSort());

  // TODO(chrisha): Create an option to evenly distribute the common startup
  //     blocks (those with call_count == process_group_calls_.size()) among
  //     the remaining blocks.

  // Create the ordering from this list.
  for (size_t i = 0; i < average_block_calls.size(); ++i) {
    const BlockGraph::Block* code_block = average_block_calls[i].block;

    if (reorderer.MustReorder(code_block))
      order->section_block_lists[code_block->section()].push_back(code_block);

    // Create an anologous data ordering if we were asked to.
    if (reorderer.flags() & Reorderer::kFlagReorderData) {
      if (!InsertDataBlocks(kDataRecursionDepth, reorderer, code_block, order))
        return false;
    }
  }

  return true;
}

bool LinearOrderGenerator::TouchBlock(const BlockCall& block_call) {
  DCHECK(block_call.block != NULL);
  // All code blocks should belong to a defined section.
  DCHECK_NE(pe::kInvalidSection, block_call.block->section());

  // Store the block along with the earliest time it was called.
  BlockCallMap::iterator it = block_call_map_.find(block_call.block);
  if (it == block_call_map_.end()) {
    std::pair<BlockCallMap::iterator, bool> insert_return;
    insert_return = block_call_map_.insert(
        std::make_pair(block_call.block, block_call));
    it = insert_return.first;
    DCHECK(insert_return.second);
    DCHECK(it != block_call_map_.end());
  } else {
    // Keep around the earliest call to this block only.
    if (block_call.time < it->second.time)
      it->second = block_call;
  }
  return true;
}

bool LinearOrderGenerator::InsertDataBlocks(size_t max_recursion_depth,
                                            const Reorderer& reorderer,
                                            const BlockGraph::Block* block,
                                            Order* order) {
  DCHECK(block != NULL);
  DCHECK(order != NULL);

  // Stop the recursion.
  if (max_recursion_depth == 0)
    return true;

  std::vector<const BlockGraph::Block*> data_blocks;

  // Iterate through any data blocks that are referenced by this
  // block, and also store them with the same time. This is a pessimistic
  // optimization, and assumes that all data linked to a code block will
  // be touched by that code block (and all data linked to by that data block,
  // and so on, up to 'max_recursion_depth').
  BlockGraph::Block::ReferenceMap::const_iterator ref_it =
      block->references().begin();
  for (; ref_it != block->references().end(); ++ref_it) {
    const BlockGraph::Block* ref = ref_it->second.referenced();
    DCHECK(ref != NULL);
    // We only touch data blocks with a valid section id.
    if (ref->type() != BlockGraph::DATA_BLOCK ||
        ref->section() == pe::kInvalidSection)
      continue;

    // Only insert data blocks that have not yet been seen.
    if (!data_block_set_.insert(ref).second)
      continue;

    // Finally, insert this block to the appropriate section ordering, but
    // only if we're reordering that section.
    if (reorderer.MustReorder(ref))
      order->section_block_lists[ref->section()].push_back(ref);

    data_blocks.push_back(ref);
  }

  // Recurse on the data blocks we just added.
  if (max_recursion_depth > 1) {
    for (size_t i = 0; i < data_blocks.size(); ++i) {
      if (!InsertDataBlocks(max_recursion_depth - 1, reorderer,
                            data_blocks[i], order))
        return false;
    }
  }

  return true;
}

bool LinearOrderGenerator::CloseProcessGroup() {
  if (block_call_map_.size() == 0)
    return true;

  BlockCalls& block_calls = process_group_calls_[next_process_group_id_];
  ++next_process_group_id_;

  MapToVector(block_call_map_, &block_calls);
  block_call_map_.clear();

  std::sort(block_calls.begin(), block_calls.end(), BlockCallSortIncrTime());

  return true;
}

}  // namespace reorder
