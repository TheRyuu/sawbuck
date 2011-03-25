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
//
// Implementation of disassembler.
#include "syzygy/core/disassembler.h"

#include "base/logging.h"

namespace core {

Disassembler::Disassembler(const uint8* code,
                           size_t code_size,
                           AbsoluteAddress code_addr,
                           InstructionCallback* on_instruction)
    : code_(code),
      code_size_(code_size),
      code_addr_(code_addr),
      on_instruction_(on_instruction),
      disassembled_bytes_(0) {
}

Disassembler::WalkResult Disassembler::Walk() {
  // Initialize our disassembly state.
  _CodeInfo code = {};
  code.dt = Decode32Bits;
  code.features = DF_NONE;

  // These are to keep track of whether we cover the entire function.
  bool incomplete_branches = false;
  while (!unvisited_.empty()) {
    AddressSet::iterator it = unvisited_.begin();
    AbsoluteAddress addr(*it);
    unvisited_.erase(it);

    // This continues disassembly along a contiguous path until we run out
    // of code, jump somewhere else, or are requested to terminate the path
    // by the OnInstruction callback.
    bool terminate = false;
    _DInst inst = {};
    for (; addr != AbsoluteAddress(0) && !terminate; addr += inst.size) {
      // Check whether it's data, we don't want to disassemble into data.
      // If not data, tag it as visited, making sure we don't re-traverse
      // if we've visited this address since it was marked unvisited.
      bool is_data = data_locations_.find(addr) != data_locations_.end();
      if (!is_data && visited_.insert(addr).second) {
        // Ok it's not already visited, let's walk this instruction.
        code.codeOffset = addr.value();
        code.codeLen = code_size_ - (addr - code_addr_);
        code.code = code_ + (addr - code_addr_);
        if (code.codeLen == 0)
          break;

        unsigned int decoded = 0;
        _DecodeResult result = distorm_decompose(&code, &inst, 1, &decoded);
        DCHECK_EQ(1U, decoded);
        DCHECK(result == DECRES_MEMORYERR || result == DECRES_SUCCESS);

        // Tally the code bytes we just disassembled.
        disassembled_bytes_ += inst.size;

        // Invoke the callback and terminate if need be
        switch (OnInstruction(inst)) {
          case kDirectiveTerminateWalk:
            return kWalkTerminated;

          case kDirectiveTerminatePath:
            terminate = true;

          default:
            break;
        }

        // Check the arguments for data references to the function.
        for (size_t i = 0; i < arraysize(inst.ops); ++i) {
          switch (inst.ops[i].type) {
            case O_DISP:
            case O_SMEM:
            case O_MEM: {
                // It's an absolute memory reference, see whether it's within
                // our function, and tag it as data if so.
                // TODO(siggi): It would be ideal to have a stronger signal
                //    here. As-is there's a (small) chance for false positives
                //    as e.g. instructions that fetch argument off the stack
                //    instructions fall in this general category with e.g.
                //      mov eax, [esp + 4]
                //    though the displacement is typically low. In the case
                //    of PE binaries we can identify absolute pointers to the
                //    image by the fact that there's a relocation entry to
                //    each such absolute displacement.
                AbsoluteAddress referenced(static_cast<uint32>(inst.disp));
                if (IsInCode(referenced, 1))
                  data_locations_.insert(referenced);
              }
              break;

            default:
              break;
          }
        }

        uint8 fc = META_GET_FC(inst.meta);
        switch (fc) {
          case FC_NONE:
          case FC_CALL:
            break;

          case FC_RET:
            // It's a RET instruction, we're done with this branch.
            terminate = true;
            break;

          case FC_SYS:
            incomplete_branches = true;
            terminate = true;
            NOTREACHED() << "Unexpected SYS* instruction encountered";
            break;

          case FC_BRANCH:
            // Unconditional branch, stop here.
            terminate = true;
            // And fall through to visit branch target.

          case FC_COND_BRANCH: {
              AbsoluteAddress dest;
              bool handled = false;
              switch (inst.ops[0].type) {
                case O_REG:
                case O_MEM:
                  // Computed branch, we can't chase this.
                  break;

                case O_SMEM:
                  // Branch to a register, can't chase this.
                  break;

                case O_DISP:
                  // Indirect address, this may be e.g. a jump to an import.
                  // TODO(siggi): validate that this is so.
                  DCHECK_EQ(32, inst.ops[0].size);
                  break;

                case O_PC:
                  // PC relative address.
                  dest = addr + static_cast<size_t>(inst.size + inst.imm.addr);
                  handled = true;
                  break;

                default:
                  NOTREACHED() << "Unexpected branch destination type";
                  break;
              }

              // Make sure to visit the branch destination.
              if (dest != AbsoluteAddress(0)) {
                if (IsInCode(dest, 1))
                  Unvisited(dest);
              } else {
                // We couldn't compute the destination, if not handled,
                // we may have incomplete coverage for the function.
                incomplete_branches = incomplete_branches || !handled;
              }
            }
            break;

          case FC_INT:
            // We encounter int3 inline in functions sometimes.
            break;

          default:
            NOTREACHED() << "Unexpected instruction type encountered";
            terminate = true;
            break;
        }
      } else {
        if (is_data)
          LOG(INFO) << "Not disassembling data at " << addr;

        terminate = true;
      }
    }
  }

  // If we covered every byte in the function, we don't
  // care that we didn't chase all computed branches.
  if (incomplete_branches && disassembled_bytes_ == code_size_)
    return kWalkSuccess;

  // Otherwise we return success only in case of no computed branches.
  return incomplete_branches ? kWalkIncomplete : kWalkSuccess;
}

bool Disassembler::Unvisited(AbsoluteAddress addr) {
  DCHECK(IsInCode(addr, 1));
  if (visited_.find(addr) != visited_.end())
    return false;
  if (data_locations_.find(addr) != data_locations_.end())
    return false;

  return unvisited_.insert(addr).second;
}

Disassembler::CallbackDirective Disassembler::OnInstruction(
    const _DInst& inst) {
  if (on_instruction_) {
    CallbackDirective directive = kDirectiveContinue;
    on_instruction_->Run(*this, inst, &directive);
    return directive;
  }

  return kDirectiveContinue;
}

bool Disassembler::IsInCode(AbsoluteAddress addr, size_t len) const {
  return addr >= code_addr_ &&
      static_cast<size_t>(addr - code_addr_) + len <= code_size_;
}

}  // namespace image_util
