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
#include "syzygy/block_graph/analysis/memory_access_analysis.h"
#include "syzygy/block_graph/transforms/iterative_transform.h"
#include "syzygy/block_graph/transforms/named_transform.h"
#include "syzygy/pe/transforms/add_imports_transform.h"

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
    // True iff we need to save the flags for this access.
    bool save_flags;
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
      check_access_hooks_(check_access_hooks),
      debug_friendly_(false),
      use_liveness_analysis_(false),
      remove_redundant_checks_(false) {
    DCHECK(check_access_hooks != NULL);
  }

  // @name Accessors.
  // @{
  bool debug_friendly() const { return debug_friendly_; }
  void set_debug_friendly(bool flag) { debug_friendly_ = flag; }

  bool use_liveness_analysis() { return use_liveness_analysis_; }
  void set_use_liveness_analysis(bool use_liveness_analysis) {
    use_liveness_analysis_ = use_liveness_analysis;
  }

  bool remove_redundant_checks() const { return remove_redundant_checks_; }
  void set_remove_redundant_checks(bool remove_redundant_checks) {
    remove_redundant_checks_ = remove_redundant_checks;
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

  // Memory accesses value numbering.
  block_graph::analysis::MemoryAccessAnalysis memory_accesses_;

  // The references to the Asan access check import entries.
  AsanHookMap* check_access_hooks_;

  // Activate the overwriting of source range for created instructions.
  bool debug_friendly_;

  // Set iff we should use the liveness analysis to do smarter instrumentation.
  bool use_liveness_analysis_;

  // When activated, a redundancy elimination is performed to minimize the
  // memory checks added by this transform.
  bool remove_redundant_checks_;

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

  bool debug_friendly() const { return debug_friendly_; }
  void set_debug_friendly(bool flag) { debug_friendly_ = flag; }

  bool intercept_crt_functions() const { return intercept_crt_functions_; }
  void set_intercept_crt_functions(bool intercept_crt_functions) {
    intercept_crt_functions_ = intercept_crt_functions;
  }

  bool use_liveness_analysis() const { return use_liveness_analysis_; }
  void set_use_liveness_analysis(bool use_liveness_analysis) {
    use_liveness_analysis_ = use_liveness_analysis;
  }

  bool remove_redundant_checks() const { return remove_redundant_checks_; }
  void set_remove_redundant_checks(bool remove_redundant_checks) {
    remove_redundant_checks_ = remove_redundant_checks;
  }

  // @}

  // The name of the DLL that is imported by default.
  static const char kSyzyAsanDll[];

  // The transform name.
  static const char kTransformName[];

  // The hooks stub name.
  static const char kAsanHookStubName[];

 protected:
  // A structure containing the information that we need to intercept a
  // function.
  struct FunctionInterceptionInfo {
    // The function's block.
    BlockGraph::Block* function_block;
    // The index for the Asan hook for this function.
    size_t asan_symbol_index;

    FunctionInterceptionInfo() : function_block(NULL), asan_symbol_index(~0U) {
    }
  };

  typedef pe::transforms::AddImportsTransform::ImportedModule ImportedModule;
  typedef std::set<std::string> FunctionInterceptionSet;
  typedef std::map<std::string,
                   FunctionInterceptionInfo> FunctionInterceptionInfoMap;

  // Intercept the calls to the functions for which we want to check the
  // arguments (i.e. the CRT functions written in assembly) and thunk them. The
  // thunk will defer the call to the original function to its instrumented
  // version in asan_rtl.
  // @param import_module The module for which the imports should be added.
  // @param block_graph The block-graph to modify.
  // @param header_block The block containing the module's DOS header of this
  //     block-graph.
  // @param functions_set The list of the function that we want to intercept.
  // @returns true on success, false on error.
  bool InterceptFunctions(ImportedModule* import_module,
                          BlockGraph* block_graph,
                          BlockGraph::Block* header_block,
                          const FunctionInterceptionSet& functions_set);

  // Name of the asan_rtl DLL we import. Defaults to "asan_rtl.dll".
  std::string asan_dll_name_;

  // Activate the overwriting of source range for created instructions.
  bool debug_friendly_;

  // Set iff we should use the liveness analysis to do smarter instrumentation.
  bool use_liveness_analysis_;

  // When activated, a redundancy elimination is performed to minimize the
  // memory checks added by this transform.
  bool remove_redundant_checks_;

  // Set iff we should intercept the CRT functions.
  bool intercept_crt_functions_;

  // References to the different asan check access import entries. Valid after
  // successful PreBlockGraphIteration.
  AsanBasicBlockTransform::AsanHookMap check_access_hooks_ref_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AsanTransform);
};

bool operator<(const AsanBasicBlockTransform::MemoryAccessInfo& left,
               const AsanBasicBlockTransform::MemoryAccessInfo& right);

}  // namespace transforms
}  // namespace instrument

#endif  // SYZYGY_INSTRUMENT_TRANSFORMS_ASAN_TRANSFORM_H_
