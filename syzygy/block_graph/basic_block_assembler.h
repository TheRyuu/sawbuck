// Copyright 2012 Google Inc. All Rights Reserved.
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
// Provides an assembler that assembles to basic block instruction lists.

#ifndef SYZYGY_BLOCK_GRAPH_BASIC_BLOCK_ASSEMBLER_H_
#define SYZYGY_BLOCK_GRAPH_BASIC_BLOCK_ASSEMBLER_H_

#include "syzygy/block_graph/basic_block.h"
#include "syzygy/core/assembler.h"

namespace block_graph {

using core::ValueSize;

class BasicBlockAssembler;
class Operand;

class Value {
 public:
  // Default construction.
  Value();
  // Constructs an 8- or 32-bit value, depending on the minimum number of bits
  // required to represent the Value. If the value can be encoded using 8-bits
  // to have the same representation under sign extension, then an 8-bit Value
  // will be created; otherwise, a 32-bit absolute Value will be created.
  explicit Value(uint32 value);
  // Constructs an absolute value having a specific bit width.
  Value(uint32 value, core::ValueSize size);
  // Constructs a 32 bit absolute value referring to the basic block @p bb.
  explicit Value(BasicBlock* bb);
  // Constructs a 32 bit absolute value referring to @p block at @p offset.
  Value(BlockGraph::Block* block, BlockGraph::Offset offset);
  // Explicitly specified size and reference info.
  Value(uint32 value, ValueSize size, const BasicBlockReference& ref);
  // Copy construction.
  Value(const Value& other);

  // Destructor.
  ~Value();

  // Assignment operator.
  const Value& operator=(const Value& other);

  // @name Accessors.
  // @{
  uint32 value() const { return value_.value(); }
  core::ValueSize size() const { return value_.size(); }
  const BasicBlockReference &reference() const { return reference_; }
  // @}

  // Comparison operator.
  bool operator==(const Value& rhs) const;

 private:
  // Private constructor for Operand.
  Value(const BasicBlockReference& ref, const core::ValueImpl& value);

  friend class BasicBlockAssembler;
  friend class Operand;

  BasicBlockReference reference_;
  core::ValueImpl value_;
};

// Displacements and immediates behave near-identically, but are semantically
// slightly different.
typedef Value Immediate;
typedef Value Displacement;

// An operand implies indirection to memory through one of the myriad
// modes supported by IA32.
class Operand {
 public:
  // A register-indirect mode.
  explicit Operand(core::Register base);

  // A register-indirect with displacement mode.
  Operand(core::Register base, const Displacement& displ);

  // A displacement-only mode.
  explicit Operand(const Displacement& displ);

  // The full [base + index * scale + displ32] mode.
  // @note esp cannot be used as an index register.
  Operand(core::Register base,
          core::Register index,
          core::ScaleFactor scale,
          const Displacement& displ);

  // The full [base + index * scale] mode.
  // @note esp cannot be used as an index register.
  Operand(core::Register base,
          core::Register index,
          core::ScaleFactor scale);

  // The [index * scale + displ32] mode.
  // @note esp cannot be used as an index register.
  Operand(core::Register index,
          core::ScaleFactor scale,
          const Displacement& displ);

  // Copy constructor.
  Operand(const Operand& o);

  // Destructor.
  ~Operand();

  // Assignment operator.
  const Operand& operator=(const Operand& other);

  // @name Accessors.
  // @{
  core::RegisterCode base() const { return operand_.base(); }
  core::RegisterCode index() const { return operand_.index(); }
  core::ScaleFactor scale() const { return operand_.scale(); }
  Displacement displacement() const {
    return Displacement(reference_, operand_.displacement());
  }
  // @}

 private:
  friend class BasicBlockAssembler;

  BasicBlockReference reference_;
  core::OperandImpl operand_;
};

class BasicBlockAssembler {
 public:
  typedef BlockGraph::Block::SourceRange SourceRange;
  typedef BasicBlock::Instructions Instructions;
  typedef core::Register Register;
  typedef core::ConditionCode ConditionCode;

  // Constructs a basic block assembler that inserts new instructions
  // into @p *list at @p where.
  BasicBlockAssembler(const Instructions::iterator& where,
                      Instructions *list);

  // Constructs a basic block assembler that inserts new instructions into
  // @p *list at @p where, assuming a starting address of @p location.
  BasicBlockAssembler(uint32 location,
                      const Instructions::iterator& where,
                      Instructions *list);

  // @returns The source range injected into created instructions.
  SourceRange source_range() const { return serializer_.source_range(); }

  // Set the SourceRange injected repeatedly into each instruction created via
  // the assembler. This should be used with care because it causes the OMAP
  // information to no longer be 1:1 mapping, and may confuse some debuggers.
  // @param source_range The source range set to each created instructions.
  void set_source_range(const SourceRange& source_range) {
    serializer_.set_source_range(source_range);
  }

  // @name Call instructions.
  // @{
  void call(const Immediate& dst);
  void call(const Operand& dst);
  // @}

  // @name Jmp instructions.
  // @{
  void jmp(const Immediate& dst);
  void jmp(const Operand& dst);
  // @}

  // @name Conditional branch instruction.
  // @{
  void j(ConditionCode code, const Immediate& dst);
  // @}

  // @name Byte mov varieties.
  // @{
  void mov_b(const Operand& dst, const Immediate& src);
  // @}

  // @name Double-word mov varieties.
  // @{
  void mov(Register dst, Register src);
  void mov(Register dst, const Operand& src);
  void mov(const Operand& dst, Register src);
  void mov(Register dst, const Immediate& src);
  void mov(const Operand& dst, const Immediate& src);
  // @}

  // @name load effective address.
  void lea(Register dst, const Operand& src);

  // @name stack manipulation.
  // @{
  void push(Register src);
  void push(const Immediate& src);
  void push(const Operand& src);

  void pop(Register src);
  void pop(const Operand& src);
  // @}

  // @name Ret instructions.
  // @{
  void ret();
  void ret(uint16 n);
  // @}

 private:
  class BasicBlockSerializer
      : public core::AssemblerImpl::InstructionSerializer {
   public:
    BasicBlockSerializer(const Instructions::iterator& where,
                         Instructions* list);

    virtual void AppendInstruction(uint32 location,
                                   const uint8* bytes,
                                   size_t num_bytes,
                                   const size_t *ref_locations,
                                   const void* const* refs,
                                   size_t num_refs) OVERRIDE;

    SourceRange source_range() const { return source_range_; }
    void set_source_range(const SourceRange& source_range) {
      source_range_ = source_range;
    }

   private:
    Instructions::iterator where_;
    Instructions* list_;

    // Source range set to instructions appended by this serializer.
    SourceRange source_range_;
  };

  BasicBlockSerializer serializer_;
  core::AssemblerImpl asm_;
};

}  // namespace block_graph

#endif  // SYZYGY_BLOCK_GRAPH_BASIC_BLOCK_ASSEMBLER_H_
