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

#include "syzygy/pe/decomposer.h"

#include <set>

#include "base/file_util.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/block_graph/unittest_util.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pdb/pdb_byte_stream.h"
#include "syzygy/pdb/pdb_file.h"
#include "syzygy/pdb/pdb_reader.h"
#include "syzygy/pdb/pdb_stream.h"
#include "syzygy/pdb/pdb_util.h"
#include "syzygy/pdb/pdb_writer.h"
#include "syzygy/pe/pe_relinker.h"
#include "syzygy/pe/pe_utils.h"
#include "syzygy/pe/unittest_util.h"

namespace pe {

using block_graph::BlockGraph;
using core::RelativeAddress;

namespace {

// Exposes the protected methods for testing.
class TestDecomposer: public Decomposer {
 public:
  explicit TestDecomposer(const PEFile& image_file)
      : Decomposer(image_file) {
  }

  // Expose as public for testing.
  using Decomposer::LoadBlockGraphFromPDBStream;
  using Decomposer::GetBlockGraphStreamFromPDB;
};

class DecomposerTest : public testing::PELibUnitTest {
  typedef testing::PELibUnitTest Super;

 public:
  void SetUp() {
    Super::SetUp();

    ASSERT_NO_FATAL_FAILURE(CreateTemporaryDir(&temp_dir_));
  }

