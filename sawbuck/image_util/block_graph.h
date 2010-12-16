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
// A block graph is a an abstract graph of blocks, each of which has an ID,
// a type, a size and a few other properties.
// Each block represents either code or data, and blocks can reference
// one another through references of various types.
#ifndef SAWBUCK_IMAGE_UTIL_BLOCK_GRAPH_H_
#define SAWBUCK_IMAGE_UTIL_BLOCK_GRAPH_H_

#include "base/basictypes.h"
#include "sawbuck/image_util/address.h"
#include "sawbuck/image_util/address_space.h"
#include <map>
#include <hash_map>
#include <set>
#include <string>
#include <vector>

namespace image_util {

// The invalid address can never occur in an graph, it's used as default
// value for block addresses.
extern const RelativeAddress kInvalidAddress;
extern const size_t kInvalidSegment;

// The BlockGraph is a top-level container for Blocks.
class BlockGraph {
 public:
  typedef size_t BlockId;
  typedef size_t Size;
  typedef ptrdiff_t Offset;

  enum BlockType {
    CODE_BLOCK,
    DATA_BLOCK,
    READONLY_BLOCK,
  };

  enum ReferenceType {
    PC_RELATIVE_REF,
    ABSOLUTE_REF,
    RELATIVE_REF,
    FILE_OFFSET_REF,
  };
  class Block;
  class Reference;
  class AddressSpace;

  // The block map contains all blocks, indexed by id.
  typedef std::map<BlockId, Block> BlockMap;

  BlockGraph();
  ~BlockGraph();

  // Add @p block of type @p type and @p size and
  // return the new block.
  // @returns the new block.
  Block* AddBlock(BlockType type, Size size, const char* name);

  // Accessors.
  const BlockMap& blocks() const { return blocks_; }
  BlockMap& blocks_mutable() { return blocks_; }

  // Retrieve the block with id.
  // @returns the block in question or NULL if no such block.
  Block* GetBlockById(BlockId id);

 private:
  // All blocks we contain.
  BlockMap blocks_;

  // Our block ID allocator.
  BlockId next_block_id_;
};

// A block represents an indivisible block of either code or data.
// The block also stores references to other blocks in the graph, their
// relative location within the block and their type and size.
// Each block has a set of attributes, including a size, a name,
// an original address and a "current" address.
// Most of those attributes are mutable, and are set in the process of
// creating and manipulating images and graph address spaces.
class BlockGraph::Block {
 public:
  // Set of the blocks that have a reference to this block.
  // This is keyed on block and source offset (not destination offset),
  // to allow easily locate and remove the backreferences on change or
  // deletion.
  typedef std::pair<Block*, Offset> Referer;
  typedef std::set<Referer> RefererSet;
  typedef std::map<Offset, Reference> ReferenceMap;
  typedef std::map<Offset, std::string> LabelMap;

  Block(BlockId id,
        BlockType type,
        Size size,
        const char* name);
  ~Block();

  // Accessors.
  BlockId id() const { return id_; }
  BlockType type() const { return type_; }

  Size size() const { return size_; }
  const char* name() const { return name_.c_str(); }
  void set_name(const char* name) { name_ = name; }

  // The address of the block is set any time the block is assigned
  // an address in an address space.
  RelativeAddress addr() const { return addr_; }
  void set_addr(RelativeAddress addr) { return addr_ = addr; }

  // The original address of the block is set the first time the block
  // is assigned an address in any address space, and does not change
  // after that.
  RelativeAddress original_addr() const { return original_addr_; }
  void set_original_addr(RelativeAddress addr) {
      return original_addr_ = addr;
  }

  // The segment index for the block, this is a convenience attribute.
  size_t segment() const { return segment_; }
  void set_segment(size_t segment) { segment_ = segment; }

  // This is true iff data_ is in the ownership of the block.
  // When true, the block will delete [] data_ on destruction.
  bool owns_data() const { return owns_data_; }
  void set_owns_data(bool owns_data) { owns_data_ = owns_data; }

  // The data bytes the block refers to.
  const uint8* data() const { return data_; }
  void set_data(const uint8* data) { data_ = data; }

  // The data size may be smaller than the block size (see size()),
  // when the block e.g. refers to data that's all or part
  // zero-initialized by the linker/loader.
  size_t data_size() const { return data_size_; }
  void set_data_size(size_t data_size) { data_size_ = data_size; }

  const ReferenceMap& references() const { return references_; }
  const RefererSet& referers() const { return referers_; }
  const LabelMap& labels() const { return labels_; }

  // Set the reference at @p offset to @p ref.
  // If there's a pre-existing reference at @p offset, this overrides it.
  // @param offset offset of the reference into this block.
  // @param ref the reference to add.
  // @returns true iff this inserts a new reference.
  bool SetReference(Offset offset, const Reference& ref);

  // Remove the reference at @p offset.
  // @returns true iff there was a reference at @p offset.
  bool RemoveReference(Offset offset);

