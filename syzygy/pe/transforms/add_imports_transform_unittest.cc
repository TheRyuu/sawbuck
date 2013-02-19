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

#include "syzygy/pe/transforms/add_imports_transform.h"

#include "gtest/gtest.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pe/decomposer.h"
#include "syzygy/pe/pe_utils.h"
#include "syzygy/pe/unittest_util.h"

namespace pe {
namespace transforms {

using block_graph::BlockGraph;
using core::RelativeAddress;
typedef AddImportsTransform::ImportedModule ImportedModule;

namespace {

class AddImportsTransformTest : public testing::PELibUnitTest {
 public:
  AddImportsTransformTest() : image_layout_(&block_graph_) {
  }

  virtual void SetUp() {
    FilePath image_path(testing::GetExeRelativePath(testing::kTestDllName));

    ASSERT_TRUE(pe_file_.Init(image_path));

    // Decompose the test image and look at the result.
    Decomposer decomposer(pe_file_);
    ASSERT_TRUE(decomposer.Decompose(&image_layout_));

    // Retrieve and validate the DOS header.
    dos_header_block_ =
        image_layout_.blocks.GetBlockByAddress(RelativeAddress(0));
    ASSERT_TRUE(dos_header_block_ != NULL);
    ASSERT_TRUE(IsValidDosHeaderBlock(dos_header_block_));
  }

  PEFile pe_file_;
  BlockGraph block_graph_;
  ImageLayout image_layout_;
  BlockGraph::Block* dos_header_block_;
};

// Given an ImportedModule tests that all of its symbols have been properly
// processed.
void TestSymbols(const ImportedModule& module) {
  for (size_t i = 0; i < module.size(); ++i) {
    BlockGraph::Reference ref;
    EXPECT_TRUE(module.GetSymbolReference(i, &ref));
    EXPECT_TRUE(ref.referenced() != NULL);
    EXPECT_GE(ref.offset(), 0);
    EXPECT_LT(ref.offset(),
              static_cast<BlockGraph::Offset>(ref.referenced()->size()));
  }
}

}  // namespace

TEST_F(AddImportsTransformTest, AddImportsExisting) {
  ImportedModule module("export_dll.dll");
  size_t function1 = module.AddSymbol("function1",
                                      ImportedModule::kAlwaysImport);
  size_t function3 = module.AddSymbol("function3",
                                      ImportedModule::kAlwaysImport);
  EXPECT_EQ("function1", module.GetSymbolName(function1));
  EXPECT_EQ("function3", module.GetSymbolName(function3));
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.mode());
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.GetSymbolMode(function1));
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.GetSymbolMode(function3));

  AddImportsTransform transform;
  transform.AddModule(&module);
  EXPECT_TRUE(block_graph::ApplyBlockGraphTransform(
      &transform, &block_graph_, dos_header_block_));
  EXPECT_EQ(0u, transform.modules_added());
  EXPECT_EQ(0u, transform.symbols_added());

  EXPECT_TRUE(module.ModuleIsImported());
  EXPECT_TRUE(module.SymbolIsImported(function1));
  EXPECT_TRUE(module.SymbolIsImported(function3));

  EXPECT_FALSE(module.ModuleWasAdded());
  EXPECT_FALSE(module.SymbolWasAdded(function1));
  EXPECT_FALSE(module.SymbolWasAdded(function3));

  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function1));
  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function3));

  EXPECT_NO_FATAL_FAILURE(TestSymbols(module));
}

