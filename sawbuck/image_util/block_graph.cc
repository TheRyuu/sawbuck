// Copyright 2010 Google Inc.
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
#include "sawbuck/image_util/block_graph.h"
#include "base/logging.h"

namespace image_util {

const RelativeAddress kInvalidAddress(0xFFFFFFFF);
const size_t kInvalidSegment = -1;

BlockGraph::BlockGraph() : next_block_id_(0) {
}

BlockGraph::~BlockGraph() {
}

BlockGraph::Block* BlockGraph::AddBlock(BlockType type,
                                        Size size,
                                        const char* name) {
  BlockId id = ++next_block_id_;
  BlockMap::iterator it = blocks_.insert(
      std::make_pair(id, Block(id, type, size, name))).first;

  return &it->second;
}

BlockGraph::Block* BlockGraph::GetBlockById(BlockId id) {
  BlockMap::iterator it(blocks_.find(id));

  if (it == blocks_.end())
    return NULL;

  return &it->second;
}

BlockGraph::AddressSpace::AddressSpace(BlockGraph* graph)
    : graph_(graph) {
  DCHECK(graph != NULL);
}

BlockGraph::Block* BlockGraph::AddressSpace::AddBlock(BlockType type,
                                                      RelativeAddress addr,
                                                      Size size,
                                                      const char* name) {
  // First check to see that the range is clear.
  AddressSpaceImpl::Range range(addr, size);
  AddressSpaceImpl::RangeMap::iterator it =
      address_space_.FindFirstIntersection(range);
  if (it != address_space_.ranges().end())
    return NULL;

  BlockGraph::Block* block = graph_->AddBlock(type, size, name);
  DCHECK(block != NULL);
  bool inserted = InsertImpl(addr, block);
  DCHECK(inserted);

  return block;
}

bool BlockGraph::AddressSpace::InsertBlock(RelativeAddress addr, Block* block) {
  return InsertImpl(addr, block);
}

// TODO(siggi): Remove this method?
BlockGraph::Block* BlockGraph::AddressSpace::GetBlockByAddress(
    RelativeAddress addr) const {
  return GetContainingBlock(addr, 1);
}

BlockGraph::Block* BlockGraph::AddressSpace::GetContainingBlock(
    RelativeAddress addr, Size size) const {
  AddressSpaceImpl::Range range(addr, size);
  AddressSpaceImpl::RangeMap::const_iterator it =
      address_space_.FindFirstIntersection(range);
  if (it == address_space_.ranges().end())
    return NULL;

  return it->second;
}

BlockGraph::Block* BlockGraph::AddressSpace::GetFirstItersectingBlock(
    RelativeAddress addr, Size size) {
  AddressSpaceImpl::Range range(addr, size);
  AddressSpaceImpl::RangeMap::iterator it =
      address_space_.FindFirstIntersection(range);
  if (it == address_space_.ranges().end())
    return NULL;

  return it->second;
}

bool BlockGraph::AddressSpace::GetAddressOf(const Block* block,
                                            RelativeAddress* addr) const {
  DCHECK(block != NULL);
  DCHECK(addr != NULL);

  BlockAddressMap::const_iterator it(block_addresses_.find(block));
  if (it == block_addresses_.end())
    return false;

  *addr = it->second;
  return true;
}

bool BlockGraph::AddressSpace::InsertImpl(RelativeAddress addr, Block* block) {
  Range range(addr, block->size());
  bool inserted = address_space_.Insert(range, block);
  if (!inserted)
    return false;

  inserted = block_addresses_.insert(std::make_pair(block, addr)).second;
  DCHECK(inserted);
  // Update the address stored in the block.
  block->set_addr(addr);

  // And set the original address if it hasn't already been set.
  if (block->original_addr() == kInvalidAddress)
    block->set_original_addr(addr);

  return true;
}

BlockGraph::Block* BlockGraph::AddressSpace::MergeIntersectingBlocks(
    const Range& range) {
  typedef std::vector<std::pair<RelativeAddress, BlockGraph::Block*>>
      BlockAddressVector;

  // Find all the blocks that intersect the range, keep them and their
  // addresses. Start by finding the first intersection, then iterate
  // from there until we find a block that doesn't intersect with range.
  AddressSpaceImpl::RangeMap::iterator address_start =
      address_space_.FindFirstIntersection(range);
  AddressSpaceImpl::RangeMap::iterator address_it(address_start);

  BlockAddressVector intersecting;
  for (; address_it != address_space_.ranges().end() &&
         address_it->first.Intersects(range); ++address_it) {
    intersecting.push_back(std::make_pair(address_it->first.start(),
                                          address_it->second));
  }

  // Bail if there are no intersections.
  if (intersecting.empty())
    return false;

  DCHECK(!intersecting.empty());

  // Calculate the start and end addresses of the new block.
  BlockGraph::Block* first_block = intersecting[0].second;
  BlockGraph::Block* last_block = intersecting[intersecting.size() - 1].second;
  DCHECK(first_block != NULL && last_block != NULL);

  RelativeAddress begin = std::min(range.start(), intersecting[0].first);
  RelativeAddress end = std::max(range.start() + range.size(),
      intersecting[intersecting.size() - 1].first + last_block->size());

  DCHECK(begin <= range.start());
  DCHECK(end >= range.start() + range.size());

  const char* block_name = first_block->name();
  BlockType block_type = first_block->type();

  // Remove the found blocks from the address space, and make
  // sure they're all of the same type as the first block.
  for (size_t i = 0; i < intersecting.size(); ++i) {
    RelativeAddress addr = intersecting[i].first;
    BlockGraph::Block* block = intersecting[i].second;
    DCHECK_EQ(block_type, block->type());
    bool removed = address_space_.Remove(Range(addr, block->size()));
    DCHECK(removed);
    size_t num_removed = block_addresses_.erase(intersecting[i].second);
    DCHECK_EQ(1U, num_removed);
  }

  BlockGraph::Block* new_block = AddBlock(BlockGraph::CODE_BLOCK,
                                          begin, end - begin,
                                          block_name);
  DCHECK(new_block != NULL);

  // Now move all labels and references to the new block.
  for (size_t i = 0; i < intersecting.size(); ++i) {
    RelativeAddress addr = intersecting[i].first;
    BlockGraph::Block* block = intersecting[i].second;
    BlockGraph::Offset start_offset = addr - begin;

    // Move labels.
    BlockGraph::Block::LabelMap::const_iterator
        label_it(block->labels().begin());
    for (; label_it != block->labels().end(); ++label_it) {
      new_block->SetLabel(start_offset + label_it->first,
                          label_it->second.c_str());
    }

    // Copy the reference map since we mutate the original.
    BlockGraph::Block::ReferenceMap refs(block->references());
    BlockGraph::Block::ReferenceMap::const_iterator ref_it(refs.begin());
    for (; ref_it != refs.end(); ++ref_it) {
      block->RemoveReference(ref_it->first);
      new_block->SetReference(start_offset + ref_it->first, ref_it->second);
    }

    // Redirect all referers to the new block, we copy the referer set
    // because it is otherwise mutated during iteration.
    BlockGraph::Block::RefererSet referers = block->referers();
    BlockGraph::Block::RefererSet::const_iterator referer_it(referers.begin());
    for (; referer_it != referers.end(); ++referer_it) {
      // Get the original reference.
      BlockGraph::Block::Referer referer = *referer_it;
      BlockGraph::Block::ReferenceMap::const_iterator found_ref(
          referer.first->references().find(referer.second));
      DCHECK(found_ref != referer.first->references().end());
      BlockGraph::Reference ref(found_ref->second);

      // Redirect the reference to the new block with the adjusted offset.
      BlockGraph::Reference new_ref(ref.type(),
                               ref.size(),
                               new_block,
                               ref.offset() + start_offset);
      referer.first->SetReference(referer.second, new_ref);
    }

    // Check that we've removed all references and
    // referrers from the original block.
    DCHECK(block->references().empty());
    DCHECK(block->referers().empty());
  }

  return new_block;
}

BlockGraph::Block::Block(BlockId id,
                         BlockType type,
                         Size size,
                         const char* name)
    : id_(id),
      type_(type),
      size_(size),
      name_(name),
      addr_(kInvalidAddress),
      original_addr_(kInvalidAddress),
      segment_(kInvalidSegment),
      owns_data_(false),
      data_(NULL),
      data_size_(0) {
}

BlockGraph::Block::~Block() {
  if (owns_data_)
    delete [] data_;
}

bool BlockGraph::Block::SetReference(Offset offset, const Reference& ref) {
  DCHECK(ref.referenced() != NULL);
  DCHECK(offset + ref.size() <= size());

  // Did we have an earlier reference at this location?
  ReferenceMap::iterator it(references_.find(offset));
  bool inserted = false;
  if (it != references_.end()) {
    // Erase the back reference.
    BlockGraph::Block* referenced = it->second.referenced();
    Referer referer(this, offset);
    size_t removed = referenced->referers_.erase(referer);
    DCHECK_EQ(1U, removed);

    // Lastly switch the reference.
    it->second = ref;
  } else {
    // It's a new reference, insert it.
    inserted = references_.insert(std::make_pair(offset, ref)).second;
    DCHECK(inserted);
  }

  // Record the back-reference.
  ref.referenced()->referers_.insert(std::make_pair(this, offset));

  return inserted;
}

bool BlockGraph::Block::RemoveReference(Offset offset) {
  // Do we have reference at this location?
  ReferenceMap::iterator it(references_.find(offset));
  if (it == references_.end())
    return false;

  BlockGraph::Block* referenced = it->second.referenced();
  Referer referer(this, offset);
  size_t removed = referenced->referers_.erase(referer);
  DCHECK_EQ(1U, removed);
  references_.erase(it);

  return true;
}

bool BlockGraph::Block::SetLabel(Offset offset, const char* name) {
  DCHECK(offset >= 0 && static_cast<size_t>(offset) <= size_);

  return labels_.insert(std::make_pair(offset, name)).second;
}

bool BlockGraph::Block::HasLabel(Offset offset) {
  DCHECK(offset >= 0 && static_cast<size_t>(offset) <= size_);

  return labels_.find(offset) != labels_.end();
}

}  // namespace image_util
