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

#include "syzygy/core/disassembler_util.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "mnemonics.h"  // NOLINT

namespace core {

_DecodeResult DistormDecompose(_CodeInfo* ci,
                               _DInst result[],
                               unsigned int max_instructions,
                               unsigned int* used_instructions_count) {
  _DecodeResult ret =
      distorm_decompose(ci, result, max_instructions, used_instructions_count);

  for (unsigned int i = 0; i < *used_instructions_count; ++i) {
    // Distorm @229 has a bug where the access size for I_FNSTCW and I_FLDCW
    // destination operand is 0 instead of 16. I've filed issue
    // http://code.google.com/p/distorm/issues/detail?id=58 to have this fixed.
    // In the meantime this is a workaround to have the correct operand size.
    switch (result[i].opcode) {
      case I_FNSTCW:
      case I_FLDCW:
        // If result[i].ops[0].size is not zero that means that distorm has been
        // fixed and that this workaround is not needed anymore.
        DCHECK(result[i].ops[0].size == 0);
        result[i].ops[0].size = 16;
        break;
      default:
        break;
    }
  }
  return ret;
}

bool DecodeOneInstruction(
    uint32 address, const uint8* buffer, size_t length, _DInst* instruction) {
  DCHECK(buffer != NULL);
  DCHECK(instruction != NULL);

  _CodeInfo code = {};
  code.dt = Decode32Bits;
  code.features = DF_NONE;
  code.codeOffset = address;
  code.codeLen = length;
  code.code = buffer;

  unsigned int decoded = 0;
  ::memset(instruction, 0, sizeof(instruction));
  _DecodeResult result = DistormDecompose(&code, instruction, 1, &decoded);

  if (result != DECRES_MEMORYERR && result != DECRES_SUCCESS)
    return false;

  DCHECK_EQ(1u, decoded);
  DCHECK_GE(length, instruction->size);
  DCHECK_LT(0, instruction->size);

  return true;
}

bool DecodeOneInstruction(
    const uint8* buffer, size_t length, _DInst* instruction) {
  DCHECK(buffer != NULL);
  DCHECK(instruction != NULL);
  if (!DecodeOneInstruction(0x10000000, buffer, length, instruction))
    return false;
  return true;
}

bool InstructionToString(
    const _DInst& instruction,
    const uint8_t* data,
    int code_length,
    std::string* buffer) {
  DCHECK(data != NULL);
  DCHECK(buffer != NULL);

  _CodeInfo code = {};
  code.codeOffset = 0;
  code.code = data;
  code.codeLen = code_length;
  code.dt = Decode32Bits;
  _DecodedInst decoded = {};
  _DInst dinst = instruction;

  dinst.addr = 0;
  distorm_format64(&code, &dinst, &decoded);

  *buffer = base::StringPrintf("%-14s %s %s",
                               decoded.instructionHex.p,
                               decoded.mnemonic.p,
                               decoded.operands.p);
  return true;
}

bool IsNop(const _DInst& instruction) {
  switch (instruction.opcode) {
    default:
      // Only the sequences recognized below qualify as NOP instructions.
      return false;

    case I_XCHG:
      // This handles the 1 bytes NOP sequence.
      //     1-byte: xchg eax, eax.
      return instruction.ops[0].type == O_REG &&
          instruction.ops[0].index == RM_AX &&
          instruction.ops[1].type == O_REG &&
          instruction.ops[1].index == RM_AX;

    case I_NOP:
      // This handles the 2, 4, 5, 7, 8 and 9 byte NOP sequences.
      //     2-byte: 66 NOP
      //     4-byte: NOP DWORD PTR [EAX + 0] (8-bit displacement)
      //     5-byte: NOP DWORD PTR [EAX + EAX*1 + 0] (8-bit displacement)
      //     7-byte: NOP DWORD PTR [EAX + 0] (32-bit displacement)
      //     8-byte: NOP DWORD PTR [EAX + EAX*1 + 0] (32-bit displacement)
      //     9-byte: NOP WORD PTR [EAX + EAX*1 + 0] (32-bit displacement)
      return true;

    case I_LEA:
      // This handles the 3 and 6 byte NOP sequences.
      //     3-byte: LEA REG, 0 (REG) (8-bit displacement)
      //     6-byte: LEA REG, 0 (REG) (32-bit displacement)
      return instruction.ops[0].type == O_REG &&
          instruction.ops[1].type == O_SMEM &&
          instruction.ops[0].index == instruction.ops[1].index &&
          instruction.disp == 0;

    case I_MOV:
      // Not documented in the Intel manuals, but we see "mov reg, reg" a lot.
      return instruction.ops[0].type == O_REG &&
          instruction.ops[1].type == O_REG &&
          instruction.ops[0].index == instruction.ops[1].index;
  }
}

bool IsCall(const _DInst& instruction) {
  return META_GET_FC(instruction.meta) == FC_CALL;
}

bool IsReturn(const _DInst& instruction) {
  return META_GET_FC(instruction.meta) == FC_RET;
}

bool IsSystemCall(const _DInst& instruction) {
  return META_GET_FC(instruction.meta) == FC_SYS;
}

bool IsConditionalBranch(const _DInst& instruction) {
  return META_GET_FC(instruction.meta) == FC_CND_BRANCH;
}

bool IsUnconditionalBranch(const _DInst& instruction) {
  return META_GET_FC(instruction.meta) == FC_UNC_BRANCH;
}

bool IsBranch(const _DInst& instruction) {
  return IsConditionalBranch(instruction) || IsUnconditionalBranch(instruction);
}

bool HasPcRelativeOperand(const _DInst& instruction, int operand_index) {
  DCHECK_LE(0, operand_index);
  DCHECK_LT(operand_index, static_cast<int>(arraysize(instruction.ops)));
  return instruction.ops[operand_index].type == O_PC;
}

bool IsControlFlow(const _DInst& instruction) {
  // For the purposes of Syzygy we include all of the control flow altering
  // instruction EXCEPT for call as true control flow.
  return IsBranch(instruction) ||
      IsReturn(instruction) ||
      IsSystemCall(instruction);
}

bool IsImplicitControlFlow(const _DInst& instruction) {
  // Control flow jumps implicitly out of the block for RET and SYS
  if (IsReturn(instruction) || IsSystemCall(instruction))
    return true;

  // Control flow is implicit for non PC-relative jumps (i.e., explicit
  // branches where the target is computed, stored in a register, stored
  // in a memory location, or otherwise indirect).
  if (IsUnconditionalBranch(instruction) &&
      !HasPcRelativeOperand(instruction, 0)) {
    return true;
  }

  // Otherwise it's not implicit control flow.
  return false;
}

bool IsInterrupt(const _DInst& instruction) {
  return META_GET_FC(instruction.meta) == FC_INT;
}

bool IsDebugInterrupt(const _DInst& instruction) {
  return IsInterrupt(instruction) && instruction.size == 1 &&
      instruction.opcode == I_INT_3;
}

}  // namespace core
