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

bool Is8Bit(uint32 value) {
  return (value & 0xFFFFFF00) == 0;
}

// Returns true if @p operand is a displacment only - e.g.
// specifies neither a base, nor an index register.
bool IsDisplacementOnly(const OperandImpl& operand) {
  return operand.displacement() != 0 &&
      operand.base() == kRegisterNone &&
      operand.index() == kRegisterNone;
}

}  // namespace

OperandImpl::OperandImpl(Register base)
    : base_(base.code()),
      index_(kRegisterNone),
      scale_(times_1),
      displacement_(0),
      displacement_ref_(NULL) {
}

OperandImpl::OperandImpl(Register base,
                         uint32 displacement,
                         const void* displacement_ref)
    : base_(base.code()),
      index_(kRegisterNone),
      scale_(times_1),
      displacement_(displacement),
      displacement_ref_(displacement_ref) {
  // There must be a base register.
  DCHECK_NE(base_, kRegisterNone);
}

OperandImpl::OperandImpl(uint32 displacement,
                         const void* displacement_ref)
    : base_(kRegisterNone),
      index_(kRegisterNone),
      scale_(times_1),
      displacement_(displacement),
      displacement_ref_(displacement_ref) {
}

OperandImpl::OperandImpl(Register base,
                         Register index,
                         ScaleFactor scale,
                         uint32 displacement,
                         void* displacement_ref)
    : base_(base.code()),
      index_(index.code()),
      scale_(scale),
      displacement_(displacement),
      displacement_ref_(displacement_ref) {
  // ESP cannot be used as an index register.
  DCHECK_NE(index.code(), kRegisterEsp);
}

ImmediateImpl::ImmediateImpl(uint32 imm) : imm_(imm), imm_ref_(NULL) {
}

