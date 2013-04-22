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
// Implementation of the SyzyAsan instrumentation transform.

#ifndef SYZYGY_INSTRUMENT_TRANSFORMS_ASAN_TRANSFORM_H_
#define SYZYGY_INSTRUMENT_TRANSFORMS_ASAN_TRANSFORM_H_

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/string_piece.h"
#include "syzygy/block_graph/filterable.h"
#include "syzygy/block_graph/iterate.h"
#include "syzygy/block_graph/analysis/liveness_analysis.h"
#include "syzygy/block_graph/transforms/iterative_transform.h"
#include "syzygy/block_graph/transforms/named_transform.h"

namespace instrument {
namespace transforms {

// This class implements the transformation applied to each basic block.
class AsanBasicBlockTransform
    : public block_graph::transforms::NamedBasicBlockSubGraphTransformImpl<
          AsanBasicBlockTransform>,
      public block_graph::Filterable {
 public:
  // Represent the different kind of access to the memory.
  enum MemoryAccessMode {
    kNoAccess,
    kReadAccess,
    kWriteAccess,
    kInstrAccess,
    kRepzAccess,
    kRepnzAccess,
  };

  enum StackAccessMode {
    kUnsafeStackAccess,
    kSafeStackAccess,
  };

  // Contains memory access information.
  struct MemoryAccessInfo {
    MemoryAccessMode mode;
    uint8_t size;
    uint16_t opcode;
  };

  typedef block_graph::BlockGraph BlockGraph;
  typedef block_graph::BasicBlockSubGraph BasicBlockSubGraph;
  typedef MemoryAccessInfo AsanHookMapEntryKey;
  // Map of hooks to asan check access functions.
  typedef std::map<AsanHookMapEntryKey, BlockGraph::Reference> AsanHookMap;
  typedef std::map<MemoryAccessMode, BlockGraph::Reference> AsanDefaultHookMap;

  // Constructor.
  // @param hooks_read_access a reference to the read access check import entry.
  // @param hooks_write_access a reference to the write access check import
  //     entry.
  explicit AsanBasicBlockTransform(AsanHookMap* check_access_hooks) :
      check_access_hooks_(check_access_hooks), debug_friendly_(false) {
    DCHECK(check_access_hooks != NULL);
  }

  // @name Accessors.
  // @{
  bool debug_friendly() const {
    return debug_friendly_;
  }

  void set_debug_friendly(bool flag) {
    debug_friendly_ = flag;
  }
  // @}

  // The transform name.
  static const char kTransformName[];

 protected:
  // @name BasicBlockSubGraphTransformInterface method.
  virtual bool TransformBasicBlockSubGraph(
      BlockGraph* block_graph,
      BasicBlockSubGraph* basic_block_subgraph) OVERRIDE;

  // Instruments the memory accesses in a basic block.
  // @param basic_block The basic block to be instrumented.
  // @param stack_mode Give some assumptions to the transformation on stack
  //     frame manipulations inside @p basic_block. The transformation assume a
  //     standard calling convention, unless specified by this parameter.
  //     (note: Unsafe blocks may be produced with the compiler flag
  //     frame-pointer-omission).
  // @returns true on success, false otherwise.
  bool InstrumentBasicBlock(block_graph::BasicCodeBlock* basic_block,
                            StackAccessMode stack_mode);

 private:
  // Liveness analysis and liveness information for this subgraph.
  block_graph::analysis::LivenessAnalysis liveness_;

  // The references to the Asan access check import entries.
  AsanHookMap* check_access_hooks_;

  // Activate the overwriting of source range for created instructions.
  bool debug_friendly_;

  DISALLOW_COPY_AND_ASSIGN(AsanBasicBlockTransform);
};

class AsanTransform
    : public block_graph::transforms::IterativeTransformImpl<AsanTransform>,
      public block_graph::Filterable {
 public:
  typedef block_graph::BlockGraph BlockGraph;
  typedef AsanBasicBlockTransform::MemoryAccessInfo MemoryAccessInfo;
  typedef AsanBasicBlockTransform::MemoryAccessMode MemoryAccessMode;

  // Initialize a new AsanTransform instance.
  AsanTransform();

  // @name IterativeTransformImpl implementation.
  // @{
  bool PreBlockGraphIteration(BlockGraph* block_graph,
                              BlockGraph::Block* header_block);
  bool OnBlock(BlockGraph* block_graph, BlockGraph::Block* block);
  bool PostBlockGraphIteration(BlockGraph* block_graph,
                               BlockGraph::Block* header_block);
  // @}

  // @name Accessors.
  // @{
  void set_instrument_dll_name(const base::StringPiece& instrument_dll_name) {
    instrument_dll_name.CopyToString(&asan_dll_name_);
  }
  const char* instrument_dll_name() const {
    return asan_dll_name_.c_str();
  }

  bool debug_friendly() const {
    return debug_friendly_;
  }

  void set_debug_friendly(bool flag) {
    debug_friendly_ = flag;
  }
  // @}

  // The name of the DLL that is imported by default.
  static const char kSyzyAsanDll[];

  // The transform name.
  static const char kTransformName[];

  // The hooks stub name.
  static const char kAsanHookStubName[];

 protected:
  // Name of the asan_rtl DLL we import. Defaults to "asan_rtl.dll".
  std::string asan_dll_name_;

  // Activate the overwriting of source range for created instructions.
  bool debug_friendly_;

  // References to the different asan check access import entries. Valid after
  // successful PreBlockGraphIteration.
  AsanBasicBlockTransform::AsanHookMap check_access_hooks_ref_;

  DISALLOW_COPY_AND_ASSIGN(AsanTransform);
};

bool operator<(const AsanBasicBlockTransform::MemoryAccessInfo& left,
               const AsanBasicBlockTransform::MemoryAccessInfo& right);

}  // namespace transforms
}  // namespace instrument

#endif  // SYZYGY_INSTRUMENT_TRANSFORMS_ASAN_TRANSFORM_H_