TEST_F(AddImportsTransformTest, AddImportsNewSymbol) {
  ImportedModule module("export_dll.dll");
  size_t function1 = module.AddSymbol("function1",
                                      ImportedModule::kAlwaysImport);
  size_t function3 = module.AddSymbol("function3",
                                      ImportedModule::kAlwaysImport);
  size_t function4 = module.AddSymbol("function4",
                                      ImportedModule::kAlwaysImport);
  EXPECT_EQ("function1", module.GetSymbolName(function1));
  EXPECT_EQ("function3", module.GetSymbolName(function3));
  EXPECT_EQ("function4", module.GetSymbolName(function4));
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.mode());
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.GetSymbolMode(function1));
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.GetSymbolMode(function3));
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.GetSymbolMode(function4));

  EXPECT_TRUE(module.import_descriptor().block() == NULL);

  AddImportsTransform transform;
  transform.AddModule(&module);
  EXPECT_TRUE(block_graph::ApplyBlockGraphTransform(
      &transform, &block_graph_, dos_header_block_));
  EXPECT_EQ(0u, transform.modules_added());
  EXPECT_EQ(1u, transform.symbols_added());

  EXPECT_TRUE(module.import_descriptor().block() != NULL);

  EXPECT_TRUE(module.ModuleIsImported());
  EXPECT_TRUE(module.SymbolIsImported(function1));
  EXPECT_TRUE(module.SymbolIsImported(function3));
  EXPECT_TRUE(module.SymbolIsImported(function4));

  EXPECT_FALSE(module.ModuleWasAdded());
  EXPECT_FALSE(module.SymbolWasAdded(function1));
  EXPECT_FALSE(module.SymbolWasAdded(function3));
  EXPECT_TRUE(module.SymbolWasAdded(function4));

  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function1));
  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function3));
  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function4));

  EXPECT_NO_FATAL_FAILURE(TestSymbols(module));

  // TODO(chrisha): Write the image and try to load it!
}

TEST_F(AddImportsTransformTest, AddImportsNewModule) {
  ImportedModule module("call_trace_client_rpc.dll");
  size_t indirect_penter = module.AddSymbol(
      "_indirect_penter", ImportedModule::kAlwaysImport);
  size_t indirect_penter_dllmain = module.AddSymbol(
      "_indirect_penter_dllmain", ImportedModule::kAlwaysImport);
  EXPECT_EQ("_indirect_penter",
            module.GetSymbolName(indirect_penter));
  EXPECT_EQ("_indirect_penter_dllmain",
            module.GetSymbolName(indirect_penter_dllmain));
  EXPECT_EQ(ImportedModule::kAlwaysImport, module.mode());
  EXPECT_EQ(ImportedModule::kAlwaysImport,
            module.GetSymbolMode(indirect_penter));
  EXPECT_EQ(ImportedModule::kAlwaysImport,
            module.GetSymbolMode(indirect_penter_dllmain));

  AddImportsTransform transform;
  transform.AddModule(&module);
  EXPECT_TRUE(block_graph::ApplyBlockGraphTransform(
      &transform, &block_graph_, dos_header_block_));
  EXPECT_EQ(1u, transform.modules_added());
  EXPECT_EQ(2u, transform.symbols_added());

  EXPECT_TRUE(module.ModuleIsImported());
  EXPECT_TRUE(module.SymbolIsImported(indirect_penter));
  EXPECT_TRUE(module.SymbolIsImported(indirect_penter_dllmain));

  EXPECT_TRUE(module.ModuleWasAdded());
  EXPECT_TRUE(module.SymbolWasAdded(indirect_penter));
  EXPECT_TRUE(module.SymbolWasAdded(indirect_penter_dllmain));

  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(indirect_penter));
  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(indirect_penter_dllmain));

  EXPECT_NO_FATAL_FAILURE(TestSymbols(module));

  // TODO(chrisha): Write the image and try to load it!
}

TEST_F(AddImportsTransformTest, FindImportsExisting) {
  ImportedModule module("export_dll.dll");
  size_t function1 = module.AddSymbol("function1", ImportedModule::kFindOnly);
  size_t function3 = module.AddSymbol("function3", ImportedModule::kFindOnly);
  EXPECT_EQ("function1", module.GetSymbolName(function1));
  EXPECT_EQ("function3", module.GetSymbolName(function3));
  EXPECT_EQ(ImportedModule::kFindOnly, module.mode());
  EXPECT_EQ(ImportedModule::kFindOnly, module.GetSymbolMode(function1));
  EXPECT_EQ(ImportedModule::kFindOnly, module.GetSymbolMode(function3));

  AddImportsTransform transform;
  transform.AddModule(&module);
  EXPECT_TRUE(block_graph::ApplyBlockGraphTransform(
      &transform, &block_graph_, dos_header_block_));
  EXPECT_EQ(0u, transform.modules_added());
  EXPECT_EQ(0u, transform.symbols_added());

  EXPECT_TRUE(module.ModuleIsImported());
  EXPECT_TRUE(module.SymbolIsImported(function1));
  EXPECT_TRUE(module.SymbolIsImported(function3));

  EXPECT_FALSE(module.ModuleWasAdded());
  EXPECT_FALSE(module.SymbolWasAdded(function1));
  EXPECT_FALSE(module.SymbolWasAdded(function3));

  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function1));
  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function3));
}

