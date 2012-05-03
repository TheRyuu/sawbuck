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

#include "syzygy/core/assembler.h"

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

// This class is used to buffer a single instruction during it's creation.
// TODO(siggi): Add a small state machine in debug mode to ensure the
//     correct order of invocation to opcode/modrm etc.
class AssemblerImpl::InstructionBuffer {
 public:
  InstructionBuffer();

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

  // Emit an 8 bit displacement, with optional reference info.
  void Emit8BitDisplacement(const DisplacementImpl& disp);

  // Emit a 32 bit displacement with optional reference info.
  void Emit32BitDisplacement(const DisplacementImpl& disp);

  // Emit a 32 bit PC-relative value.
  void Emit32BitPCRelative(uint32 location, const ValueImpl& disp);

  // Emit a 16 bit immediate value.
  void Emit16BitValue(uint16 value);

 protected:
  void EmitByte(uint8 byte);

  size_t num_references_;
  const void* (references_)[2];
  size_t reference_offsets_[2];

  // No instruction on x86 can exceed 15 bytes, per specs.
  static const size_t kMaxInstructionLength = 15;
  size_t len_;
  uint8 buf_[kMaxInstructionLength];
};

AssemblerImpl::InstructionBuffer::InstructionBuffer()
    : len_(0), num_references_(0) {
#ifndef NDEBUG
  // Initialize the buffer in debug mode for easier debugging.
  ::memset(buf_, 0xCC, sizeof(buf_));
#endif
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

void AssemblerImpl::InstructionBuffer::Emit8BitDisplacement(
    const DisplacementImpl& disp) {
  DCHECK(disp.size() == kSize8Bit);

  if (disp.reference() != NULL) {
    DCHECK_GT(arraysize(references_), num_references_);
    reference_offsets_[num_references_] = len();
    references_[num_references_] = disp.reference();
  }

  EmitByte(disp.value());
}

void AssemblerImpl::InstructionBuffer::Emit32BitDisplacement(
    const DisplacementImpl& disp) {
  DCHECK(disp.size() == kSize32Bit);

  if (disp.reference() != NULL) {
    DCHECK_GT(arraysize(references_), num_references_);
    reference_offsets_[num_references_] = len();
    references_[num_references_] = disp.reference();
  }

  uint32 value = disp.value();
  EmitByte(value);
  EmitByte(value >> 8);
  EmitByte(value >> 16);
  EmitByte(value >> 24);
}

void AssemblerImpl::InstructionBuffer::Emit32BitPCRelative(
    uint32 location, const ValueImpl& value) {
  DCHECK_EQ(kSize32Bit, value.size());

  if (value.reference() != NULL) {
    DCHECK_GT(arraysize(references_), num_references_);
    reference_offsets_[num_references_] = len();
    references_[num_references_] = value.reference();
  }

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

void AssemblerImpl::InstructionBuffer::EmitByte(uint8 byte) {
  DCHECK_GT(sizeof(buf_), len_);
  buf_[len_++] = byte;
}

AssemblerImpl::AssemblerImpl(uint32 location, InstructionSerializer* serializer)
    : location_(location), serializer_(serializer) {
  DCHECK(serializer != NULL);
}

void AssemblerImpl::call(const ImmediateImpl& dst) {
  DCHECK_EQ(kSize32Bit, dst.size());
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0xE8);
  instr.Emit32BitPCRelative(location_, dst);

  Output(instr);
}

void AssemblerImpl::call(const OperandImpl& dst) {
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0xFF);
  EncodeOperand(0x2, dst, &instr);

  Output(instr);
}

void AssemblerImpl::jmp(const ImmediateImpl& dst) {
  DCHECK_EQ(kSize32Bit, dst.size());
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0xE9);
  instr.Emit32BitPCRelative(location_, dst);

  Output(instr);
}

void AssemblerImpl::jmp(const OperandImpl& dst) {
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0xFF);
  EncodeOperand(0x4, dst, &instr);

  Output(instr);
}

void AssemblerImpl::ret() {
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0xC3);

  Output(instr);
}

void AssemblerImpl::ret(uint16 n) {
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0xC2);
  instr.Emit16BitValue(n);

  Output(instr);
}

void AssemblerImpl::mov(Register dst, Register src) {
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0x8B);
  instr.EmitModRMByte(Reg1, dst.code(), src.code());

  Output(instr);
}

void AssemblerImpl::mov(Register dst, const OperandImpl& src) {
  InstructionBuffer instr;

  if (dst.code() == kRegisterEax && IsDisplacementOnly(src)) {
    // Special encoding for indirect displacement only to EAX.
    instr.EmitOpCodeByte(0xA1);
    instr.Emit32BitDisplacement(src.displacement());
  } else {
    instr.EmitOpCodeByte(0x8B);
    EncodeOperand(dst.code(), src, &instr);
  }

  Output(instr);
}

