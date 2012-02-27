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
//
// Unittests for iteration primitives.

#include "syzygy/instrument/transforms/entry_thunk_transform.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/block_graph/typed_block.h"
#include "syzygy/pe/pe_utils.h"

namespace instrument {
namespace transforms {

namespace {

using block_graph::BlockGraph;
using block_graph::TypedBlock;
using core::AbsoluteAddress;
using testing::_;
using testing::Return;

// Expose protected members for testing.
class TestEntryThunkTransform : public EntryThunkTransform {
 public:
  using EntryThunkTransform::Thunk;
};

class EntryThunkTransformTest : public testing::Test {
 public:
  EntryThunkTransformTest()
      : dos_header_block_(NULL),
        nt_headers_block_(NULL),
        foo_(NULL),
        bar_(NULL),
        array_(NULL) {
  }

  virtual void SetUp() {
    // TODO(siggi): We have a lot of code that does this sort of thing, maybe
    //     it should be concentrated in a test fixture in pe someplace.
    // Create the DOS/NT headers.
    dos_header_block_ = bg_.AddBlock(BlockGraph::DATA_BLOCK,
                                 sizeof(IMAGE_DOS_HEADER),
                                 "DOS Header");
    ASSERT_TRUE(
        dos_header_block_->AllocateData(dos_header_block_->size()) != NULL);

    nt_headers_block_ = bg_.AddBlock(BlockGraph::DATA_BLOCK,
                                     sizeof(IMAGE_NT_HEADERS),
                                     "NT Headers");

    ASSERT_TRUE(
        nt_headers_block_->AllocateData(nt_headers_block_->size()) != NULL);
    TypedBlock<IMAGE_NT_HEADERS> nt_headers;
    ASSERT_TRUE(nt_headers.Init(0, nt_headers_block_));
    nt_headers->Signature = IMAGE_NT_SIGNATURE;
    nt_headers->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
    nt_headers->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;

    TypedBlock<IMAGE_DOS_HEADER> dos_header;
    ASSERT_TRUE(dos_header.Init(0, dos_header_block_));
    ASSERT_TRUE(dos_header.SetReference(BlockGraph::RELATIVE_REF,
                                        dos_header->e_lfanew,
                                        nt_headers));

    // Make the DOS header valid just for giggles.
    ASSERT_TRUE(pe::UpdateDosHeader(dos_header_block_));

    // Get the .text section.
    BlockGraph::Section* text =
        bg_.FindOrAddSection(pe::kCodeSectionName, pe::kCodeCharacteristics);

    // Create a couple of code blocks for "functions".
    foo_ = bg_.AddBlock(BlockGraph::CODE_BLOCK, 20, "foo");
    foo_->set_section(text->id());
    foo_->source_ranges().Push(BlockGraph::Block::DataRange(0, 20),
        BlockGraph::Block::SourceRange(core::RelativeAddress(0x1000), 20));

    bar_ = bg_.AddBlock(BlockGraph::CODE_BLOCK, 20, "bar");
    bar_->set_section(text->id());
    bar_->source_ranges().Push(BlockGraph::Block::DataRange(0, 20),
        BlockGraph::Block::SourceRange(core::RelativeAddress(0x1020), 20));

    // Get the .rdata section.
    BlockGraph::Section* rdata =
        bg_.FindOrAddSection(pe::kReadOnlyDataSectionName,
                             pe::kReadOnlyDataCharacteristics);

    // Create a data array block.
    array_ = bg_.AddBlock(BlockGraph::DATA_BLOCK,
                          30 * sizeof(AbsoluteAddress),
                          "array");
    array_->set_section(rdata->id());

    // foo() refers to the start of bar() with a PC-relative reference.
    foo_->SetReference(5, BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF,
                                                sizeof(AbsoluteAddress),
                                                bar_,
                                                0));
    // foo() is self-referential.
    foo_->SetReference(10, BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF,
                                                sizeof(AbsoluteAddress),
                                                foo_,
                                                0));

    // bar() refers to foo() five bytes in.
    bar_->SetReference(5, BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF,
                                                sizeof(AbsoluteAddress),
                                                foo_,
                                                5));