TEST_F(AddImportsTransformTest, FindImportsNewSymbol) {
  ImportedModule module("export_dll.dll");
  size_t function1 = module.AddSymbol("function1", ImportedModule::kFindOnly);
  size_t function3 = module.AddSymbol("function3", ImportedModule::kFindOnly);
  size_t function4 = module.AddSymbol("function4", ImportedModule::kFindOnly);
  EXPECT_EQ("function1", module.GetSymbolName(function1));
  EXPECT_EQ("function3", module.GetSymbolName(function3));
  EXPECT_EQ("function4", module.GetSymbolName(function4));
  EXPECT_EQ(ImportedModule::kFindOnly, module.mode());
  EXPECT_EQ(ImportedModule::kFindOnly, module.GetSymbolMode(function1));
  EXPECT_EQ(ImportedModule::kFindOnly, module.GetSymbolMode(function3));
  EXPECT_EQ(ImportedModule::kFindOnly, module.GetSymbolMode(function4));

  AddImportsTransform transform;
  transform.AddModule(&module);
  EXPECT_TRUE(block_graph::ApplyBlockGraphTransform(
      &transform, &block_graph_, dos_header_block_));
  EXPECT_EQ(0u, transform.modules_added());
  EXPECT_EQ(0u, transform.symbols_added());

  EXPECT_TRUE(module.ModuleIsImported());
  EXPECT_TRUE(module.SymbolIsImported(function1));
  EXPECT_TRUE(module.SymbolIsImported(function3));
  EXPECT_FALSE(module.SymbolIsImported(function4));

  EXPECT_FALSE(module.ModuleWasAdded());
  EXPECT_FALSE(module.SymbolWasAdded(function1));
  EXPECT_FALSE(module.SymbolWasAdded(function3));
  EXPECT_FALSE(module.SymbolWasAdded(function4));

  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function1));
  EXPECT_NE(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function3));
  EXPECT_EQ(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(function4));
}

TEST_F(AddImportsTransformTest, FindImportsNewModule) {
  ImportedModule module("call_trace_client_rpc.dll");
  size_t indirect_penter = module.AddSymbol(
      "_indirect_penter", ImportedModule::kFindOnly);
  size_t indirect_penter_dllmain = module.AddSymbol(
      "_indirect_penter_dllmain", ImportedModule::kFindOnly);
  EXPECT_EQ("_indirect_penter",
            module.GetSymbolName(indirect_penter));
  EXPECT_EQ("_indirect_penter_dllmain",
            module.GetSymbolName(indirect_penter_dllmain));
  EXPECT_EQ(ImportedModule::kFindOnly, module.mode());
  EXPECT_EQ(ImportedModule::kFindOnly, module.GetSymbolMode(indirect_penter));
  EXPECT_EQ(ImportedModule::kFindOnly,
            module.GetSymbolMode(indirect_penter_dllmain));

  AddImportsTransform transform;
  transform.AddModule(&module);
  EXPECT_TRUE(block_graph::ApplyBlockGraphTransform(
      &transform, &block_graph_, dos_header_block_));
  EXPECT_EQ(0u, transform.modules_added());
  EXPECT_EQ(0u, transform.symbols_added());

  EXPECT_FALSE(module.ModuleIsImported());
  EXPECT_FALSE(module.SymbolIsImported(indirect_penter));
  EXPECT_FALSE(module.SymbolIsImported(indirect_penter_dllmain));

  EXPECT_FALSE(module.ModuleWasAdded());
  EXPECT_FALSE(module.SymbolWasAdded(indirect_penter));
  EXPECT_FALSE(module.SymbolWasAdded(indirect_penter_dllmain));

  EXPECT_EQ(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(indirect_penter));
  EXPECT_EQ(ImportedModule::kInvalidIatIndex,
            module.GetSymbolIatIndex(indirect_penter_dllmain));
}

}  // namespace transforms
}  // namespace pe
