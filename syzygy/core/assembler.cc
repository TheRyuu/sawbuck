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

#include "syzygy/core/assembler.h"

#include <limits>

#include "base/logging.h"

namespace core {

const Register eax(kRegisterEax);
const Register ecx(kRegisterEcx);
const Register edx(kRegisterEdx);
const Register ebx(kRegisterEbx);
const Register esp(kRegisterEsp);
const Register ebp(kRegisterEbp);
const Register esi(kRegisterEsi);
const Register edi(kRegisterEdi);

namespace {

enum Mod {
  Reg1Ind = 0,  // Register indirect mode.
  Reg1ByteDisp = 1,  // Register + byte displacement.
  Reg1WordDisp = 2,  // Register + word displacement.
  Reg1 = 3,  // Register + word displacement.
};

// Returns true if @p operand is a displacment only - e.g.
// specifies neither a base, nor an index register.
bool IsDisplacementOnly(const OperandImpl& operand) {
  return operand.displacement().size() != kSizeNone &&
      operand.base() == kRegisterNone &&
      operand.index() == kRegisterNone;
}

}  // namespace

const size_t AssemblerImpl::kShortBranchOpcodeSize = 1;
const size_t AssemblerImpl::kShortBranchSize = kShortBranchOpcodeSize + 1;

const size_t AssemblerImpl::kLongBranchOpcodeSize = 2;
const size_t AssemblerImpl::kLongBranchSize = kLongBranchOpcodeSize + 4;

const size_t AssemblerImpl::kShortJumpOpcodeSize = 1;
const size_t AssemblerImpl::kShortJumpSize = kShortJumpOpcodeSize + 1;

const size_t AssemblerImpl::kLongJumpOpcodeSize = 1;
const size_t AssemblerImpl::kLongJumpSize = kLongJumpOpcodeSize + 4;

// No instruction on x86 can exceed 15 bytes, per specs.
const size_t AssemblerImpl::kMaxInstructionLength = 15;

OperandImpl::OperandImpl(Register base)
    : base_(base.code()),
      index_(kRegisterNone),
      scale_(kTimes1) {
}

OperandImpl::OperandImpl(Register base,
                         const DisplacementImpl& displacement)
    : base_(base.code()),
      index_(kRegisterNone),
      scale_(kTimes1),
      displacement_(displacement) {
  // There must be a base register.
  DCHECK_NE(kRegisterNone, base_);
}

OperandImpl::OperandImpl(const DisplacementImpl& displacement)
    : base_(kRegisterNone),
      index_(kRegisterNone),
      scale_(kTimes1),
      displacement_(displacement) {
  DCHECK_NE(kSizeNone, displacement.size());
}

OperandImpl::OperandImpl(Register base,
                         Register index,
                         ScaleFactor scale,
                         const DisplacementImpl& displacement)
    : base_(base.code()),
      index_(index.code()),
      scale_(scale),
      displacement_(displacement) {
  // ESP cannot be used as an index register.
  DCHECK_NE(kRegisterEsp, index.code());
  DCHECK_NE(kSizeNone, displacement.size());
}

OperandImpl::OperandImpl(Register base,
                         Register index,
                         ScaleFactor scale)
    : base_(base.code()),
      index_(index.code()),
      scale_(scale) {
  // ESP cannot be used as an index register.
  DCHECK_NE(kRegisterEsp, index.code());
  DCHECK_EQ(kSizeNone, displacement_.size());
}

OperandImpl::OperandImpl(Register index,
                         ScaleFactor scale,
                         const DisplacementImpl& displacement)
    : base_(kRegisterNone),
      index_(index.code()),
      scale_(scale),
      displacement_(displacement) {
  // ESP cannot be used as an index register.
  DCHECK_NE(kRegisterEsp, index.code());
  DCHECK_NE(kSizeNone, displacement.size());
}

OperandImpl::OperandImpl(RegisterCode base,
                         RegisterCode index,
                         ScaleFactor scale,
                         const DisplacementImpl& displacement)
    : base_(base),
      index_(index),
      scale_(scale),
      displacement_(displacement) {
}

ValueImpl::ValueImpl()
    : value_(0), reference_(NULL), size_(kSizeNone) {
}

ValueImpl::ValueImpl(uint32 value, ValueSize size)
    : value_(value), reference_(NULL), size_(size) {
}

ValueImpl::ValueImpl(uint32 value,
                     ValueSize size,
                     const void* value_ref)
    : value_(value), reference_(value_ref), size_(size) {
}

bool ValueImpl::operator==(const ValueImpl& rhs) const {
  return value_ == rhs.value_ &&
      reference_ == rhs.reference_ &&
      size_ == rhs.size_;
}

// This class is used to buffer a single instruction during it's creation.
// TODO(siggi): Add a small state machine in debug mode to ensure the
//     correct order of invocation to opcode/modrm etc.
class AssemblerImpl::InstructionBuffer {
 public:
  explicit InstructionBuffer(AssemblerImpl* assm);
  ~InstructionBuffer();