  // Set a label to @p offset.
  // A label in code marks the location of the start of an instruction -
  // e.g. a location where disassembly can usefully commence. Labels
  // appear to be inserted by the VS tool chain where e.g. a switch
  // statement is implemented with a jump table, to note the location
  // of the jump destinations.
  // @returns true iff a new label is inserted.
  // @note that only one label can exist at each offset, and the first
  //    label set at any offset will stay there.
  bool SetLabel(Offset offset, const char* name);

  // Returns true iff the block has a label at @p offset.
  bool HasLabel(Offset offset);

 private:
  BlockId id_;
  BlockType type_;
  Size size_;
  std::string name_;
  RelativeAddress addr_;
  RelativeAddress original_addr_;

  size_t segment_;

  ReferenceMap references_;
  RefererSet referers_;
  LabelMap labels_;

  // True iff data_ is ours to deallocate with delete [].
  // If this is false, data_ must be guaranteed to outlive the block.
  bool owns_data_;
  // A pointer to the code or data we represent.
  const uint8* data_;
  // Size of the above.
  size_t data_size_;
};

// An graph address space endows an graph with a non-overlapping ordering
// on blocks, where each block occupies zero or one address ranges in the
// address space. No two blocks may overlap in an address space.
class BlockGraph::AddressSpace {
 public:
  typedef image_util::AddressSpace<RelativeAddress, BlockGraph::Size, Block*>
      AddressSpaceImpl;
  typedef AddressSpaceImpl::RangeMap RangeMap;
  typedef AddressSpaceImpl::Range Range;

  // Constructs a new empty address space.
  // @p start to @p start + @p size on @p graph.
  explicit AddressSpace(BlockGraph* graph);

  // Add a block of type @p type and @p size at @p address to our associated
  // graph, and return the new block.
  // @returns the new block, or NULL if the new block would overlap
  //    an existing block.
  Block* AddBlock(BlockType type,
                  RelativeAddress addr,
                  Size size,
                  const char* name);

  // Merges all blocks that intersect @p range into a single block.
  // Moves labels and references from the intersecting blocks to the
  // merged block, and changes referring blocks to refer to the new,
  // merged block.
  // @returns the new, merged block if there was at least one intersecting
  //    block in @p range, or NULL otherwise.
  Block* MergeIntersectingBlocks(const Range& range);

  // Insert existing block @p block at @p address.
  // @returns true on succes, or false if the @p block would overlap
  //    an existing block.
  bool InsertBlock(RelativeAddress addr, Block* block);

  // Returns a pointer to the block containing address, or NULL
  // if no block contains address.
  Block* GetBlockByAddress(RelativeAddress address) const;

  // Finds the first block, if any that intersects
  // [@p address, @p address + @p size).
  Block* GetFirstItersectingBlock(RelativeAddress address, Size size);

  // Retrieve the address off @p block.
  // @param block the block in question.
  // @param addr on success, returns the address of @p block in this
  //    address space.
  // @returns true on success, false if @p block is not in this
  //    address space.
  bool GetAddressOf(const Block* block, RelativeAddress* addr) const;

  // Accessor.
  BlockGraph* graph() const { return graph_; }

  const AddressSpaceImpl& address_space_impl() const {
    return address_space_;
  }

 private:
  bool InsertImpl(RelativeAddress addr, Block* block);

  typedef stdext::hash_map<const Block*, RelativeAddress> BlockAddressMap;

  AddressSpaceImpl address_space_;
  BlockAddressMap block_addresses_;
  BlockGraph* graph_;
};

// Represents a reference from one block to another.
class BlockGraph::Reference {
 public:
  // @param type type of reference.
  // @param size size of reference.
  // @param referenced the referenced block.
  // @param referenced_offset offset of reference into referenced.
  Reference(ReferenceType type,
            Size size,
            Block* referenced,
            Offset offset)
      : type_(type),
        size_(size),
        referenced_(referenced),
        offset_(offset) {
  }

  // Copy constructor.
  Reference(const Reference& other)
      : type_(other.type_),
        size_(other.size_),
        referenced_(other.referenced_),
        offset_(other.offset_) {
  }

  // Accessors.
  ReferenceType type() const { return type_; }
  Size size() const { return size_; }
  Block* referenced() const { return referenced_; }
  Offset offset() const { return offset_; }

  bool operator==(const Reference& other) const {
    return type_ == other.type_ &&
        size_ == other.size_ &&
        referenced_ == other.referenced_ &&
        offset_ == other.offset_;
  }
 private:
  // Type of this reference.
  ReferenceType type_;

  // Size of this reference.
  // Absolute references are always pointer wide, but PC-relative
  // references can be 1, 2 or 4 byte wide, which affects their range.
  Size size_;

  // The block referenced.
  Block* referenced_;

  // Offset into the referenced block.
  Offset offset_;
};

}  // namespace image_util

#endif  // SAWBUCK_IMAGE_UTIL_BLOCK_GRAPH_H_
