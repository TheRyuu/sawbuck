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

#include "syzygy/pe/new_decomposer.h"

#include "base/string_split.h"
#include "gtest/gtest.h"
#include "syzygy/block_graph/block_graph_serializer.h"
#include "syzygy/block_graph/typed_block.h"
#include "syzygy/block_graph/unittest_util.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pdb/pdb_byte_stream.h"
#include "syzygy/pdb/pdb_file.h"
#include "syzygy/pdb/pdb_reader.h"
#include "syzygy/pdb/pdb_util.h"
#include "syzygy/pe/pe_relinker.h"
#include "syzygy/pe/pe_utils.h"
#include "syzygy/pe/unittest_util.h"

namespace pe {

namespace {

using block_graph::BlockGraph;
using block_graph::ConstTypedBlock;
using core::RelativeAddress;

const size_t kPointerSize = BlockGraph::Reference::kMaximumSize;

// Exposes the protected methods for testing.
class TestNewDecomposer : public NewDecomposer {
 public:
  explicit TestNewDecomposer(const PEFile& image_file)
      : NewDecomposer(image_file) {
  }

  // Expose as public for testing.
  using NewDecomposer::LoadBlockGraphFromPdbStream;
  using NewDecomposer::LoadBlockGraphFromPdb;
};

class NewDecomposerTest : public testing::PELibUnitTest {
  typedef testing::PELibUnitTest Super;

 public:
  void SetUp() {
    Super::SetUp();

    ASSERT_NO_FATAL_FAILURE(CreateTemporaryDir(&temp_dir_));
  }

