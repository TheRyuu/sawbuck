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
// This file declares implementation classes to generate assembly code.
// The API to the assembler is intentionally very close to the API exposed
// by the V8 assembler.

#ifndef SYZYGY_CORE_ASSEMBLER_H_
#define SYZYGY_CORE_ASSEMBLER_H_

#include "base/basictypes.h"
#include "base/logging.h"

namespace core {

enum RegisterCode {
  kRegisterNone = -1,
  kRegisterEax = 0,
  kRegisterEcx = 1,
  kRegisterEdx = 2,
  kRegisterEbx = 3,
  kRegisterEsp = 4,
  kRegisterEbp = 5,
  kRegisterEsi = 6,
  kRegisterEdi = 7,
};

// The condition codes by which conditional branches are determined. This enum
// is taken from the V8 project, and has the property that the conditions are
// defined to be bit-wise ORed into the base conditional branch opcode, and
// they can be easily negated/inverted.
//
// See:
//     http://code.google.com/p/v8/source/browse/trunk/src/ia32/assembler-ia32.h
enum ConditionCode {
  // Any value < 0 is considered no_condition
  kNoCondition  = -1,

  kOverflow =  0,
  kNoOverflow =  1,
  kBelow =  2,
  kAboveEqual =  3,
  kEqual =  4,
  kNotEqual =  5,
  kBelowEqual =  6,
  kAbove =  7,
  kNegative =  8,
  kPositive =  9,
  kParityEven = 10,
  kParityOdd = 11,
  kLess = 12,
  kGreaterEqual = 13,
  kLessEqual = 14,
  kGreater = 15,

  // Aliases.
  kCarry = kBelow,
  kNotCarry = kAboveEqual,
  kZero = kEqual,
  kNotZero = kNotEqual,
  kSign = kNegative,
  kNotSign = kPositive,

  // Extents.
  kMinConditionCode = 0,
  kMaxConditionCode = 15
};

// The conditions on which a loop instruction should branch. These are modeled
// in the same manner as ConditionCode (above).
enum LoopCode {
  kLoopOnCounterAndNotZeroFlag = 0,  // LOOPNE and LOOPNZ
  kLoopOnCounterAndZeroFlag = 1,  // LOOPE and NOOPZ.
  kLoopOnCounter = 2,  // LOOP.
};

inline ConditionCode NegateConditionCode(ConditionCode cc) {
  DCHECK_GT(16, cc);
  return static_cast<ConditionCode>(cc ^ 1);
}

// Each instance of this class names a register.
class Register {
 public:
  explicit Register(RegisterCode code) : code_(code) {
    DCHECK(code != kRegisterNone);
  }

  RegisterCode code() const { return code_; }
  void set_code(RegisterCode code) { code_ = code; }

 private:
  RegisterCode code_;
};

// Convenience constants for the x86 registers.
extern const Register eax;
extern const Register ecx;
extern const Register edx;
extern const Register ebx;
extern const Register esp;
extern const Register ebp;
extern const Register esi;
extern const Register edi;

// Selects a scale for the Operand addressing modes.
// The values match the encoding in the x86 SIB bytes.
enum ScaleFactor {
  kTimes1 = 0,
  kTimes2 = 1,
  kTimes4 = 2,
  kTimes8 = 3,
};

// Size for immediate and displacement operands.
enum ValueSize {
  kSizeNone,
  kSize8Bit,
  kSize32Bit,
};

// An instance of this class is an explicit value, which is either
// an immediate or a displacement.
class ValueImpl  {
 public:
  ValueImpl();
  ValueImpl(uint32 value, ValueSize size);
  ValueImpl(uint32 value, ValueSize size, const void* imm_ref);

  // @name Accessors.
  // @{
  uint32 value() const { return value_; }
  const void* reference() const { return reference_; }
  ValueSize size() const { return size_; }
  // @}

  // Comparison operator.
  bool operator==(const ValueImpl& rhs) const;

 private:
  uint32 value_;
  const void* reference_;
  ValueSize size_;
};

// Displacements and immediates behave near-identically, but are semantically
// slightly different.
typedef ValueImpl ImmediateImpl;
typedef ValueImpl DisplacementImpl;

// An operand implies indirection to memory through one of the myriad
// modes supported by IA32.
class OperandImpl {
 public:
  // A register-indirect mode.
  explicit OperandImpl(Register base);

  // A register-indirect with displacement mode.
  OperandImpl(Register base, const DisplacementImpl& displ);

  // A displacement-only mode.
  explicit OperandImpl(const DisplacementImpl& displ);

  // The full [base + index * scale + displ32] mode.
  // @note esp cannot be used as an index register.
  OperandImpl(Register base,
              Register index,
              ScaleFactor scale,
              const DisplacementImpl& displ);