ImmediateImpl::ImmediateImpl(
    uint32 imm, const void* imm_ref) : imm_(imm), imm_ref_(imm_ref) {
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
  void opcode(uint8 opcode);
  // Emit a ModR/M byte.
  void modrm(Mod mod, RegisterCode reg2, RegisterCode reg1);
  // Emit a SIB byte.
  void sib(ScaleFactor scale, RegisterCode index, RegisterCode base);

  // Emit an 8 bit displacement, with optional reference info.
  void disp8(uint8 disp, const void* reference);

  // Emit a 32 bit displacement with optional reference info.
  void disp32(uint32 disp, const void* reference);

 protected:
  void emit_b(uint8 byte);

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

void AssemblerImpl::InstructionBuffer::opcode(uint8 opcode) {
  emit_b(opcode);
}

void AssemblerImpl::InstructionBuffer::modrm(Mod mod,
                                             RegisterCode reg2,
                                             RegisterCode reg1) {
  DCHECK_NE(reg2, kRegisterNone);
  DCHECK_NE(reg1, kRegisterNone);

  emit_b((mod << 6) | (reg2 << 3) | reg1);
}

void AssemblerImpl::InstructionBuffer::sib(ScaleFactor scale,
                                           RegisterCode index,
                                           RegisterCode base) {
  DCHECK_NE(index, kRegisterNone);
  DCHECK_NE(base, kRegisterNone);

  emit_b((scale << 6) | (index << 3) | base);
}

void AssemblerImpl::InstructionBuffer::disp8(uint8 disp,
                                             const void* reference) {
  DCHECK(num_references_ < arraysize(references_));
  reference_offsets_[num_references_] = len();
  references_[num_references_] = reference;
  emit_b(disp);
}

void AssemblerImpl::InstructionBuffer::disp32(uint32 disp,
                                              const void* reference) {
  DCHECK(num_references_ < arraysize(references_));
  reference_offsets_[num_references_] = len();
  references_[num_references_] = reference;
  emit_b(disp);
  emit_b(disp >> 8);
  emit_b(disp >> 16);
  emit_b(disp >> 24);
}

void AssemblerImpl::InstructionBuffer::emit_b(uint8 byte) {
  DCHECK(len_ < sizeof(buf_));
  buf_[len_++] = byte;
}

AssemblerImpl::AssemblerImpl(uint32 location, InstructionSerializer* serializer)
    : location_(location), serializer_(serializer) {
  DCHECK(serializer != NULL);
}

void AssemblerImpl::mov(Register dst, Register src) {
  InstructionBuffer instr;

  instr.opcode(0x8B);
  instr.modrm(Reg1, dst.code(), src.code());

  Output(instr);
}

void AssemblerImpl::mov(Register dst, const OperandImpl& src) {
  InstructionBuffer instr;

  if (dst.code() == kRegisterEax && IsDisplacementOnly(src)) {
    // Special encoding for indirect displacement only to EAX.
    instr.opcode(0xA1);
    instr.disp32(src.displacement(), src.displacement_ref());
  } else {
    instr.opcode(0x8B);
    EncodeOperands(dst, src, &instr);
  }

  Output(instr);
}

void AssemblerImpl::mov(const OperandImpl& dst, Register src) {
  InstructionBuffer instr;

  if (src.code() == kRegisterEax && IsDisplacementOnly(dst)) {
    // Special encoding for indirect displacement only from EAX.
    instr.opcode(0xA3);
    instr.disp32(dst.displacement(), dst.displacement_ref());
  } else {
    instr.opcode(0x89);
    EncodeOperands(src, dst, &instr);
  }

  Output(instr);
}

void AssemblerImpl::mov(Register dst, const ImmediateImpl& src) {
  InstructionBuffer instr;

  instr.opcode(0xB8 | dst.code());
  instr.disp32(src.imm(), src.imm_ref());

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

void AssemblerImpl::EncodeOperands(Register op1,
                                   const OperandImpl& op2,
                                   InstructionBuffer* instr) {
  DCHECK(instr != NULL);

  // The op2 operand can encode any one of the following things:
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
  DCHECK_NE(op2.index(), kRegisterEsp);

  // Is there an index register?
  if (op2.index() == kRegisterNone) {
    DCHECK_EQ(op2.scale(), times_1);

    // No index register, is there a base register?
    if (op2.base() == kRegisterNone) {
      // No base register, this is a displacement only.
      DCHECK_NE(op2.displacement(), 0U);
      DCHECK_EQ(op2.scale(), times_1);

      // The [disp32] mode is encoded by overloading [EBP].
      instr->modrm(Reg1Ind, op1.code(), kRegisterEbp);
      instr->disp32(op2.displacement(), op2.displacement_ref());
    } else {
      // Base register only, is it ESP?
      if (op2.base() == kRegisterEsp) {
        // The [ESP] and [ESP+disp] cases cannot be encoded without a SIB byte.
        if (op2.displacement() == 0) {
          instr->modrm(Reg1Ind, op1.code(), kRegisterEsp);
          instr->sib(times_1, kRegisterEsp, kRegisterEsp);
        } else if (Is8Bit(op2.displacement())) {
          instr->modrm(Reg1ByteDisp, op1.code(), kRegisterEsp);
          instr->sib(times_1, kRegisterEsp, kRegisterEsp);
          instr->disp8(op2.displacement(), op2.displacement_ref());
        } else {
          instr->modrm(Reg1WordDisp, op1.code(), kRegisterEsp);
          instr->sib(times_1, kRegisterEsp, kRegisterEsp);
          instr->disp32(op2.displacement(), op2.displacement_ref());
        }
      } else if (op2.displacement() == 0 && op2.base() != kRegisterEbp) {
        // The [EBP] case cannot be encoded canonically, there always must
        // be a (zero) displacement.
        DCHECK_NE(op2.base(), kRegisterEbp);
        instr->modrm(Reg1Ind, op1.code(), op2.base());
      } else if (Is8Bit(op2.displacement())) {
        // It's [base+disp8], or possibly [EBP].
        instr->modrm(Reg1ByteDisp, op1.code(), op2.base());
        instr->disp8(op2.displacement(), op2.displacement_ref());
      } else {
        // It's [base+disp32].
        instr->modrm(Reg1WordDisp, op1.code(), op2.base());
        instr->disp32(op2.displacement(), op2.displacement_ref());
      }
    }
  } else {
    // There must be a base register as well.
    DCHECK_NE(op2.base(), kRegisterNone);

    // Is there a displacement?
    if (op2.displacement() == 0) {
      instr->modrm(Reg1Ind, op1.code(), kRegisterEsp);
      instr->sib(op2.scale(), op2.index(), op2.base());
    } else if (Is8Bit(op2.displacement())) {
      instr->modrm(Reg1ByteDisp, op1.code(), kRegisterEsp);
      instr->sib(op2.scale(), op2.index(), op2.base());
      instr->disp8(op2.displacement(), op2.displacement_ref());
    } else {
      instr->modrm(Reg1WordDisp, op1.code(), kRegisterEsp);
      instr->sib(op2.scale(), op2.index(), op2.base());
      instr->disp32(op2.displacement(), op2.displacement_ref());
    }
  }
}

}  // namespace core