  // @name Accessors.
  // @{
  size_t len() const { return len_; }
  const uint8* buf() const { return buf_; }
  size_t num_references() const { return num_references_; }
  const size_t *reference_offsets() const { return reference_offsets_; }
  const void*const* references() const { return references_; }
  // @}

  // Emit an opcode byte.
  void EmitOpCodeByte(uint8 opcode);
  // Emit a ModR/M byte, reg_op is either a register or an opcode
  // extension, as fits the instruction being generated.
  void EmitModRMByte(Mod mod, uint8 reg_op, RegisterCode reg1);
  // Emit a SIB byte.
  void EmitScaleIndexBaseByte(ScaleFactor scale,
                              RegisterCode index,
                              RegisterCode base);
  // Emit an operand.
  void EmitOperand(uint8 reg_op, const OperandImpl& op);

  // Emit an 8-bit displacement, with optional reference info.
  void Emit8BitDisplacement(const DisplacementImpl& disp);

  // Emit a 32-bit displacement with optional reference info.
  void Emit32BitDisplacement(const DisplacementImpl& disp);

  // Emit an 8-bit PC-relative value.
  void Emit8BitPCRelative(uint32 location, const ValueImpl& disp);

  // Emit a 32-bit PC-relative value.
  void Emit32BitPCRelative(uint32 location, const ValueImpl& disp);

  // Emit a 16-bit immediate value.
  void Emit16BitValue(uint16 value);

  // Add reference at current location.
  void AddReference(const void* reference);

 protected:
  void EmitByte(uint8 byte);