void AssemblerImpl::mov(const OperandImpl& dst, Register src) {
  InstructionBuffer instr;

  if (src.code() == kRegisterEax && IsDisplacementOnly(dst)) {
    // Special encoding for indirect displacement only from EAX.
    instr.EmitOpCodeByte(0xA3);
    instr.Emit32BitDisplacement(dst.displacement());
  } else {
    instr.EmitOpCodeByte(0x89);
    EncodeOperand(src.code(), dst, &instr);
  }

  Output(instr);
}

void AssemblerImpl::mov(Register dst, const ValueImpl& src) {
  DCHECK_NE(kSizeNone, src.size());
  InstructionBuffer instr;

  instr.EmitOpCodeByte(0xB8 | dst.code());
  instr.Emit32BitDisplacement(src);

  Output(instr);
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

void AssemblerImpl::EncodeOperand(uint8 reg_op,
                                  const OperandImpl& op,
                                  InstructionBuffer* instr) {
  DCHECK_GE(8, reg_op);
  DCHECK(instr != NULL);

  // The op operand can encode any one of the following things:
  // An indirect register access [EAX].
  // An indirect 32 bit displacement only [0xDEADBEEF].
  // An indirect base register + 32/8 bit displacement [EAX+0xDEADBEEF].
  // An indirect base + index register*scale [EAX+ECX*4].
  // An indirect base + index register*scale + 32/8 bit displacement
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
      instr->EmitModRMByte(Reg1Ind, reg_op, kRegisterEbp);
      instr->Emit32BitDisplacement(op.displacement());
    } else {
      // Base register only, is it ESP?
      if (op.base() == kRegisterEsp) {
        // The [ESP] and [ESP+disp] cases cannot be encoded without a SIB byte.
        if (op.displacement().size() == kSizeNone) {
          instr->EmitModRMByte(Reg1Ind, reg_op, kRegisterEsp);
          instr->EmitScaleIndexBaseByte(kTimes1, kRegisterEsp, kRegisterEsp);
        } else if (op.displacement().size() == kSize8Bit) {
          instr->EmitModRMByte(Reg1ByteDisp, reg_op, kRegisterEsp);
          instr->EmitScaleIndexBaseByte(kTimes1, kRegisterEsp, kRegisterEsp);
          instr->Emit8BitDisplacement(op.displacement());
        } else {
          DCHECK_EQ(kSize32Bit, op.displacement().size());
          instr->EmitModRMByte(Reg1WordDisp, reg_op, kRegisterEsp);
          instr->EmitScaleIndexBaseByte(kTimes1, kRegisterEsp, kRegisterEsp);
          instr->Emit32BitDisplacement(op.displacement());
        }
      } else if (op.displacement().size() == kSizeNone) {
        if (op.base() == kRegisterEbp) {
          // The [EBP] case cannot be encoded canonically, there always must
          // be a (zero) displacement.
          instr->EmitModRMByte(Reg1ByteDisp, reg_op, op.base());
          instr->Emit8BitDisplacement(DisplacementImpl(0, kSize8Bit, NULL));
        } else {
          instr->EmitModRMByte(Reg1Ind, reg_op, op.base());
        }
      } else if (op.displacement().size() == kSize8Bit) {
        // It's [base+disp8], or possibly [EBP].
        instr->EmitModRMByte(Reg1ByteDisp, reg_op, op.base());
        instr->Emit8BitDisplacement(op.displacement());
      } else {
        DCHECK_EQ(kSize32Bit, op.displacement().size());
        // It's [base+disp32].
        instr->EmitModRMByte(Reg1WordDisp, reg_op, op.base());
        instr->Emit32BitDisplacement(op.displacement());
      }
    }
  } else {
    // There must be a base register as well.
    DCHECK_NE(kRegisterNone, op.base());

    // Is there a displacement?
    if (op.displacement().size() == kSizeNone) {
      instr->EmitModRMByte(Reg1Ind, reg_op, kRegisterEsp);
      instr->EmitScaleIndexBaseByte(op.scale(), op.index(), op.base());
    } else if (op.displacement().size() == kSize8Bit) {
      instr->EmitModRMByte(Reg1ByteDisp, reg_op, kRegisterEsp);
      instr->EmitScaleIndexBaseByte(op.scale(), op.index(), op.base());
      instr->Emit8BitDisplacement(op.displacement());
    } else {
      DCHECK_EQ(kSize32Bit, op.displacement().size());
      instr->EmitModRMByte(Reg1WordDisp, reg_op, kRegisterEsp);
      instr->EmitScaleIndexBaseByte(op.scale(), op.index(), op.base());
      instr->Emit32BitDisplacement(op.displacement());
    }
  }
}

}  // namespace core
