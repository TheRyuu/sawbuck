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
// A class that attempts to disassemble a function into basic blocks.
//
// Given a function block (dubbed macro block), this disassembler attempts to
// cut it up into sequences of contiguous instruction runs and data blocks. A
// contiguous instruction run is defined as a set of instructions that under
// normal operation will always run from start to end. This class requires that
// all external references to addresses within a function block have an
// associated label.

#ifndef SYZYGY_BLOCK_GRAPH_BASIC_BLOCK_DISASSEMBLER_H_
#define SYZYGY_BLOCK_GRAPH_BASIC_BLOCK_DISASSEMBLER_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/string_piece.h"
#include "syzygy/block_graph/basic_block.h"
#include "syzygy/block_graph/block_graph.h"
#include "syzygy/core/address.h"
#include "syzygy/core/disassembler.h"
#include "distorm.h"  // NOLINT

namespace block_graph {

// This class re-disassembles an already-processed code block (referred to
// herein as a macro block) and breaks it up into basic blocks.
// A basic block is defined here as one of:
// 1) A series of code instructions that will be executed contiguously.
// 2) A chunk of data (or at least something we couldn't identify as code).
// TODO(robertshield) 3) Padding.
//
// The break-down into basic blocks happens in three passes:
// 1) Code disassembly starting from the given set of unvisited labels.
// 2) Data block construction to fill any gaps.
// 3) Block break up that splits up previously discovered blocks if it is
//    discovered that they contain jump targets or unvisited labels.
//
// In order for this to work, all jump targets from external blocks must already
// have been marked with labels. To get this, run the standard disassembly phase
// using Decomposer and Disassembler first. Failing to do this will result in
// missing some potential basic-block splits.
class BasicBlockDisassembler : public core::Disassembler {
 public:
  typedef core::AbsoluteAddress AbsoluteAddress;

  // Use the AddressSpace primitives to represent the set of basic blocks.
  typedef core::AddressSpace<AbsoluteAddress, size_t, BasicBlock>
      BBAddressSpace;
  typedef BBAddressSpace::Range Range;
  typedef BBAddressSpace::RangeMap RangeMap;
  typedef BBAddressSpace::RangeMapConstIter RangeMapConstIter;
  typedef BBAddressSpace::RangeMapIter RangeMapIter;

  // Creates and sets up a BasicBlockDisassembler that decomposes a function
  // macro block into basic blocks.
  // @param code pointer to the data bytes the containing macro block refers to.
  // @param code_size the size of the containing macro block.
  // @param code_addr the starting address of the macro code block (e.g. as
  //     given by a BlockGraph::AddressSpace).
  // @param entry_points The set of addresses within the macro block from which
  //     to start disassembly walks. These will typically be labels within
  //     the macro block.
  // @param containing_block_name The name of the containing macro block.
  // @param on_instruction Pointer to a callback routine called during
  //     disassembly.
  BasicBlockDisassembler(const uint8* code,
                         size_t code_size,
                         AbsoluteAddress code_addr,
                         const AddressSet& entry_points,
                         const base::StringPiece& containing_block_name,
                         Disassembler::InstructionCallback on_instruction);

  // Returns a RangeMap mapping ranges that each cover a single basic block
  // to BlockGraph::Block instances that contain some information about that
  // basic block.
  const BBAddressSpace& GetBasicBlockRanges() const {
    return basic_block_address_space_;
  }

 protected:
  // Overrides from Disassembler. See disassembler.h for comments.
  // @{
  virtual CallbackDirective OnInstruction(AbsoluteAddress addr,
                                          const _DInst& inst) OVERRIDE;
  virtual CallbackDirective OnBranchInstruction(AbsoluteAddress addr,
                                                const _DInst& inst,
                                                AbsoluteAddress dest) OVERRIDE;
  virtual CallbackDirective OnStartInstructionRun(
      AbsoluteAddress start_address) OVERRIDE;
  virtual CallbackDirective OnEndInstructionRun(AbsoluteAddress addr,
                                                const _DInst& inst) OVERRIDE;
  virtual CallbackDirective OnDisassemblyComplete() OVERRIDE;
  // @}

  // Fills in all gaps in the range
  // [code_addr_, code_addr_ + code_size_[ with data basic blocks.
  // @returns true on success.
  bool FillInGapBlocks();

  // For every range in @p basic_block_ranges that contains an address in
  // @p jump_targets (not counting addresses that point to the beginning of the
  // range), split that range in two.
  // @returns true on success.
  bool SplitBlockOnJumpTargets(const AddressSet& jump_targets);

  // Returns true if basic_block_ranges_ fully covers the macro block with
  // no gaps or overlap.
  bool ValidateBasicBlockCoverage() const;

  // Inserts a range and associated block into @p basic_block_ranges.
  bool InsertBlockRange(AbsoluteAddress addr,
                        size_t size,
                        BlockGraph::BlockType type);

  // An address space that keeps the basic block range mapping.
  BBAddressSpace basic_block_address_space_;

  // Tracks locations our conditional branches jump to. Used to fix up basic
  // blocks by breaking up those that have a jump target in the middle.
  AddressSet jump_targets_;

  // An incrementing counter used to number the temporary basic blocks as
  // they are constructed.
  int next_block_id_;

  // The name of the containing block.
  std::string containing_block_name_;

  // The start of the current basic block during a walk.
  AbsoluteAddress current_block_start_;

  // The list of instructions in the current basic block.
  BasicBlock::Instructions current_instructions_;

  // The set of successors for the current basic block.
  BasicBlock::Successors current_successors_;
};

}  // namespace block_graph

#endif  // SYZYGY_BLOCK_GRAPH_BASIC_BLOCK_DISASSEMBLER_H_
