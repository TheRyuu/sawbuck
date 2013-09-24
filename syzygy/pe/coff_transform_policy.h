// Copyright 2013 Google Inc. All Rights Reserved.
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
// Declares the COFF-specific transform policy object. This guides decisions
// made during image decomposition, basic-block decomposition, transforms and
// block building.

#ifndef SYZYGY_PE_COFF_TRANSFORM_POLICY_H_
#define SYZYGY_PE_COFF_TRANSFORM_POLICY_H_

#include "syzygy/block_graph/transform_policy.h"

namespace pe {

// The interface that guides image and basic-block transfom decisions for COFF
// files.
class CoffTransformPolicy : public block_graph::TransformPolicyInterface {
 public:
  virtual ~CoffTransformPolicy() { }

  // @name TransformPolicyInterface implementation
  // @{
  virtual bool CodeBlockAttributesAreBasicBlockSafe(
      const BlockGraph::Block* code_block) const OVERRIDE;
  virtual bool CodeBlockIsSafeToBasicBlockDecompose(
      const BlockGraph::Block* code_block) const OVERRIDE;
  virtual bool ReferenceIsSafeToRedirect(
      const BlockGraph::Block* referrer,
      const BlockGraph::Reference& reference) const OVERRIDE;
  // @}
};

}  // namespace pe

#endif  // SYZYGY_PE_COFF_TRANSFORM_POLICY_H_