    // The array refers to the start of both foo() and bar().
    array_->SetReference(0, BlockGraph::Reference(BlockGraph::ABSOLUTE_REF,
                                                  sizeof(AbsoluteAddress),
                                                  foo_,
                                                  0));

    array_->SetReference(4, BlockGraph::Reference(BlockGraph::ABSOLUTE_REF,
                                                  sizeof(AbsoluteAddress),
                                                  bar_,
                                                  0));

    // And the array refers 5 bytes into foo().
    array_->SetReference(8, BlockGraph::Reference(BlockGraph::ABSOLUTE_REF,
                                                  sizeof(AbsoluteAddress),
                                                  foo_,
                                                  5));

    num_sections_pre_transform_ = bg_.sections().size();

    // No thunks so far.
    ASSERT_NO_FATAL_FAILURE(VerifyThunks(0, 0, 0));
  }

  // Counts the number of thunks, the number of distinct destinations they
  // reference, and the number of entrypoints referenced.
  void CountThunks(size_t* num_thunks,
                   size_t* num_destinations,
                   size_t* num_entrypoints) {
    size_t found_thunks = 0;
    typedef std::set<std::pair<BlockGraph::Block*, BlockGraph::Offset>>
        ReferenceMap;
    ReferenceMap destinations;
    ReferenceMap entrypoints;
    BlockGraph::Section* thunk_section = bg_.FindSection(".thunks");
    if (thunk_section != NULL) {
      BlockGraph::BlockMap::const_iterator it(bg_.blocks().begin());

      for (; it != bg_.blocks().end(); ++it) {
        const BlockGraph::Block& block = it->second;
        if (block.section() == thunk_section->id()) {
          // It's a thunk.
          ++found_thunks;

          // Lookup and record the destination.
          BlockGraph::Reference ref;
          ASSERT_TRUE(block.GetReference(
              offsetof(TestEntryThunkTransform::Thunk, func_addr), &ref));
          ASSERT_EQ(BlockGraph::ABSOLUTE_REF, ref.type());
          destinations.insert(std::make_pair(ref.referenced(), ref.offset()));

          // Test the source ranges on the thunk.
          ASSERT_EQ(1, block.source_ranges().size());
          BlockGraph::Block::SourceRanges::RangePair r =
              block.source_ranges().range_pairs()[0];
          ASSERT_EQ(0, r.first.start());
          ASSERT_EQ(sizeof(TestEntryThunkTransform::Thunk), r.first.size());

          // Retrieve the referenced block's source ranges to calculate
          // the destination start address.
          ASSERT_EQ(1, ref.referenced()->source_ranges().size());
          BlockGraph::Block::SourceRanges::RangePair o =
              ref.referenced()->source_ranges().range_pairs()[0];

          // The thunk's destination should be the block's start, plus the
          // reference offset.
          ASSERT_EQ(o.second.start() + ref.offset(), r.second.start());
          ASSERT_EQ(sizeof(TestEntryThunkTransform::Thunk), r.second.size());

          // Lookup and record the entrypoint.
          ASSERT_TRUE(block.GetReference(
              offsetof(TestEntryThunkTransform::Thunk, hook_addr), &ref));
          ASSERT_EQ(BlockGraph::ABSOLUTE_REF, ref.type());
          entrypoints.insert(std::make_pair(ref.referenced(), ref.offset()));

          EXPECT_EQ(BlockGraph::CODE_BLOCK, block.type());
          EXPECT_EQ(2, block.references().size());
        }
      }
    }

    if (num_thunks != NULL)
      *num_thunks = found_thunks;
    if (num_destinations != NULL)
      *num_destinations = destinations.size();
    if (num_entrypoints != NULL)
      *num_entrypoints = entrypoints.size();
  }

  // Verifies that there are num_thunks thunks in the image, and that they
  // have the expected properties.
  void VerifyThunks(size_t expected_thunks,
                    size_t expected_destinations,
                    size_t expected_entrypoints) {
    size_t found_thunks = 0;
    size_t found_destinations = 0;
    size_t found_entrypoints = 0;
    CountThunks(&found_thunks, &found_destinations, &found_entrypoints);
    EXPECT_EQ(expected_thunks, found_thunks);
    EXPECT_EQ(expected_destinations, found_destinations);
    EXPECT_EQ(expected_entrypoints, found_entrypoints);
  }