  AssemblerImpl* asm_;
  size_t num_references_;
  const void* (references_)[2];
  size_t reference_offsets_[2];
  size_t len_;
  uint8 buf_[kMaxInstructionLength];
};

AssemblerImpl::InstructionBuffer::InstructionBuffer(AssemblerImpl* assm)
    : asm_(assm), len_(0), num_references_(0) {
  DCHECK(assm != NULL);
#ifndef NDEBUG
  // Initialize the buffer in debug mode for easier debugging.
  ::memset(buf_, 0xCC, sizeof(buf_));
#endif
}

AssemblerImpl::InstructionBuffer::~InstructionBuffer() {
  asm_->Output(*this);
}

void AssemblerImpl::InstructionBuffer::EmitOpCodeByte(uint8 opcode) {
  EmitByte(opcode);
}

void AssemblerImpl::InstructionBuffer::EmitModRMByte(
    Mod mod, uint8 reg_op, RegisterCode reg1) {
  DCHECK_LE(reg_op, 8);
  DCHECK_NE(kRegisterNone, reg1);

  EmitByte((mod << 6) | (reg_op << 3) | reg1);
}

void AssemblerImpl::InstructionBuffer::EmitScaleIndexBaseByte(
    ScaleFactor scale, RegisterCode index, RegisterCode base) {
  DCHECK_NE(kRegisterNone, index);
  DCHECK_NE(kRegisterNone, base);

  EmitByte((scale << 6) | (index << 3) | base);
}

void AssemblerImpl::InstructionBuffer::EmitOperand(
    uint8 reg_op, const OperandImpl& op) {
  DCHECK_GE(8, reg_op);

  // The op operand can encode any one of the following things:
  // An indirect register access [EAX].
  // An indirect 32-bit displacement only [0xDEADBEEF].
  // An indirect base register + 32/8-bit displacement [EAX+0xDEADBEEF].
  // An indirect base + index register*scale [EAX+ECX*4].
  // An indirect base + index register*scale + 32/8-bit displacement
  //   [EAX+ECX*4+0xDEADBEEF].
  // To complicate things, there are certain combinations that can't be encoded
  // canonically. The mode [ESP] or [ESP+disp] can never be encoded in a
  // ModR/M byte alone, as ESP in the ModR/M byte for any of the indirect modes
  // is overloaded to select the SIB representation.
  // Likewise [EBP] is overloaded to encode the [disp32] case.
  // See e.g. http://ref.x86asm.net/geek32-abc.html#modrm_byte_32 for a nice
  // overview table of the ModR/M byte encoding.

  // ESP can never be used as an index register on X86.
  DCHECK_NE(kRegisterEsp, op.index());

  // Is there an index register?
  if (op.index() == kRegisterNone) {
    DCHECK_EQ(kTimes1, op.scale());

    // No index register, is there a base register?
    if (op.base() == kRegisterNone) {
      // No base register, this is a displacement only.
      DCHECK_NE(kSizeNone, op.displacement().size());
      DCHECK_EQ(kTimes1, op.scale());

      // The [disp32] mode is encoded by overloading [EBP].
      EmitModRMByte(Reg1Ind, reg_op, kRegisterEbp);
      Emit32BitDisplacement(op.displacement());
    } else {
      // Base register only, is it ESP?
      if (op.base() == kRegisterEsp) {
        // The [ESP] and [ESP+disp] cases cannot be encoded without a SIB byte.
        if (op.displacement().size() == kSizeNone) {
          EmitModRMByte(Reg1Ind, reg_op, kRegisterEsp);
          EmitScaleIndexBaseByte(kTimes1, kRegisterEsp, kRegisterEsp);
        } else if (op.displacement().size() == kSize8Bit) {
          EmitModRMByte(Reg1ByteDisp, reg_op, kRegisterEsp);
          EmitScaleIndexBaseByte(kTimes1, kRegisterEsp, kRegisterEsp);
          Emit8BitDisplacement(op.displacement());
        } else {
          DCHECK_EQ(kSize32Bit, op.displacement().size());
          EmitModRMByte(Reg1WordDisp, reg_op, kRegisterEsp);
          EmitScaleIndexBaseByte(kTimes1, kRegisterEsp, kRegisterEsp);
          Emit32BitDisplacement(op.displacement());
        }
      } else if (op.displacement().size() == kSizeNone) {
        if (op.base() == kRegisterEbp) {
          // The [EBP] case cannot be encoded canonically, there always must
          // be a (zero) displacement.
          EmitModRMByte(Reg1ByteDisp, reg_op, op.base());
          Emit8BitDisplacement(DisplacementImpl(0, kSize8Bit, NULL));
        } else {
          EmitModRMByte(Reg1Ind, reg_op, op.base());
        }
      } else if (op.displacement().size() == kSize8Bit) {
        // It's [base+disp8], or possibly [EBP].
        EmitModRMByte(Reg1ByteDisp, reg_op, op.base());
        Emit8BitDisplacement(op.displacement());
      } else {
        DCHECK_EQ(kSize32Bit, op.displacement().size());
        // It's [base+disp32].
        EmitModRMByte(Reg1WordDisp, reg_op, op.base());
        Emit32BitDisplacement(op.displacement());
      }
    }
  } else if (op.base() == kRegisterNone) {
    // Index, no base.
    DCHECK_NE(kRegisterNone, op.index());
    DCHECK_EQ(kRegisterNone, op.base());

    // This mode always has a 32 bit displacement.
    EmitModRMByte(Reg1Ind, reg_op, kRegisterEsp);
    EmitScaleIndexBaseByte(op.scale(), op.index(), kRegisterEbp);
    Emit32BitDisplacement(op.displacement());
  } else {
    // Index and base case.
    DCHECK_NE(kRegisterNone, op.index());
    DCHECK_NE(kRegisterNone, op.base());

    // Is there a displacement?
    if (op.displacement().size() == kSizeNone) {
      EmitModRMByte(Reg1Ind, reg_op, kRegisterEsp);
      EmitScaleIndexBaseByte(op.scale(), op.index(), op.base());
    } else if (op.displacement().size() == kSize8Bit) {
      EmitModRMByte(Reg1ByteDisp, reg_op, kRegisterEsp);
      EmitScaleIndexBaseByte(op.scale(), op.index(), op.base());
      Emit8BitDisplacement(op.displacement());
    } else {
      DCHECK_EQ(kSize32Bit, op.displacement().size());
      EmitModRMByte(Reg1WordDisp, reg_op, kRegisterEsp);
      EmitScaleIndexBaseByte(op.scale(), op.index(), op.base());
      Emit32BitDisplacement(op.displacement());
    }
  }
}

void AssemblerImpl::InstructionBuffer::Emit8BitDisplacement(
    const DisplacementImpl& disp) {
  DCHECK(disp.size() == kSize8Bit);

  AddReference(disp.reference());

  EmitByte(disp.value());
}

void AssemblerImpl::InstructionBuffer::Emit32BitDisplacement(
    const DisplacementImpl& disp) {
  AddReference(disp.reference());

  uint32 value = disp.value();
  EmitByte(value);
  EmitByte(value >> 8);
  EmitByte(value >> 16);
  EmitByte(value >> 24);
}

void AssemblerImpl::InstructionBuffer::Emit8BitPCRelative(
    uint32 location, const ValueImpl& value) {
  DCHECK_EQ(kSize8Bit, value.size());

  AddReference(value.reference());

  // Turn the absolute value into a value relative to the address of
  // the end of the emitted constant.
  int32 relative_value = value.value() - (location + len_ + 1);
  DCHECK_LE(std::numeric_limits<int8>::min(), relative_value);
  DCHECK_GE(std::numeric_limits<int8>::max(), relative_value);
  EmitByte(relative_value);
}

void AssemblerImpl::InstructionBuffer::Emit32BitPCRelative(
    uint32 location, const ValueImpl& value) {
  DCHECK_EQ(kSize32Bit, value.size());

  AddReference(value.reference());

  // Turn the absolute value into a value relative to the address of
  // the end of the emitted constant.
  uint32 relative_value = value.value() - (location + len_ + 4);
  EmitByte(relative_value);
  EmitByte(relative_value >> 8);
  EmitByte(relative_value >> 16);
  EmitByte(relative_value >> 24);
}

void AssemblerImpl::InstructionBuffer::Emit16BitValue(uint16 value) {
  EmitByte(value);
  EmitByte(value >> 8);
}

void AssemblerImpl::InstructionBuffer::AddReference(const void* reference) {
  if (reference == NULL)
    return;

  DCHECK_GT(arraysize(references_), num_references_);
  reference_offsets_[num_references_] = len();
  references_[num_references_] = reference;
  ++num_references_;
}

void AssemblerImpl::InstructionBuffer::EmitByte(uint8 byte) {
  DCHECK_GT(sizeof(buf_), len_);
  buf_[len_++] = byte;
}

AssemblerImpl::AssemblerImpl(uint32 location, InstructionSerializer* serializer)
    : location_(location), serializer_(serializer) {
  DCHECK(serializer != NULL);
}

void AssemblerImpl::call(const ImmediateImpl& dst) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xE8);
  instr.Emit32BitPCRelative(location_, dst);
}

