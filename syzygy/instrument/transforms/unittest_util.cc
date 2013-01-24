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
// Add basic block frequency transform unittests.

#include "syzygy/instrument/transforms/unittest_util.h"

#include "syzygy/pe/decomposer.h"

namespace testing {

TestDllTransformTest::TestDllTransformTest() : dos_header_block_(NULL) {
}

void TestDllTransformTest::DecomposeTestDll() {
  FilePath test_dll_path = ::testing::GetOutputRelativePath(kDllName);

  ASSERT_TRUE(pe_file_.Init(test_dll_path));

  pe::ImageLayout layout(&block_graph_);
  pe::Decomposer decomposer(pe_file_);
  ASSERT_TRUE(decomposer.Decompose(&layout));

  dos_header_block_ = layout.blocks.GetBlockByAddress(
      core::RelativeAddress(0));
  ASSERT_TRUE(dos_header_block_ != NULL);
}

}  // namespace