  enum ImageType {
    DLL_IMAGE,
    EXE_IMAGE,
  };

  // Sets the image entrypoint and sets or clears the DLL flag
  // in the NT headers.
  void SetEntryPoint(BlockGraph::Block* entrypoint, ImageType image_type) {
    // Set the image entrypoint.
    TypedBlock<IMAGE_NT_HEADERS> nt_headers;
    ASSERT_TRUE(nt_headers.Init(0, nt_headers_block_));
    ASSERT_TRUE(
        nt_headers.SetReference(
            BlockGraph::RELATIVE_REF,
            nt_headers->OptionalHeader.AddressOfEntryPoint,
            entrypoint,
            0));

    // Set or clear the DLL flag.
    if (image_type == DLL_IMAGE)
      nt_headers->FileHeader.Characteristics |= IMAGE_FILE_DLL;
    else
      nt_headers->FileHeader.Characteristics &= ~IMAGE_FILE_DLL;
  }

  // Creates a TLS directory with the given block for entrypoint, and sets or
  // clears the DLL flag in the NT headers.
  void SetTLSEntryPoint(BlockGraph::Block* entrypoint, ImageType image_type) {
    // Set the image entrypoint.
    TypedBlock<IMAGE_NT_HEADERS> nt_headers;
    ASSERT_TRUE(nt_headers.Init(0, nt_headers_block_));
    IMAGE_DATA_DIRECTORY& data_dir =
        nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    ASSERT_EQ(0, data_dir.Size);

    // Create the TLS directory block.
    BlockGraph::Block* tls_dir_block = bg_.AddBlock(BlockGraph::DATA_BLOCK,
                                                    sizeof(IMAGE_TLS_DIRECTORY),
                                                    "TLS Directory");
    ASSERT_TRUE(tls_dir_block != NULL);
    ASSERT_TRUE(tls_dir_block->AllocateData(tls_dir_block->size()));

    // Hook the TLS dir up to the NT headers.
    ASSERT_TRUE(nt_headers.SetReference(BlockGraph::ABSOLUTE_REF,
                                        data_dir.VirtualAddress,
                                        tls_dir_block,
                                        0));
    data_dir.Size = tls_dir_block->size();

    TypedBlock<IMAGE_TLS_DIRECTORY> tls_dir;
    ASSERT_TRUE(tls_dir.Init(0, tls_dir_block));

    BlockGraph::Block* tls_callbacks = bg_.AddBlock(BlockGraph::DATA_BLOCK,
                                                    2 * sizeof(AbsoluteAddress),
                                                    "TLS Callbacks");
    ASSERT_TRUE(tls_callbacks != NULL);
    ASSERT_TRUE(tls_callbacks->AllocateData(tls_callbacks->size()) != NULL);
    ASSERT_TRUE(tls_dir.SetReference(BlockGraph::ABSOLUTE_REF,
                                     tls_dir->AddressOfCallBacks,
                                     tls_callbacks,
                                     0));

    ASSERT_TRUE(tls_callbacks->SetReference(0,
                    BlockGraph::Reference(BlockGraph::ABSOLUTE_REF,
                                          sizeof(AbsoluteAddress),
                                          entrypoint,
                                          0)));

    // Set or clear the DLL flag.
    if (image_type == DLL_IMAGE)
      nt_headers->FileHeader.Characteristics |= IMAGE_FILE_DLL;
    else
      nt_headers->FileHeader.Characteristics &= ~IMAGE_FILE_DLL;
  }

 protected:
  size_t num_sections_pre_transform_;

  BlockGraph bg_;
  BlockGraph::Block* dos_header_block_;
  BlockGraph::Block* nt_headers_block_;

  BlockGraph::Block* foo_;
  BlockGraph::Block* bar_;
  BlockGraph::Block* array_;
};

}  // namespace