void AssemblerImpl::call(const OperandImpl& dst) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xFF);
  instr.EmitOperand(0x2, dst);
}

void AssemblerImpl::j(ConditionCode cc, const ImmediateImpl& dst) {
  DCHECK_LE(0, cc);
  DCHECK_GE(15, cc);

  InstructionBuffer instr(this);
  if (dst.size() == kSize32Bit) {
    instr.EmitOpCodeByte(0x0F);
    instr.EmitOpCodeByte(0x80 | cc);
    instr.Emit32BitPCRelative(location_, dst);
  } else {
    DCHECK_EQ(kSize8Bit, dst.size());
    instr.EmitOpCodeByte(0x70 | cc);
    instr.Emit8BitPCRelative(location_, dst);
  }
}

void AssemblerImpl::jecxz(const ImmediateImpl& dst) {
  DCHECK_EQ(kSize8Bit, dst.size());
  InstructionBuffer instr(this);
  instr.EmitOpCodeByte(0xE3);
  instr.Emit8BitPCRelative(location_, dst);
}

void AssemblerImpl::jmp(const ImmediateImpl& dst) {
  InstructionBuffer instr(this);

  if (dst.size() == kSize32Bit) {
    instr.EmitOpCodeByte(0xE9);
    instr.Emit32BitPCRelative(location_, dst);
  } else {
    DCHECK_EQ(kSize8Bit, dst.size());
    instr.EmitOpCodeByte(0xEB);
    instr.Emit8BitPCRelative(location_, dst);
  }
}