  // The [base + index * scale] mode.
  // @note esp cannot be used as an index register.
  OperandImpl(Register base,
              Register index,
              ScaleFactor scale);

  // The [index * scale + displ32] mode - e.g. no base.
  // @note esp cannot be used as an index register.
  OperandImpl(Register index,
              ScaleFactor scale,
              const DisplacementImpl& displ);

  // Low-level constructor, none of the parameters are checked.
  OperandImpl(RegisterCode base,
              RegisterCode index,
              ScaleFactor scale,
              const DisplacementImpl& displacement);

  // @name Accessors.
  // @{
  RegisterCode base() const { return base_; }
  RegisterCode index() const { return index_; }
  ScaleFactor scale() const { return scale_; }
  const DisplacementImpl& displacement() const { return displacement_; }
  // @}

 private:
  // The base register involved, or none.
  RegisterCode base_;
  // The index register involved, or none.
  RegisterCode index_;
  // The scaling factor, must be kTimes1 if no index register.
  ScaleFactor scale_;
  // The displacement, if any.
  DisplacementImpl displacement_;
};

// The assembler takes care of maintaining an output location (address), and
// generating a stream of bytes and references as instructions are assembled.
class AssemblerImpl {
 public:
  // The assembler pushes instructions and references to
  // one of these for serialization.
  class InstructionSerializer {
   public:
    virtual void AppendInstruction(uint32 location,
                                   const uint8* bytes,
                                   size_t num_bytes,
                                   const size_t *ref_locations,
                                   const void* const* refs,
                                   size_t num_refs) = 0;
  };

  // Constructs an assembler that assembles to @p delegate
  // starting at @p location.
  AssemblerImpl(uint32 location, InstructionSerializer* serializer);

  // @name Accessors.
  // @{
  uint32 location() const { return location_; }
  void set_location(uint32 location) { location_ = location; }
  // @}

  // @name Call instructions.
  // @{
  void call(const ImmediateImpl& dst);
  void call(const OperandImpl& dst);
  // @}

  // @name Control flow instructions.
  // @{
  void j(ConditionCode cc, const ImmediateImpl& dst);
  void jecxz(const ImmediateImpl& dst);
  void jmp(const ImmediateImpl& dst);
  void jmp(const OperandImpl& dst);
  void l(LoopCode lc, const ImmediateImpl& dst);
  void ret();
  void ret(uint16 n);
  // @}

  // @name byte mov varieties.
  // @{
  void mov_b(const OperandImpl& dst, const ImmediateImpl& src);
  // @}

  // @name Double-word mov varieties.
  // @{
  void mov(Register dst, Register src);
  void mov(Register dst, const OperandImpl& src);
  void mov(const OperandImpl& dst, Register src);
  void mov(Register dst, const ImmediateImpl& src);
  void mov(const OperandImpl& dst, const ImmediateImpl& src);
  // @}

  // @name load effective address.
  void lea(Register dst, const OperandImpl& src);

  // @name stack manipulation.
  // @{
  void push(Register src);
  void push(const ImmediateImpl& src);
  void push(const OperandImpl& src);

  void pop(Register dst);
  void pop(const OperandImpl& src);
  // @}

  // @name Aliases
  // @{
  void loop(const ImmediateImpl& dst) { l(kLoopOnCounter, dst); }
  void loope(const ImmediateImpl& dst) { l(kLoopOnCounterAndZeroFlag, dst); }
  void loopne(const ImmediateImpl& dst) {
    l(kLoopOnCounterAndNotZeroFlag, dst);
  }
  // @}

  // Size of an 8 bit reach branch opcode.
  static const size_t kShortBranchOpcodeSize;
  // Size of an 8 bit reach branch.
  static const size_t kShortBranchSize;

  // Size of a 32 bit reach branch op code.
  static const size_t kLongBranchOpcodeSize;
  // Size of an 8bit reach branch.
  static const size_t kLongBranchSize;

  // Size of an 8 bit reach jump opcode.
  static const size_t kShortJumpOpcodeSize;
  // Size of an 8 bit reach jump.
  static const size_t kShortJumpSize;

  // Size of a 32 bit reach jump op code.
  static const size_t kLongJumpOpcodeSize;
  // Size of an 8bit reach jump.
  static const size_t kLongJumpSize;

  // The maximum length a single instruction will assemble to.
  // No instruction on x86 can exceed 15 bytes, per specs.
  static const size_t kMaxInstructionLength = 15;

 private:
  class InstructionBuffer;
  // Output the instruction data in @p instr to our delegate.
  void Output(const InstructionBuffer& instr);

  // Stores the current location of assembly.
  uint32 location_;

  // The delegate we push instructions at.
  InstructionSerializer* serializer_;
};

}  // namespace core

#endif  // SYZYGY_CORE_ASSEMBLER_H_