  FilePath temp_dir_;
};

}  // namespace

TEST_F(DecomposerTest, Decompose) {
  FilePath image_path(testing::GetExeRelativePath(kDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  // Decompose the test image and look at the result.
  Decomposer decomposer(image_file);
  EXPECT_TRUE(decomposer.pdb_path().empty());

  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  ASSERT_TRUE(decomposer.Decompose(&image_layout));
  EXPECT_FALSE(decomposer.pdb_path().empty());

  // Retrieve and validate the DOS header.
  BlockGraph::Block* dos_header_block =
      image_layout.blocks.GetBlockByAddress(RelativeAddress(0));
  ASSERT_TRUE(dos_header_block != NULL);
  ASSERT_TRUE(IsValidDosHeaderBlock(dos_header_block));

  // Retrieve and validate the NT header.
  BlockGraph::Block* nt_headers_block =
      GetNtHeadersBlockFromDosHeaderBlock(dos_header_block);
  ASSERT_TRUE(nt_headers_block != NULL);
  ASSERT_TRUE(IsValidNtHeadersBlock(nt_headers_block));

  // There should be some blocks in the graph and in the layout.
  EXPECT_NE(0U, block_graph.blocks().size());
  EXPECT_NE(0U, image_layout.blocks.address_space_impl().size());

  // All the blocks in the graph should be represented in the address space.
  EXPECT_EQ(block_graph.blocks().size(),
            image_layout.blocks.address_space_impl().size());

  ASSERT_EQ(6, image_layout.sections.size());

  EXPECT_EQ(".text", image_layout.sections[0].name);
  EXPECT_NE(0U, image_layout.sections[0].addr.value());
  EXPECT_NE(0U, image_layout.sections[0].size);
  EXPECT_NE(0U, image_layout.sections[0].data_size);
  EXPECT_EQ(IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ,
            image_layout.sections[0].characteristics);

  EXPECT_EQ(".rdata", image_layout.sections[1].name);
  EXPECT_NE(0U, image_layout.sections[1].addr.value());
  EXPECT_NE(0U, image_layout.sections[1].size);
  EXPECT_NE(0U, image_layout.sections[1].data_size);
  EXPECT_EQ(IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ,
            image_layout.sections[1].characteristics);

  EXPECT_EQ(".data", image_layout.sections[2].name);
  EXPECT_NE(0U, image_layout.sections[2].addr.value());
  EXPECT_NE(0U, image_layout.sections[2].size);
  EXPECT_NE(0U, image_layout.sections[2].data_size);
  EXPECT_EQ(
      IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE,
      image_layout.sections[2].characteristics);

  EXPECT_EQ(".tls", image_layout.sections[3].name);
  EXPECT_NE(0U, image_layout.sections[3].addr.value());
  EXPECT_NE(0U, image_layout.sections[3].size);
  EXPECT_NE(0U, image_layout.sections[3].data_size);
  EXPECT_EQ(
      IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE,
      image_layout.sections[3].characteristics);

  EXPECT_EQ(".rsrc", image_layout.sections[4].name);
  EXPECT_NE(0U, image_layout.sections[4].addr.value());
  EXPECT_NE(0U, image_layout.sections[4].size);
  EXPECT_NE(0U, image_layout.sections[4].data_size);
  EXPECT_EQ(IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ,
      image_layout.sections[4].characteristics);

  EXPECT_EQ(".reloc", image_layout.sections[5].name);
  EXPECT_NE(0U, image_layout.sections[5].addr.value());
  EXPECT_NE(0U, image_layout.sections[5].size);
  EXPECT_NE(0U, image_layout.sections[5].data_size);
  EXPECT_EQ(IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_DISCARDABLE |
      IMAGE_SCN_MEM_READ, image_layout.sections[5].characteristics);

  // We expect the ImageLayout sections to agree with the BlockGraph sections
  // in number, id, name and characteristics.
  EXPECT_EQ(block_graph.sections().size(), image_layout.sections.size());
  for (size_t i = 0; i < image_layout.sections.size(); ++i) {
    const BlockGraph::Section* section =
        block_graph.GetSectionById(i);
    ASSERT_TRUE(section != NULL);
    EXPECT_EQ(section->id(), i);
    EXPECT_EQ(section->name(), image_layout.sections[i].name);
    EXPECT_EQ(section->characteristics(),
              image_layout.sections[i].characteristics);
  }

  // We expect every block to be associated with a section, and only two blocks
  // should not be assigned to a section--the two header blocks.
  size_t non_section_blocks = 0;
  BlockGraph::BlockMap::const_iterator it =
      block_graph.blocks().begin();
  for (; it != block_graph.blocks().end(); ++it) {
    const BlockGraph::Block& block = it->second;
    if (block.section() == BlockGraph::kInvalidSectionId) {
      ++non_section_blocks;
    } else {
      // If this is not a header block, it should refer to a valid section id.
      EXPECT_LE(0u, block.section());
      EXPECT_LT(block.section(), block_graph.sections().size());
    }
  }
  EXPECT_EQ(2u, non_section_blocks);
}

TEST_F(DecomposerTest, DecomposeFailsWithNonexistentPdb) {
  FilePath image_path(testing::GetExeRelativePath(kDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  Decomposer decomposer(image_file);
  decomposer.set_pdb_path(testing::GetExeRelativePath(L"nonexistent.pdb"));

  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  EXPECT_FALSE(decomposer.Decompose(&image_layout));
}

TEST_F(DecomposerTest, BlockGraphSerializationRoundTrip) {
  FilePath image_path(testing::GetExeRelativePath(kDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  // Decompose the test image and look at the result.
  Decomposer decomposer(image_file);

  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  ASSERT_TRUE(decomposer.Decompose(&image_layout));

  FilePath temp_file_path = temp_dir_.Append(L"test_dll.dll.bg");

  // Save the BlockGraph.
  {
    file_util::ScopedFILE temp_file(file_util::OpenFile(temp_file_path, "wb"));
    core::FileOutStream out_stream(temp_file.get());
    core::NativeBinaryOutArchive out_archive(&out_stream);
    EXPECT_TRUE(
        SaveDecomposition(image_file, block_graph, image_layout, &out_archive));
    EXPECT_TRUE(out_archive.Flush());
  }

  // Load the BlockGraph, and compare it to the original.
  {
    file_util::ScopedFILE temp_file(file_util::OpenFile(temp_file_path, "rb"));
    core::FileInStream in_stream(temp_file.get());
    core::NativeBinaryInArchive in_archive(&in_stream);
    PEFile in_image_file;
    BlockGraph in_block_graph;
    ImageLayout in_image_layout(&block_graph);
    EXPECT_TRUE(LoadDecomposition(&in_archive,
                                  &in_image_file,
                                  &in_block_graph,
                                  &in_image_layout));

    EXPECT_TRUE(testing::BlockGraphsEqual(block_graph, in_block_graph, true));
    EXPECT_THAT(image_layout.blocks.address_space_impl().ranges(),
        testing::ContainerEq(
            in_image_layout.blocks.address_space_impl().ranges()));

    EXPECT_THAT(image_layout.sections,
                testing::ContainerEq(in_image_layout.sections));
  }
}

TEST_F(DecomposerTest, BasicBlockDecompose) {
  FilePath image_path(testing::GetExeRelativePath(kDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  // Decompose the test image and look at the result.
  Decomposer decomposer(image_file);

  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  ASSERT_TRUE(decomposer.Decompose(&image_layout));

  Decomposer::BasicBlockBreakdown breakdown;
  ASSERT_TRUE(decomposer.BasicBlockDecompose(image_layout, &breakdown));
  ASSERT_TRUE(breakdown.basic_block_address_space.begin() !=
      breakdown.basic_block_address_space.end());
}

TEST_F(DecomposerTest, LabelsAndAttributes) {
  FilePath image_path(testing::GetExeRelativePath(kDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  // Decompose the test image and look at the result.
  Decomposer decomposer(image_file);
  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  ASSERT_TRUE(decomposer.Decompose(&image_layout));

  // Locate various specific function blocks in the block-graph.
  const BlockGraph::Block* dll_main_block = NULL;
  const BlockGraph::Block* func_with_inl_asm_block = NULL;
  const BlockGraph::Block* strchr_block = NULL;
  {
    BlockGraph::BlockMap::const_iterator it =
        block_graph.blocks().begin();
    for (; it != block_graph.blocks().end(); ++it) {
      if (it->second.name().find("DllMain@12") != std::string::npos) {
        ASSERT_TRUE(dll_main_block == NULL);
        dll_main_block = &it->second;
      } else if (it->second.name().find("FunctionWithInlineAssembly") !=
          std::string::npos) {
        ASSERT_TRUE(func_with_inl_asm_block == NULL);
        func_with_inl_asm_block = &it->second;
      } else if (it->second.name().find("strchr.obj") != std::string::npos) {
        ASSERT_TRUE(strchr_block == NULL);
        strchr_block = &it->second;
      }
    }
  }

  // Validate that the FunctionWithInlineAssembly block has the appropriate
  // attributes.
  ASSERT_FALSE(func_with_inl_asm_block == NULL);
  ASSERT_TRUE(func_with_inl_asm_block->attributes() &
      BlockGraph::HAS_INLINE_ASSEMBLY);

  // Validate that the strchr block has the appropriate attributes.
  ASSERT_FALSE(strchr_block == NULL);
  ASSERT_TRUE(strchr_block->attributes() &
      BlockGraph::BUILT_BY_UNSUPPORTED_COMPILER);

  // Validate that the DllMain block has the expected population of labels.
  ASSERT_FALSE(dll_main_block == NULL);

#ifdef NDEBUG
  EXPECT_EQ(23, dll_main_block->labels().size());
#else
  EXPECT_EQ(24, dll_main_block->labels().size());
#endif

  std::map<BlockGraph::LabelAttributes, size_t> label_attr_counts;
  {
    BlockGraph::Block::LabelMap::const_iterator it =
        dll_main_block->labels().begin();
    for (; it != dll_main_block->labels().end(); ++it) {
      BlockGraph::LabelAttributes attr_mask = 1;
      for (; attr_mask != BlockGraph::LABEL_ATTR_MAX; attr_mask <<= 1) {
        if (it->second.attributes() & attr_mask)
          label_attr_counts[attr_mask]++;
      }
    }
  }

#ifdef NDEBUG
  EXPECT_EQ(17, label_attr_counts[BlockGraph::CODE_LABEL_ATTR]);
#else
  EXPECT_EQ(18, label_attr_counts[BlockGraph::CODE_LABEL]);
#endif
  EXPECT_EQ(4, label_attr_counts[BlockGraph::DATA_LABEL_ATTR]);
  EXPECT_EQ(2, label_attr_counts[BlockGraph::JUMP_TABLE_LABEL_ATTR]);
  EXPECT_EQ(2, label_attr_counts[BlockGraph::CASE_TABLE_LABEL_ATTR]);
  EXPECT_EQ(1, label_attr_counts[BlockGraph::DEBUG_START_LABEL_ATTR]);
  EXPECT_EQ(1, label_attr_counts[BlockGraph::DEBUG_END_LABEL_ATTR]);
}

namespace {

// This test fixture class contains all the tests that need files generated by
// the relinker (the new image and its corresponding PDB).
class DecomposerAfterRelinkTest : public DecomposerTest {
  typedef DecomposerTest Super;

 public:
  void SetUp() {
    Super::SetUp();

    // Initialize a relinker and generate a pdb that contains a block-graph
    // stream.
    relinked_dll_ = temp_dir_.Append(kDllName);
    relinked_pdb_ = temp_dir_.Append(kDllPdbName);

    relinker_.set_input_path(testing::GetExeRelativePath(kDllName));
    relinker_.set_input_pdb_path(testing::GetExeRelativePath(kDllPdbName));
    relinker_.set_allow_overwrite(true);
    relinker_.set_augment_pdb(true);
    relinker_.set_output_path(relinked_dll_);
    relinker_.set_output_pdb_path(relinked_pdb_);
    ASSERT_TRUE(relinker_.Init());
    ASSERT_TRUE(relinker_.Relink());
  }

  PERelinker relinker_;
  FilePath relinked_dll_;
  FilePath relinked_pdb_;
};

}  // namespace

TEST_F(DecomposerAfterRelinkTest, LoadRedecompositionData) {
  PEFile image_file;
  ASSERT_TRUE(image_file.Init(relinked_dll_));

  // Decompose the test image and look at the result.
  Decomposer decomposer(image_file);
  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);

  ASSERT_TRUE(decomposer.Decompose(&image_layout));
  // Ensure that we have the same amount of blocks in the block-graph created
  // by the relinker and in the one read from the PDB.
  ASSERT_EQ(relinker_.block_graph().blocks().size(),
            block_graph.blocks().size());
}

TEST_F(DecomposerAfterRelinkTest, FailToLoadBlockGraphWithInvalidVersion) {
  // Get the block-graph stream from the PDB and change the version of it.

  // Get the stream.
  PEFile image_file;
  ASSERT_TRUE(image_file.Init(relinked_dll_));
  TestDecomposer decomposer(image_file);
  pdb::PdbFile pdb_file;
  pdb::PdbReader pdb_reader;
  pdb_reader.Read(relinked_pdb_, &pdb_file);
  scoped_refptr<pdb::PdbStream> block_graph_stream =
      decomposer.GetBlockGraphStreamFromPDB(&pdb_file);

  // Create a copy of the stream. We need to do this to have a stream that we
  // can modify.
  scoped_refptr<pdb::PdbByteStream> new_stream = new pdb::PdbByteStream();
  ASSERT_TRUE(new_stream->Init(block_graph_stream.get()));
  block_graph_stream = new_stream.get();
  scoped_refptr<pdb::WritablePdbStream> block_graph_writer =
      block_graph_stream->GetWritablePdbStream();
  ASSERT_TRUE(block_graph_writer.get() != NULL);

  // Change the version of the stream.
  block_graph_writer->set_pos(0);
  block_graph_writer->Write(pdb::kSyzygyBlockGraphStreamVersion + 1);

  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  // We've invalided the version previously so this test should fail.
  ASSERT_FALSE(decomposer.LoadBlockGraphFromPDBStream(block_graph_stream.get(),
                                                      &image_layout.blocks));
}

}  // namespace pe