void AssemblerImpl::jmp(const OperandImpl& dst) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xFF);
  instr.EmitOperand(0x4, dst);
}

void AssemblerImpl::l(LoopCode lc, const ImmediateImpl& dst) {
  DCHECK_EQ(kSize8Bit, dst.size());
  DCHECK_LE(0, lc);
  DCHECK_GE(2, lc);
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xE0 | lc);
  instr.Emit8BitPCRelative(location_, dst);
}

void AssemblerImpl::ret() {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xC3);
}

void AssemblerImpl::ret(uint16 n) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xC2);
  instr.Emit16BitValue(n);
}

void AssemblerImpl::mov_b(const OperandImpl& dst, const ImmediateImpl& src) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xC6);
  instr.EmitOperand(0, dst);
  instr.Emit8BitDisplacement(src);
}

void AssemblerImpl::mov(Register dst, Register src) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0x8B);
  instr.EmitModRMByte(Reg1, dst.code(), src.code());
}

void AssemblerImpl::mov(Register dst, const OperandImpl& src) {
  InstructionBuffer instr(this);

  if (dst.code() == kRegisterEax && IsDisplacementOnly(src)) {
    // Special encoding for indirect displacement only to EAX.
    instr.EmitOpCodeByte(0xA1);
    instr.Emit32BitDisplacement(src.displacement());
  } else {
    instr.EmitOpCodeByte(0x8B);
    instr.EmitOperand(dst.code(), src);
  }
}

void AssemblerImpl::mov(const OperandImpl& dst, Register src) {
  InstructionBuffer instr(this);

  if (src.code() == kRegisterEax && IsDisplacementOnly(dst)) {
    // Special encoding for indirect displacement only from EAX.
    instr.EmitOpCodeByte(0xA3);
    instr.Emit32BitDisplacement(dst.displacement());
  } else {
    instr.EmitOpCodeByte(0x89);
    instr.EmitOperand(src.code(), dst);
  }
}

void AssemblerImpl::mov(Register dst, const ValueImpl& src) {
  DCHECK_NE(kSizeNone, src.size());
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xB8 | dst.code());
  instr.Emit32BitDisplacement(src);
}

void AssemblerImpl::mov(const OperandImpl& dst, const ImmediateImpl& src) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xC7);
  instr.EmitOperand(0, dst);
  instr.Emit32BitDisplacement(src);
}

void AssemblerImpl::lea(Register dst, const OperandImpl& src) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0x8D);
  instr.EmitOperand(dst.code(), src);
}

void AssemblerImpl::push(Register src) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0x50 | src.code());
}

void AssemblerImpl::push(const ImmediateImpl& src) {
  DCHECK_EQ(kSize32Bit, src.size());
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0x68);
  instr.Emit32BitDisplacement(src);
}

void AssemblerImpl::push(const OperandImpl& dst) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0xFF);
  instr.EmitOperand(0x6, dst);
}

void AssemblerImpl::pop(Register src) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0x58 | src.code());
}

void AssemblerImpl::pop(const OperandImpl& dst) {
  InstructionBuffer instr(this);

  instr.EmitOpCodeByte(0x8F);
  instr.EmitOperand(0, dst);
}


void AssemblerImpl::Output(const InstructionBuffer& instr) {
  serializer_->AppendInstruction(location_,
                                 instr.buf(),
                                 instr.len(),
                                 instr.reference_offsets(),
                                 instr.references(),
                                 instr.num_references());

  location_ += instr.len();
}
}  // namespace core