  FilePath temp_dir_;
};

}  // namespace

TEST_F(NewDecomposerTest, MutatorsAndAccessors) {
  FilePath image_path(testing::GetExeRelativePath(testing::kTestDllName));
  FilePath pdb_path(testing::GetExeRelativePath(testing::kTestDllPdbName));

  PEFile image_file;
  ASSERT_TRUE(image_file.Init(image_path));

  NewDecomposer decomposer(image_file);
  EXPECT_TRUE(decomposer.pdb_path().empty());
  EXPECT_TRUE(decomposer.parse_debug_info());

  decomposer.set_pdb_path(pdb_path);
  EXPECT_EQ(pdb_path, decomposer.pdb_path());

  decomposer.set_parse_debug_info(false);
  EXPECT_FALSE(decomposer.parse_debug_info());
}

TEST_F(NewDecomposerTest, Decompose) {
  FilePath image_path(testing::GetExeRelativePath(testing::kTestDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  // Decompose the test image.
  NewDecomposer decomposer(image_file);
  EXPECT_TRUE(decomposer.pdb_path().empty());

  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  EXPECT_TRUE(decomposer.Decompose(&image_layout));
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

  // There should be some blocks in the graph and in the layout, and the same
  // number in the block-graph and image layout.
  EXPECT_LT(0u, block_graph.blocks().size());
  EXPECT_LT(0u, image_layout.blocks.size());
  EXPECT_EQ(block_graph.blocks().size(), image_layout.blocks.size());

  EXPECT_EQ(6u, block_graph.sections().size());
  EXPECT_EQ(6u, image_layout.sections.size());

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

  // Every byte of each section must be accounted for by all of the non-header
  // blocks.
  size_t section_blocks = 0;
  for (size_t i = 0; i < image_layout.sections.size(); ++i) {
    BlockGraph::AddressSpace::RangeMapConstIterPair it_pair =
        image_layout.blocks.GetIntersectingBlocks(
            image_layout.sections[i].addr,
            image_layout.sections[i].size);

    // Make sure the first iterator is dereferenceable.
    ASSERT_TRUE(it_pair.first != image_layout.blocks.end());

    // The first and last iterators should not be the same.
    EXPECT_TRUE(it_pair.first != it_pair.second);

    // The first iterator should start at the beginning of the section.
    EXPECT_EQ(image_layout.sections[i].addr, it_pair.first->first.start());

    // Ensure the blocks are contiguous, and count the number of blocks as we
    // go.
    BlockGraph::AddressSpace::RangeMapConstIter it_old = it_pair.first;
    BlockGraph::AddressSpace::RangeMapConstIter it_cur = it_pair.first;
    ++it_cur;
    ++section_blocks;
    while (it_cur != it_pair.second) {
      EXPECT_EQ(it_old->first.end(), it_cur->first.start());
      it_old = it_cur;
      ++it_cur;
      ++section_blocks;
    }

    // Make sure the last block perfectly covers the section.
    EXPECT_EQ(image_layout.sections[i].addr + image_layout.sections[i].size,
              it_old->first.end());
  }

  // All of the blocks should have been covered, save the 2 header blocks.
  EXPECT_EQ(section_blocks + 2, image_layout.blocks.size());

  // Make sure that all bracketed COFF groups have been parsed. There are 8
  // of them that we currently know of:
  // .CRT$XCA -> .CRT$XCZ: C initializers
  // .CRT$XIA -> .CRT$XLZ: C++ initializers
  // .CRT$XLA -> .CRT$XLZ: TLS callbacks
  // .CRT$XPA -> .CRT$XPZ: CRT pre-termination functions.
  // .CRT$XTA -> .CRT$XTZ: CRT termination functions.
  // .rtc$IAA -> .rtc$IZZ: Run-time checking initializers.
  // .rtc$TAA -> .rtc$TZZ: Run-time checking termination functions.
  // .tls -> .tls$ZZZ: TLS data.
  size_t coff_group_blocks = 0;
  it = block_graph.blocks().begin();
  for (; it != block_graph.blocks().end(); ++it) {
    const BlockGraph::Block& block = it->second;
    if (block.attributes() & BlockGraph::COFF_GROUP)
      ++coff_group_blocks;
  }
  EXPECT_EQ(8u, coff_group_blocks);
}

TEST_F(NewDecomposerTest, DecomposeFailsWithNonexistentPdb) {
  FilePath image_path(testing::GetExeRelativePath(testing::kTestDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  NewDecomposer decomposer(image_file);
  decomposer.set_pdb_path(testing::GetExeRelativePath(L"nonexistent.pdb"));

  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  EXPECT_FALSE(decomposer.Decompose(&image_layout));
}

TEST_F(NewDecomposerTest, LabelsAndAttributes) {
  FilePath image_path(testing::GetExeRelativePath(testing::kTestDllName));
  PEFile image_file;

  ASSERT_TRUE(image_file.Init(image_path));

  // Decompose the test image and look at the result.
  NewDecomposer decomposer(image_file);
  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  ASSERT_TRUE(decomposer.Decompose(&image_layout));

  // Locate various specific function blocks in the block-graph.
  const BlockGraph::Block* dll_main_block = NULL;
  const BlockGraph::Block* func_with_inl_asm_block = NULL;
  const BlockGraph::Block* strchr_block = NULL;
  const BlockGraph::Block* imp_load_block = NULL;
  {
    BlockGraph::BlockMap::const_iterator it =
        block_graph.blocks().begin();
    for (; it != block_graph.blocks().end(); ++it) {
      const BlockGraph::Block* block = &(it->second);

      BlockGraph::Label label;
      if (!block->GetLabel(0, &label))
        continue;
      std::vector<std::string> names;
      base::SplitStringUsingSubstr(label.name(), NewDecomposer::kLabelNameSep,
                                   &names);

      base::StringPiece block_name(block->name());

      if (std::find(names.begin(), names.end(), "DllMain") != names.end() &&
          block_name.ends_with("\\test_dll.obj")) {
        ASSERT_TRUE(dll_main_block == NULL);
        dll_main_block = &it->second;
      } else if (std::find(names.begin(), names.end(),
                           "FunctionWithInlineAssembly") != names.end() &&
          block_name.ends_with("\\test_dll.obj")) {
        ASSERT_TRUE(func_with_inl_asm_block == NULL);
        func_with_inl_asm_block = &it->second;
      } else if (std::find(names.begin(), names.end(),
                           "found_bx") != names.end() &&
            block_name.ends_with("\\strchr.obj")) {
        ASSERT_TRUE(strchr_block == NULL);
        strchr_block = &it->second;
      } else if (std::find(names.begin(), names.end(),
                           "__imp_load_CoInitialize") != names.end()) {
        ASSERT_TRUE(imp_load_block == NULL);
        imp_load_block = &it->second;
      }
    }
  }
  ASSERT_TRUE(dll_main_block != NULL);
  ASSERT_TRUE(func_with_inl_asm_block != NULL);
  ASSERT_TRUE(strchr_block != NULL);
  ASSERT_TRUE(imp_load_block != NULL);

  ASSERT_NE(0UL, imp_load_block->attributes() & BlockGraph::THUNK);

  // TODO(chrisha): When alignment calculations are complete, re-enable this
  //     test.
  // DllMain has a jump table so it should have pointer alignment.
  //ASSERT_EQ(kPointerSize, dll_main_block->alignment());

  // Validate that the FunctionWithInlineAssembly block has the appropriate
  // attributes.
  ASSERT_TRUE(func_with_inl_asm_block->attributes() &
      BlockGraph::HAS_INLINE_ASSEMBLY);

  // Validate that the strchr block has the appropriate attributes.
  ASSERT_TRUE(strchr_block->attributes() &
      BlockGraph::BUILT_BY_UNSUPPORTED_COMPILER);

  // Validate that the DllMain block has the expected population of labels.
  EXPECT_EQ(32, dll_main_block->labels().size());

  std::map<BlockGraph::LabelAttributes, size_t> label_attr_counts;
  {
    BlockGraph::Block::LabelMap::const_iterator it =
        dll_main_block->labels().begin();
    for (; it != dll_main_block->labels().end(); ++it) {
      BlockGraph::LabelAttributes attr_mask = 1;
      for (; attr_mask != BlockGraph::LABEL_ATTRIBUTES_MAX; attr_mask <<= 1) {
        if (it->second.has_attributes(attr_mask))
          label_attr_counts[attr_mask]++;
      }
    }
  }

  EXPECT_EQ(19, label_attr_counts[BlockGraph::CODE_LABEL]);
  EXPECT_EQ(10, label_attr_counts[BlockGraph::CALL_SITE_LABEL]);
  EXPECT_EQ(5, label_attr_counts[BlockGraph::DATA_LABEL]);
  EXPECT_EQ(3, label_attr_counts[BlockGraph::JUMP_TABLE_LABEL]);
  EXPECT_EQ(2, label_attr_counts[BlockGraph::CASE_TABLE_LABEL]);
  EXPECT_EQ(1, label_attr_counts[BlockGraph::DEBUG_START_LABEL]);
  EXPECT_EQ(1, label_attr_counts[BlockGraph::DEBUG_END_LABEL]);
}

namespace {

void GetNtHeadersBlock(const BlockGraph::Block* dos_header_block,
                       BlockGraph::Block** out_nt_headers_block) {
  DCHECK(out_nt_headers_block != NULL);

  ConstTypedBlock<IMAGE_DOS_HEADER> dos_header;
  ASSERT_TRUE(dos_header.Init(0, dos_header_block));
  ConstTypedBlock<IMAGE_NT_HEADERS> nt_headers;
  ASSERT_TRUE(dos_header.Dereference(dos_header->e_lfanew, &nt_headers));
  ASSERT_TRUE(nt_headers.block() != NULL);
  *out_nt_headers_block = const_cast<BlockGraph::Block*>(nt_headers.block());
}

// This test fixture class contains all the tests that need files generated by
// the relinker (the new image and its corresponding PDB).
class NewDecomposerAfterRelinkTest : public NewDecomposerTest {
  typedef NewDecomposerTest Super;

 public:
  virtual void SetUp() OVERRIDE {
    Super::SetUp();
  }

  void Relink(bool compress_pdb) {
    // Initialize a relinker and generate a pdb that contains a block-graph
    // stream.
    relinked_dll_ = temp_dir_.Append(testing::kTestDllName);
    relinked_pdb_ = temp_dir_.Append(testing::kTestDllPdbName);

    relinker_.set_input_path(testing::GetExeRelativePath(
        testing::kTestDllName));
    relinker_.set_input_pdb_path(testing::GetExeRelativePath(
        testing::kTestDllPdbName));
    relinker_.set_allow_overwrite(true);
    relinker_.set_augment_pdb(true);
    relinker_.set_compress_pdb(compress_pdb);
    relinker_.set_output_path(relinked_dll_);
    relinker_.set_output_pdb_path(relinked_pdb_);
    ASSERT_TRUE(relinker_.Init());
    ASSERT_TRUE(relinker_.Relink());
  }

  // Given a decomposed image this checks its NT headers against those
  // contained in the transformed image stored in the relinker.
  void ReconcileNtHeaders(ImageLayout* image_layout) {
    DCHECK(image_layout != NULL);

    // Get the NT headers block associated with the in-memory representation of
    // the relinked image.
    BlockGraph::Block* nt1 = NULL;
    ASSERT_NO_FATAL_FAILURE(GetNtHeadersBlock(relinker_.dos_header_block(),
                                              &nt1));
    ASSERT_TRUE(nt1 != NULL);

    // Get the NT headers block associated with the decomposition just performed
    // on the relinked image.
    BlockGraph::Block* dos_header_block =
        image_layout->blocks.GetBlockByAddress(core::RelativeAddress(0));
    ASSERT_TRUE(dos_header_block != NULL);
    BlockGraph::Block* nt2 = NULL;
    ASSERT_NO_FATAL_FAILURE(GetNtHeadersBlock(dos_header_block, &nt2));
    ASSERT_TRUE(nt2 != NULL);

    // The NT headers don't compare equal because things like the timestamp and
    // checksum are filled out post-transform. We copy the old NT headers into
    // the new image so that we can do a simple comparison afterwards.
    ASSERT_EQ(nt1->data_size(), nt2->data_size());
    nt1->SetData(nt2->data(), nt2->data_size());
  }

  // Used to ensure that round-trip decomposition works (where the image is not
  // decomposed, but rather deserialized from the PDB).
  void LoadRedecompositionData(bool compressed) {
    ASSERT_NO_FATAL_FAILURE(Relink(compressed));

    PEFile image_file;
    ASSERT_TRUE(image_file.Init(relinked_dll_));

    // Decompose the test image and look at the result.
    NewDecomposer decomposer(image_file);
    BlockGraph block_graph;
    ImageLayout image_layout(&block_graph);

    ASSERT_TRUE(decomposer.Decompose(&image_layout));

    // Certain data is written to the NT headers post-transform (checksum), so
    // it's not reflected in the relinker's block-graph. We reconcile the
    // headers prior to doing the comparison.
    ASSERT_NO_FATAL_FAILURE(ReconcileNtHeaders(&image_layout));

    // Ensure that the post-relink block-graph and the deserialized one from the
    // PDB are the same.
    block_graph::BlockGraphSerializer bgs;
    ASSERT_TRUE(::testing::BlockGraphsEqual(relinker_.block_graph(),
                                            block_graph,
                                            bgs));
  }

  PERelinker relinker_;
  FilePath relinked_dll_;
  FilePath relinked_pdb_;
};

}  // namespace

TEST_F(NewDecomposerAfterRelinkTest, LoadRedecompositionDataUncompressed) {
  ASSERT_NO_FATAL_FAILURE(LoadRedecompositionData(false));
}

TEST_F(NewDecomposerAfterRelinkTest, LoadRedecompositionDataCompressed) {
  ASSERT_NO_FATAL_FAILURE(LoadRedecompositionData(true));
}

TEST_F(NewDecomposerAfterRelinkTest, FailToLoadBlockGraphWithInvalidVersion) {
  ASSERT_NO_FATAL_FAILURE(Relink(true));

  // Get the block-graph stream from the PDB and change the version of it.

  // Get the stream.
  pdb::PdbFile pdb_file;
  pdb::PdbReader pdb_reader;
  pdb_reader.Read(relinked_pdb_, &pdb_file);
  scoped_refptr<pdb::PdbStream> block_graph_stream;
  EXPECT_TRUE(pdb::LoadNamedStreamFromPdbFile(pdb::kSyzygyBlockGraphStreamName,
                                              &pdb_file,
                                              &block_graph_stream));

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
  PEFile image_file;
  ASSERT_TRUE(image_file.Init(relinked_dll_));
  ASSERT_FALSE(TestNewDecomposer::LoadBlockGraphFromPdbStream(
      image_file, block_graph_stream.get(), &image_layout));
}

}  // namespace pe