TEST_F(EntryThunkTransformTest, InstrumentAll) {
  EntryThunkTransform transform;

  ASSERT_TRUE(ApplyTransform(&transform, &bg_, dos_header_block_));

  // We should have three thunks - one each for the start of foo() and bar(),
  // and one for the middle of foo().
  ASSERT_NO_FATAL_FAILURE(VerifyThunks(3, 3, 1));

  // The .thunks section should have been added.
  EXPECT_EQ(num_sections_pre_transform_+ 1, bg_.sections().size());
}

TEST_F(EntryThunkTransformTest, InstrumentNoInterior) {
  EntryThunkTransform transform;

  // No interior reference instrumentation.
  transform.set_instrument_interior_references(false);

  ASSERT_TRUE(ApplyTransform(&transform, &bg_, dos_header_block_));

  // We should have two thunks - one each for the start of foo() and bar().
  ASSERT_NO_FATAL_FAILURE(VerifyThunks(2, 2, 1));

  // The .thunks section should have been added.
  EXPECT_EQ(num_sections_pre_transform_+ 1, bg_.sections().size());
}

TEST_F(EntryThunkTransformTest, InstrumentDllEntrypoint) {
  EntryThunkTransform transform;

  // Set the entrypoint to foo(), and make us a DLL.
  ASSERT_NO_FATAL_FAILURE(SetEntryPoint(foo_, DLL_IMAGE));

  ASSERT_TRUE(ApplyTransform(&transform, &bg_, dos_header_block_));

  // We should have three thunks - one each for the start of foo() and bar().
  // One of the thunks should use the DllMain entrypoint.
  ASSERT_NO_FATAL_FAILURE(VerifyThunks(3, 3, 2));

  // The .thunks section should have been added.
  EXPECT_EQ(num_sections_pre_transform_+ 1, bg_.sections().size());
}

TEST_F(EntryThunkTransformTest, InstrumentExeEntrypoint) {
  EntryThunkTransform transform;

  // Set the entrypoint to foo(), and make us an EXE.
  ASSERT_NO_FATAL_FAILURE(SetEntryPoint(foo_, EXE_IMAGE));

  ASSERT_TRUE(ApplyTransform(&transform, &bg_, dos_header_block_));

  // We should have three thunks - one each for the start of foo() and bar().
  // None of the thunks should use the DllMain entrypoint.
  ASSERT_NO_FATAL_FAILURE(VerifyThunks(3, 3, 1));

  // The .thunks section should have been added.
  EXPECT_EQ(num_sections_pre_transform_+ 1, bg_.sections().size());
}

TEST_F(EntryThunkTransformTest, InstrumentDllTLSEntrypoint) {
  EntryThunkTransform transform;

  // Set foo() up as a TLS entrypoint, and make us a DLL.
  ASSERT_NO_FATAL_FAILURE(SetTLSEntryPoint(foo_, DLL_IMAGE));

  ASSERT_TRUE(ApplyTransform(&transform, &bg_, dos_header_block_));

  // We should have three thunks - one each for the start of foo() and bar().
  // One of the thunks should use the DllMain entrypoint.
  ASSERT_NO_FATAL_FAILURE(VerifyThunks(3, 3, 2));

  // The .thunks section should have been added.
  EXPECT_EQ(num_sections_pre_transform_+ 1, bg_.sections().size());
}

TEST_F(EntryThunkTransformTest, InstrumentExeTLSEntrypoint) {
  EntryThunkTransform transform;

  // Set foo() up as a TLS entrypoint, and make us an EXE.
  ASSERT_NO_FATAL_FAILURE(SetTLSEntryPoint(foo_, EXE_IMAGE));

  ASSERT_TRUE(ApplyTransform(&transform, &bg_, dos_header_block_));

  // We should have three thunks - one each for the start of foo() and bar().
  // One of the thunks should use the DllMain entrypoint.
  ASSERT_NO_FATAL_FAILURE(VerifyThunks(3, 3, 2));

  // The .thunks section should have been added.
  EXPECT_EQ(num_sections_pre_transform_+ 1, bg_.sections().size());
}

}  // namespace transforms
}  // namespace instrument
