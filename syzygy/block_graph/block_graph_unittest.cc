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

#include "syzygy/block_graph/block_graph.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/block_graph/unittest_util.h"
#include "syzygy/block_graph/typed_block.h"
#include "syzygy/core/unittest_util.h"

namespace block_graph {

using core::ByteVector;
using core::CreateByteInStream;
using core::CreateByteOutStream;
using core::NativeBinaryInArchive;
using core::NativeBinaryOutArchive;
using core::RelativeAddress;
using core::ScopedInStreamPtr;
using core::ScopedOutStreamPtr;

const size_t kPtrSize = sizeof(core::RelativeAddress);

TEST(SectionTest, CreationAndProperties) {
  BlockGraph::Section section(0, "foo", 1);
  ASSERT_EQ(0, section.id());
  ASSERT_EQ("foo", section.name());
  ASSERT_EQ(1u, section.characteristics());

  section.set_name("bar");
  ASSERT_EQ("bar", section.name());

  section.set_characteristic((1 << 5) | (1 << 6));
  ASSERT_EQ((1u | (1 << 5) | (1 << 6)), section.characteristics());

  section.clear_characteristic(1 | (1<<5));
  ASSERT_EQ(1u << 6, section.characteristics());

  section.set_characteristics(0);
  ASSERT_EQ(0u, section.characteristics());
}

TEST(SectionTest, Comparison) {
  BlockGraph::Section section0(0, "foo", 0);
  BlockGraph::Section section1(0, "foo", 0);
  BlockGraph::Section section2(1, "bar", 1);

  EXPECT_EQ(section0, section1);
  EXPECT_NE(section0, section2);
}

TEST(SectionTest, Serialization) {
  BlockGraph::Section section0(0, "foo", 0);
  EXPECT_TRUE(testing::TestSerialization(section0));
}

class BlockTest: public testing::Test {
 public:
  virtual void SetUp() {
    block_ = image_.AddBlock(kBlockType, kBlockSize, kBlockName);
    ASSERT_TRUE(block_ != NULL);
  }

 protected:
  static const BlockGraph::BlockType kBlockType = BlockGraph::CODE_BLOCK;
  static const size_t kBlockSize = 0x20;
  static const char* kBlockName;
  static const uint8 kTestData[];

  BlockGraph image_;
  BlockGraph::Block* block_;
};

const char* BlockTest::kBlockName = "block";
const uint8 BlockTest::kTestData[] = "who's your daddy?";

TEST_F(BlockTest, Initialization) {
  // Test initialization.
  ASSERT_EQ(kBlockType, block_->type());
  ASSERT_EQ(kBlockSize, block_->size());
  ASSERT_EQ(1, block_->alignment());
  ASSERT_STREQ(kBlockName, block_->name().c_str());
  ASSERT_EQ(kInvalidAddress, block_->addr());
  ASSERT_EQ(BlockGraph::kInvalidSectionId, block_->section());
  ASSERT_EQ(0, block_->attributes());
  ASSERT_EQ(NULL, block_->data());
  ASSERT_EQ(0, block_->data_size());
  ASSERT_FALSE(block_->owns_data());
}

TEST_F(BlockTest, Accessors) {
  ASSERT_NE(BlockGraph::DATA_BLOCK, block_->type());
  block_->set_type(BlockGraph::DATA_BLOCK);
  ASSERT_EQ(BlockGraph::DATA_BLOCK, block_->type());

  ASSERT_NE(0x10U, block_->size());
  block_->set_size(0x10);
  ASSERT_EQ(0x10U, block_->size());

  ASSERT_STRNE("foo", block_->name().c_str());
  block_->set_name("foo");
  ASSERT_STREQ("foo", block_->name().c_str());

  ASSERT_NE(16U, block_->alignment());
  block_->set_alignment(16);
  ASSERT_EQ(16U, block_->alignment());

  // Test accessors.
  block_->set_attribute(0x20);
  ASSERT_EQ(0x20, block_->attributes());
  block_->set_attribute(0x10);
  ASSERT_EQ(0x30, block_->attributes());
  block_->clear_attribute(0x20);
  ASSERT_EQ(0x10, block_->attributes());

  block_->set_size(sizeof(kTestData));
  block_->SetData(kTestData, sizeof(kTestData));
  ASSERT_EQ(kTestData, block_->data());
  ASSERT_EQ(sizeof(kTestData), block_->data_size());
  ASSERT_EQ(false, block_->owns_data());
}

TEST_F(BlockTest, AllocateData) {
  // Test AllocateData.
  const size_t kBlockSize = 0x20;
  uint8* data = block_->AllocateData(block_->size());
  ASSERT_TRUE(block_->owns_data());
  ASSERT_EQ(block_->size(), block_->data_size());
  ASSERT_EQ(data, block_->data());

  static const uint8 zeros[kBlockSize] = {};
  ASSERT_EQ(0, memcmp(&zeros[0], data, block_->size()));
}

TEST_F(BlockTest, CopyData) {
  // Test CopyData.
  uint8* data = block_->CopyData(sizeof(kTestData), kTestData);
  ASSERT_TRUE(block_->owns_data());
  ASSERT_EQ(sizeof(kTestData), block_->data_size());
  ASSERT_EQ(data, block_->data());
  ASSERT_EQ(0, memcmp(kTestData, data, block_->data_size()));
}

TEST_F(BlockTest, ResizeData) {
  // Set the block's data.
  block_->SetData(kTestData, sizeof(kTestData));

  // Shrinking the data should not take ownership.
  const uint8* data = block_->ResizeData(sizeof(kTestData) / 2);
  ASSERT_TRUE(data != NULL);
  ASSERT_TRUE(data == kTestData);
  ASSERT_FALSE(block_->owns_data());

  // Growing the data must always take ownership.
  data = block_->ResizeData(sizeof(kTestData));
  ASSERT_TRUE(data != NULL);
  ASSERT_TRUE(data != kTestData);
  ASSERT_TRUE(block_->owns_data());
  // The head of the data should be identical to the input.
  ASSERT_EQ(0, memcmp(data, kTestData, sizeof(kTestData) / 2));
  // And the tail should be zeros.
  static const uint8 kZeros[sizeof(kTestData) - sizeof(kTestData) / 2] = {};
  ASSERT_EQ(0, memcmp(data + sizeof(kTestData) / 2, kZeros, sizeof(kZeros)));

  // Now grow it from non-owned.
  block_->SetData(kTestData, sizeof(kTestData));
  data = block_->ResizeData(sizeof(kTestData) + sizeof(kZeros));
  ASSERT_TRUE(data != NULL);
  ASSERT_TRUE(data != kTestData);
  ASSERT_TRUE(block_->owns_data());

  // The head of the data should be identical to the input.
  ASSERT_EQ(0, memcmp(data, kTestData, sizeof(kTestData)));
  // And the tail should be zeros.
  ASSERT_EQ(0, memcmp(data + sizeof(kTestData), kZeros, sizeof(kZeros)));
}

TEST_F(BlockTest, GetMutableData) {
  // Set the block's data.
  block_->SetData(kTestData, sizeof(kTestData));

  // Getting a mutable pointer should copy the data to heap.
  uint8* data = block_->GetMutableData();
  ASSERT_TRUE(data != NULL);
  ASSERT_TRUE(data != kTestData);
  ASSERT_TRUE(block_->owns_data());
  ASSERT_EQ(sizeof(kTestData), block_->data_size());
  ASSERT_EQ(data, block_->data());
  ASSERT_EQ(0, memcmp(kTestData, data, block_->data_size()));

  // Getting the data a second time should return the same pointer.
  ASSERT_EQ(data, block_->GetMutableData());
}

TEST_F(BlockTest, InsertData) {
  // Create a block with a labelled array of pointers. Explicitly initialize
  // the last one with some data and let the block be longer than its
  // explicitly initialized length.
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, 4 * kPtrSize, "Block1");
  block1->AllocateData(3 * kPtrSize);
  block1->source_ranges().Push(BlockGraph::Block::DataRange(0, 4 * kPtrSize),
                               BlockGraph::Block::SourceRange(
                               core::RelativeAddress(0), 4 * kPtrSize));
  BlockGraph::Reference outgoing_ref(BlockGraph::RELATIVE_REF,
                                     kPtrSize,
                                     block_,
                                     0);
  block1->SetReference(0, outgoing_ref);
  block1->SetReference(kPtrSize, outgoing_ref);
  block1->SetLabel(0, "Pointer1");
  block1->SetLabel(kPtrSize, "Pointer2");
  block1->SetLabel(2 * kPtrSize, "Pointer3");
  TypedBlock<uint32> data1;
  ASSERT_TRUE(data1.Init(0, block1));
  data1[0] = 0xAAAAAAAA;
  data1[1] = 0xBBBBBBBB;
  data1[2] = 0xCCCCCCCC;

  // Create a block with a pointer to the first entry of block1.
  BlockGraph::Block* block2 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, kPtrSize, "Block2");
  block2->SetReference(0, BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                                kPtrSize,
                                                block1,
                                                0));

  // Create a block with a pointer to the second entry of block1.
  BlockGraph::Block* block3 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, kPtrSize, "Block3");
  block3->SetReference(0, BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                                kPtrSize,
                                                block1,
                                                kPtrSize));

  // Insert a new pointer entry in between the first and second entries.
  block1->InsertData(kPtrSize, kPtrSize, false);

  // Ensure the data_size and block size are as expected.
  EXPECT_EQ(5 * kPtrSize, block1->size());
  EXPECT_EQ(4 * kPtrSize, block1->data_size());

  // Ensure the source ranges are as expected.
  BlockGraph::Block::SourceRanges expected_src_ranges;
  expected_src_ranges.Push(
      BlockGraph::Block::DataRange(0, kPtrSize),
      BlockGraph::Block::SourceRange(core::RelativeAddress(0), kPtrSize));
  expected_src_ranges.Push(
      BlockGraph::Block::DataRange(2 * kPtrSize, 3 * kPtrSize),
      BlockGraph::Block::SourceRange(core::RelativeAddress(kPtrSize),
                                     3 * kPtrSize));
  EXPECT_THAT(expected_src_ranges.range_pairs(),
              testing::ContainerEq(block1->source_ranges().range_pairs()));

  // Ensure that the contents of the block's data are as expected.
  EXPECT_EQ(0xAAAAAAAAu, data1[0]);
  EXPECT_EQ(0x00000000u, data1[1]);
  EXPECT_EQ(0xBBBBBBBBu, data1[2]);
  EXPECT_EQ(0xCCCCCCCCu, data1[3]);

  // Ensure that the labels have been shifted appropriately.
  BlockGraph::Block::LabelMap expected_labels;
  expected_labels.insert(std::make_pair(0, "Pointer1"));
  expected_labels.insert(std::make_pair(2 * kPtrSize, "Pointer2"));
  expected_labels.insert(std::make_pair(3 * kPtrSize, "Pointer3"));
  EXPECT_THAT(expected_labels, testing::ContainerEq(block1->labels()));

  // Ensure that the referrers are as expected.
  BlockGraph::Block::ReferrerSet expected_referrers;
  expected_referrers.insert(std::make_pair(block2, 0));
  expected_referrers.insert(std::make_pair(block3, 0));
  EXPECT_THAT(expected_referrers, testing::ContainerEq(block1->referrers()));

  BlockGraph::Reference expected_ref(BlockGraph::RELATIVE_REF,
                                     kPtrSize,
                                     block1,
                                     0);
  BlockGraph::Reference actual_ref;
  EXPECT_TRUE(block2->GetReference(0, &actual_ref));
  EXPECT_EQ(expected_ref, actual_ref);

  expected_ref = BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                       kPtrSize,
                                       block1,
                                       2 * kPtrSize);
  EXPECT_TRUE(block3->GetReference(0, &actual_ref));
  EXPECT_EQ(expected_ref, actual_ref);

  // Ensure that the references have been shifted appropriately.
  BlockGraph::Block::ReferenceMap expected_references;
  expected_references.insert(std::make_pair(0, outgoing_ref));
  expected_references.insert(std::make_pair(2 * kPtrSize, outgoing_ref));
  EXPECT_EQ(expected_references, block1->references());
}

TEST_F(BlockTest, InsertDataImplicit) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, 40, "Block1");
  block1->AllocateData(30);

  // Do an insert data in the implicitly initialized portion of the block.
  block1->InsertData(30, 10, false);

  // We expect the block to have grown, but the data size should still be the
  // same.
  EXPECT_EQ(50u, block1->size());
  EXPECT_EQ(30u, block1->data_size());
}

TEST_F(BlockTest, InsertDataImplicitForceAllocation) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, 40, "Block1");
  block1->AllocateData(30);

  // Do an insert data in the implicitly initialized portion of the block, but
  // force data to be allocated.
  block1->InsertData(30, 10, true);

  // We expect the block to have grown, as well as the data size.
  EXPECT_EQ(50u, block1->size());
  EXPECT_EQ(40u, block1->data_size());
}

TEST_F(BlockTest, InsertDataForceAllocateDoesNotShorten) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, 40, "Block1");
  block1->AllocateData(30);

  // Insert data in the allocated region, but request allocation to be forced.
  block1->InsertData(0, 10, true);

  EXPECT_EQ(50u, block1->size());
  EXPECT_EQ(40u, block1->data_size());
}

TEST_F(BlockTest, RemoveData) {
  // Create a block with a labelled array of pointers. Explicitly initialize
  // the last one with some data and let the block be longer than its
  // explicitly initialized length.
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, 6 * kPtrSize, "Block1");
  block1->AllocateData(3 * kPtrSize);
  block1->source_ranges().Push(BlockGraph::Block::DataRange(0, 6 * kPtrSize),
                               BlockGraph::Block::SourceRange(
                                   core::RelativeAddress(0), 6 * kPtrSize));
  BlockGraph::Reference outgoing_ref(BlockGraph::RELATIVE_REF,
                                     kPtrSize,
                                     block_,
                                     0);
  block1->SetReference(0, outgoing_ref);
  block1->SetReference(2 * kPtrSize, outgoing_ref);
  block1->SetReference(5 * kPtrSize, outgoing_ref);
  block1->SetLabel(0, "Pointer1");
  block1->SetLabel(2 * kPtrSize, "Pointer3");
  block1->SetLabel(3 * kPtrSize, "EndOfPointers");
  TypedBlock<uint32> data1;
  ASSERT_TRUE(data1.Init(0, block1));
  data1[0] = 0xAAAAAAAA;
  data1[1] = 0xBBBBBBBB;
  data1[2] = 0xCCCCCCCC;

  // Create a block with a pointer to the first entry of block1.
  BlockGraph::Block* block2 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, kPtrSize, "Block2");
  block2->SetReference(0, BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                                kPtrSize,
                                                block1,
                                                0));

  // Create a block with a pointer to the third entry of block1.
  BlockGraph::Block* block3 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, kPtrSize, "Block4");
  block3->SetReference(0, BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                                kPtrSize,
                                                block1,
                                                2 * kPtrSize));

  // Create a block with a pointer to the fifth entry of block1.
  BlockGraph::Block* block4 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, kPtrSize, "Block3");
  block4->SetReference(0, BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                                kPtrSize,
                                                block1,
                                                4 * kPtrSize));

  // Trying to remove the fourth entry should fail because it contains a label.
  EXPECT_FALSE(block1->RemoveData(3 * kPtrSize, kPtrSize));

  // Trying to remove the fifth entry should fail because there is a referrer
  // pointing to it.
  EXPECT_FALSE(block1->RemoveData(4 * kPtrSize, kPtrSize));

  // Trying to remove the sixth entry should fail because it contains a
  // reference.
  EXPECT_FALSE(block1->RemoveData(5 * kPtrSize, kPtrSize));

  // Finally, we should be able to delete the second entry.
  EXPECT_TRUE(block1->RemoveData(kPtrSize, kPtrSize));

  // Ensure the data_size and block size are as expected.
  EXPECT_EQ(5 * kPtrSize, block1->size());
  EXPECT_EQ(2 * kPtrSize, block1->data_size());

  // Ensure the source ranges are as expected.
  BlockGraph::Block::SourceRanges expected_src_ranges;
  expected_src_ranges.Push(
      BlockGraph::Block::DataRange(0, kPtrSize),
      BlockGraph::Block::SourceRange(core::RelativeAddress(0), kPtrSize));
  expected_src_ranges.Push(
      BlockGraph::Block::DataRange(kPtrSize, 4 * kPtrSize),
      BlockGraph::Block::SourceRange(core::RelativeAddress(2 * kPtrSize),
                                     4 * kPtrSize));
  EXPECT_THAT(expected_src_ranges.range_pairs(),
              testing::ContainerEq(block1->source_ranges().range_pairs()));

  // Ensure that the contents of the block's data are as expected.
  EXPECT_EQ(0xAAAAAAAAu, data1[0]);
  EXPECT_EQ(0xCCCCCCCCu, data1[1]);

  // Ensure that the labels have been shifted appropriately.
  BlockGraph::Block::LabelMap expected_labels;
  expected_labels.insert(std::make_pair(0, "Pointer1"));
  expected_labels.insert(std::make_pair(1 * kPtrSize, "Pointer3"));
  expected_labels.insert(std::make_pair(2 * kPtrSize, "EndOfPointers"));
  EXPECT_THAT(expected_labels, testing::ContainerEq(block1->labels()));

  // Ensure that the referrers are as expected.
  BlockGraph::Block::ReferrerSet expected_referrers;
  expected_referrers.insert(std::make_pair(block2, 0));
  expected_referrers.insert(std::make_pair(block3, 0));
  expected_referrers.insert(std::make_pair(block4, 0));
  EXPECT_THAT(expected_referrers, testing::ContainerEq(block1->referrers()));

  BlockGraph::Reference expected_ref(BlockGraph::RELATIVE_REF,
                                     kPtrSize,
                                     block1,
                                     0);
  BlockGraph::Reference actual_ref;
  EXPECT_TRUE(block2->GetReference(0, &actual_ref));
  EXPECT_EQ(expected_ref, actual_ref);

  expected_ref = BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                       kPtrSize,
                                       block1,
                                       kPtrSize);
  EXPECT_TRUE(block3->GetReference(0, &actual_ref));
  EXPECT_EQ(expected_ref, actual_ref);

  expected_ref = BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                                       kPtrSize,
                                       block1,
                                       3 * kPtrSize);
  EXPECT_TRUE(block4->GetReference(0, &actual_ref));
  EXPECT_EQ(expected_ref, actual_ref);

  // Ensure that the references have been shifted appropriately.
  BlockGraph::Block::ReferenceMap expected_references;
  expected_references.insert(std::make_pair(0, outgoing_ref));
  expected_references.insert(std::make_pair(kPtrSize, outgoing_ref));
  expected_references.insert(std::make_pair(4 * kPtrSize, outgoing_ref));
  EXPECT_EQ(expected_references, block1->references());
}

TEST_F(BlockTest, RemoveDataPartlyImplicit) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, 40, "Block1");
  block1->AllocateData(30);

  // Remove data that spans both the initialized and implicit parts of the
  // block.
  EXPECT_TRUE(block1->RemoveData(25, 10));

  // We expect both the block and the data size to have shrunk.
  EXPECT_EQ(30u, block1->size());
  EXPECT_EQ(25u, block1->data_size());
}

TEST_F(BlockTest, RemoveDataImplicit) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::CODE_BLOCK, 40, "Block1");
  block1->AllocateData(30);

  // Do an remove data in the implicitly initialized portion of the block.
  EXPECT_TRUE(block1->RemoveData(30, 5));

  // We expect the block to have shrunk, but the data size should still be the
  // same.
  EXPECT_EQ(35u, block1->size());
  EXPECT_EQ(30u, block1->data_size());
}

TEST_F(BlockTest, InsertOrRemoveDataSameSizeNoAllocate) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::DATA_BLOCK, 40, "Block1");

  EXPECT_TRUE(block1->InsertOrRemoveData(0, 20, 20, false));
  EXPECT_EQ(40u, block1->size());
  EXPECT_EQ(0u, block1->data_size());
}

TEST_F(BlockTest, InsertOrRemoveDataSameSizeAllocate) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::DATA_BLOCK, 40, "Block1");

  EXPECT_TRUE(block1->InsertOrRemoveData(0, 20, 20, true));
  EXPECT_EQ(40u, block1->size());
  EXPECT_EQ(20u, block1->data_size());
}

TEST_F(BlockTest, InsertOrRemoveGrowNoAllocate) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::DATA_BLOCK, 40, "Block1");

  EXPECT_TRUE(block1->InsertOrRemoveData(0, 10, 20, false));
  EXPECT_EQ(50u, block1->size());
  EXPECT_EQ(0u, block1->data_size());
}

TEST_F(BlockTest, InsertOrRemoveGrowAllocate) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::DATA_BLOCK, 40, "Block1");

  EXPECT_TRUE(block1->InsertOrRemoveData(0, 10, 20, true));
  EXPECT_EQ(50u, block1->size());
  EXPECT_EQ(20u, block1->data_size());
}

TEST_F(BlockTest, InsertOrRemoveShrinkNoAllocate) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::DATA_BLOCK, 40, "Block1");
  block1->AllocateData(15);

  EXPECT_TRUE(block1->InsertOrRemoveData(0, 20, 10, false));
  EXPECT_EQ(30u, block1->size());
  EXPECT_EQ(10u, block1->data_size());
}

TEST_F(BlockTest, InsertOrRemoveShrinkAllocate) {
  BlockGraph::Block* block1 = image_.AddBlock(
      BlockGraph::DATA_BLOCK, 40, "Block1");

  EXPECT_TRUE(block1->InsertOrRemoveData(0, 20, 10, true));
  EXPECT_EQ(30u, block1->size());
  EXPECT_EQ(10u, block1->data_size());
}

TEST(BlockGraphTest, AddSections) {
  BlockGraph image;
  ASSERT_EQ(0u, image.sections().size());

  BlockGraph::Section* section0 = image.AddSection("foo", 0);
  ASSERT_TRUE(section0 != NULL);
  ASSERT_EQ("foo", section0->name());
  ASSERT_EQ(0u, section0->characteristics());
  ASSERT_EQ(1u, image.sections().size());

  BlockGraph::Section* section1 = image.AddSection("foo", 0);
  ASSERT_TRUE(section1 != NULL);
  ASSERT_EQ("foo", section1->name());
  ASSERT_EQ(0u, section1->characteristics());
  ASSERT_EQ(2u, image.sections().size());

  // This section has the same name and characteristics, but it should not be
  // the same section as section0.
  EXPECT_TRUE(section0 != section1);
  EXPECT_NE(section0->id(), section1->id());

  BlockGraph::Section* section2 = image.FindOrAddSection("foo", 1);
  ASSERT_TRUE(section2 != NULL);
  ASSERT_EQ("foo", section2->name());
  ASSERT_EQ(1u, section2->characteristics());
  ASSERT_EQ(2u, image.sections().size());

  // This should be the same as section0, the first instance of a section
  // with name 'foo'.
  EXPECT_EQ(section0, section2);

  BlockGraph::Section* section3 = image.FindOrAddSection("bar", 1);
  ASSERT_TRUE(section3 != NULL);
  ASSERT_EQ("bar", section3->name());
  ASSERT_EQ(1u, section3->characteristics());
  ASSERT_EQ(3u, image.sections().size());

  // Test out FindSection.
  EXPECT_EQ(section0, image.FindSection("foo"));
  EXPECT_EQ(section3, image.FindSection("bar"));
  EXPECT_TRUE(image.FindSection("baz") == NULL);
}

TEST(BlockGraphTest, RemoveSection) {
  BlockGraph image;
  ASSERT_EQ(0u, image.sections().size());

  BlockGraph::Section* section0 = image.AddSection("foo", 0);
  ASSERT_TRUE(section0 != NULL);
  ASSERT_EQ(1u, image.sections().size());

  BlockGraph::Section* section1 = image.AddSection("bar", 0);
  ASSERT_TRUE(section1 != NULL);
  ASSERT_EQ(2u, image.sections().size());

  // We should not be able to delete a non-existent section.
  EXPECT_FALSE(image.RemoveSectionById(BlockGraph::kInvalidSectionId));
  ASSERT_EQ(2u, image.sections().size());

  // Deleting normal sections should work just fine.

  EXPECT_TRUE(image.RemoveSectionById(section0->id()));
  ASSERT_EQ(1u, image.sections().size());

  EXPECT_TRUE(image.RemoveSection(section1));
  ASSERT_EQ(0u, image.sections().size());
}

TEST(BlockGraphTest, RemoveBlock) {
  BlockGraph image;

  // Add some blocks to the image.
  BlockGraph::Block* b1 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b1");
  BlockGraph::Block* b2 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b2");
  BlockGraph::Block* b3 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b3");
  BlockGraph::Block* b4 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b4");
  ASSERT_TRUE(b1 != NULL);
  ASSERT_TRUE(b2 != NULL);
  ASSERT_TRUE(b3 != NULL);
  ASSERT_TRUE(b4 != NULL);
  EXPECT_EQ(4u, image.blocks().size());

  // Add a reference from block 1 to block 2.
  BlockGraph::Reference ref12(BlockGraph::PC_RELATIVE_REF, 1, b2, 9);
  ASSERT_TRUE(b1->SetReference(0, ref12));
  EXPECT_THAT(b1->references(), testing::Contains(std::make_pair(0, ref12)));
  EXPECT_THAT(b2->referrers(), testing::Contains(std::make_pair(b1, 0)));
  EXPECT_EQ(1u, b1->references().size());
  EXPECT_EQ(1u, b2->referrers().size());

  // Try to delete Block 1. This should fail because it has references.
  ASSERT_FALSE(image.RemoveBlock(b1));
  EXPECT_EQ(4u, image.blocks().size());

  // Try to delete Block 2. This should fail because it has referrers.
  ASSERT_FALSE(image.RemoveBlockById(b2->id()));
  EXPECT_EQ(4u, image.blocks().size());

  // Try to delete a block that doesn't belong to the block graph. This
  // should fail.
  BlockGraph other_image;
  BlockGraph::Block* other_block =
      other_image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "other_block");
  ASSERT_FALSE(image.RemoveBlock(other_block));
  EXPECT_EQ(4u, image.blocks().size());

  // Try to delete a block with an invalid ID. This should fail.
  ASSERT_FALSE(image.RemoveBlockById(15));
  EXPECT_EQ(4u, image.blocks().size());

  // Delete block 3.
  ASSERT_TRUE(image.RemoveBlock(b3));
  EXPECT_EQ(3u, image.blocks().size());

  // Delete block 4.
  ASSERT_TRUE(image.RemoveBlockById(b4->id()));
  EXPECT_EQ(2u, image.blocks().size());
}

TEST(BlockGraphTest, References) {
  BlockGraph image;

  BlockGraph::Block* b1 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b1");
  BlockGraph::Block* b2 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b2");
  BlockGraph::Block* b3 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b3");
  ASSERT_TRUE(b1 != NULL);
  ASSERT_TRUE(b2 != NULL);
  ASSERT_TRUE(b3 != NULL);

  ASSERT_TRUE(b1->references().empty());
  ASSERT_TRUE(b1->referrers().empty());
  ASSERT_TRUE(b2->references().empty());
  ASSERT_TRUE(b2->referrers().empty());
  ASSERT_TRUE(b3->references().empty());
  ASSERT_TRUE(b3->referrers().empty());

  BlockGraph::Reference dummy;
  ASSERT_FALSE(dummy.IsValid());

  // Add the first reference, and test that we get a backref.
  BlockGraph::Reference r_pc(BlockGraph::PC_RELATIVE_REF, 1, b2, 9);
  ASSERT_TRUE(r_pc.IsValid());
  ASSERT_EQ(BlockGraph::PC_RELATIVE_REF, r_pc.type());
  ASSERT_EQ(1, r_pc.size());
  ASSERT_EQ(b2, r_pc.referenced());
  ASSERT_EQ(9, r_pc.offset());

  ASSERT_TRUE(b1->SetReference(0, r_pc));
  EXPECT_THAT(b2->referrers(), testing::Contains(std::make_pair(b1, 0)));

  ASSERT_TRUE(b1->SetReference(1, r_pc));
  EXPECT_THAT(b2->referrers(), testing::Contains(std::make_pair(b1, 1)));

  BlockGraph::Reference r_abs(BlockGraph::ABSOLUTE_REF, 4, b2, 13);
  ASSERT_FALSE(b1->SetReference(1, r_abs));
  BlockGraph::Reference r_rel(BlockGraph::RELATIVE_REF, 4, b2, 17);
  ASSERT_TRUE(b1->SetReference(5, r_rel));
  BlockGraph::Reference r_file(BlockGraph::FILE_OFFSET_REF, 4, b2, 23);
  ASSERT_TRUE(b1->SetReference(9, r_file));

  // Test that the reference map is as expected.
  BlockGraph::Block::ReferenceMap expected;
  expected.insert(std::make_pair(0, r_pc));
  expected.insert(std::make_pair(1, r_abs));
  expected.insert(std::make_pair(5, r_rel));
  expected.insert(std::make_pair(9, r_file));
  EXPECT_THAT(b1->references(), testing::ContainerEq(expected));

  // Test reference transfer.
  // This should fail, as all the references will fall outside b3.
  // TODO(chrisha): We need to create a logging MessageHandler that we can
  //     put test expectations on. This test is meant to fail, but we don't
  //     want to see the error message it would produce! Ideally, this should
  //     live in 'syzygy/testing' or something of the like, as it could be
  //     used across many unittests. For now, we simply disable logging for
  //     this test.
  int old_level = logging::GetMinLogLevel();
  logging::SetMinLogLevel(logging::LOG_FATAL);
  ASSERT_FALSE(b2->TransferReferrers(b3->size(), b3));
  logging::SetMinLogLevel(old_level);

  // Now move the references from b2 to b3
  ASSERT_TRUE(b2->TransferReferrers(0, b3));
  // Test that b2 no longer has referrers.
  EXPECT_THAT(b2->referrers(), BlockGraph::Block::ReferrerSet());

  // Test that the references transferred as expected.
  expected.clear();
  expected.insert(std::make_pair(0,
      BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF, 1, b3, 9)));
  expected.insert(std::make_pair(1,
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF, 4, b3, 13)));
  expected.insert(std::make_pair(5,
      BlockGraph::Reference(BlockGraph::RELATIVE_REF, 4, b3, 17)));
  expected.insert(std::make_pair(9,
      BlockGraph::Reference(BlockGraph::FILE_OFFSET_REF, 4, b3, 23)));
  EXPECT_THAT(b1->references(), testing::ContainerEq(expected));

  // Remove the references.
  ASSERT_TRUE(b1->RemoveReference(0));
  ASSERT_TRUE(b1->RemoveReference(1));
  ASSERT_TRUE(b1->RemoveReference(5));
  ASSERT_TRUE(b1->RemoveReference(9));
  EXPECT_THAT(b1->references(), BlockGraph::Block::ReferenceMap());

  EXPECT_THAT(b2->referrers(), BlockGraph::Block::ReferrerSet());
}

TEST(BlockGraphTest, Labels) {
  BlockGraph image;

  BlockGraph::Block* block =
      image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "labeled");
  ASSERT_TRUE(block->labels().empty());
  for (int i = 0; i < 0x20; ++i) {
    std::string label;
    ASSERT_FALSE(block->HasLabel(i));
    EXPECT_FALSE(block->GetLabel(i, &label));
    EXPECT_FALSE(block->RemoveLabel(i));
  }

  EXPECT_TRUE(block->SetLabel(13, "foo"));
  EXPECT_FALSE(block->SetLabel(13, "foo2"));

  EXPECT_TRUE(block->SetLabel(17, "bar"));
  EXPECT_FALSE(block->SetLabel(17, "bar2"));

  EXPECT_TRUE(block->SetLabel(15, "baz"));
  EXPECT_TRUE(block->HasLabel(15));
  EXPECT_TRUE(block->RemoveLabel(15));
  EXPECT_FALSE(block->HasLabel(15));

  for (int i = 0; i < 0x20; ++i) {
    std::string label;
    if (i == 13 || i == 17) {
      ASSERT_TRUE(block->HasLabel(i));
      EXPECT_TRUE(block->GetLabel(i, &label));
      EXPECT_EQ(std::string(i == 13 ? "foo" : "bar"), label);
    } else {
      ASSERT_FALSE(block->HasLabel(i));
      EXPECT_FALSE(block->GetLabel(i, &label));
    }
  }

  BlockGraph::Block::LabelMap expected;
  expected.insert(std::make_pair(13, "foo"));
  expected.insert(std::make_pair(17, "bar"));
  EXPECT_THAT(block->labels(), testing::ContainerEq(expected));
}

TEST(BlockGraphTest, Serialization) {
  BlockGraph image;

  BlockGraph::Section* s1 = image.AddSection("s1", 0);
  BlockGraph::Section* s2 = image.AddSection("s2", 0);
  ASSERT_TRUE(s1 != NULL);
  ASSERT_TRUE(s2 != NULL);

  BlockGraph::Block* b1 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b1");
  BlockGraph::Block* b2 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b2");
  BlockGraph::Block* b3 = image.AddBlock(BlockGraph::CODE_BLOCK, 0x20, "b3");
  ASSERT_TRUE(b1 != NULL);
  ASSERT_TRUE(b2 != NULL);
  ASSERT_TRUE(b3 != NULL);

  b1->set_section(s1->id());
  b2->set_section(s1->id());
  b3->set_section(s2->id());
  ASSERT_EQ(b1->section(), s1->id());
  ASSERT_EQ(b2->section(), s1->id());
  ASSERT_EQ(b3->section(), s2->id());

  uint8* b1_data = b1->AllocateData(b1->size());
  for (size_t i = 0; i < b1->size(); ++i) {
    b1_data[i] = 0;
  }

  ASSERT_TRUE(b1->references().empty());
  ASSERT_TRUE(b1->referrers().empty());
  ASSERT_TRUE(b2->references().empty());
  ASSERT_TRUE(b2->referrers().empty());
  ASSERT_TRUE(b3->references().empty());
  ASSERT_TRUE(b3->referrers().empty());

  BlockGraph::Reference r_pc(BlockGraph::PC_RELATIVE_REF, 1, b2, 9);
  ASSERT_TRUE(b1->SetReference(0, r_pc));
  ASSERT_TRUE(b1->SetReference(1, r_pc));

  BlockGraph::Reference r_abs(BlockGraph::ABSOLUTE_REF, 4, b2, 13);
  ASSERT_FALSE(b1->SetReference(1, r_abs));

  BlockGraph::Reference r_rel(BlockGraph::RELATIVE_REF, 4, b2, 17);
  ASSERT_TRUE(b1->SetReference(5, r_rel));

  BlockGraph::Reference r_file(BlockGraph::FILE_OFFSET_REF, 4, b2, 23);
  ASSERT_TRUE(b1->SetReference(9, r_file));

  ByteVector byte_vector;
  ScopedOutStreamPtr out_stream(
      CreateByteOutStream(std::back_inserter(byte_vector)));
  NativeBinaryOutArchive out_archive(out_stream.get());
  ASSERT_TRUE(out_archive.Save(image));

  BlockGraph image_copy;
  ScopedInStreamPtr in_stream(
      CreateByteInStream(byte_vector.begin(), byte_vector.end()));
  NativeBinaryInArchive in_archive(in_stream.get());
  ASSERT_TRUE(in_archive.Load(&image_copy));

  EXPECT_TRUE(testing::BlockGraphsEqual(image, image_copy));
}

TEST(BlockGraphAddressSpaceTest, AddBlock) {
  BlockGraph image;
  BlockGraph::AddressSpace address_space(&image);

  // We should be able to insert this block.
  BlockGraph::Block* block = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                    RelativeAddress(0x1000),
                                                    0x20,
                                                    "code");
  ASSERT_TRUE(block != NULL);
  EXPECT_EQ(0x1000, block->addr().value());

  // But inserting anything that intersects with it should fail.
  EXPECT_EQ(NULL, address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                         RelativeAddress(0x1000),
                                         0x20,
                                         "code"));

  // Overlapping from below.
  EXPECT_EQ(NULL, address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                         RelativeAddress(0xFF0),
                                         0x20,
                                         "code"));
  // Enclosing.
  EXPECT_EQ(NULL, address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                         RelativeAddress(0xFF0),
                                         0x30,
                                         "code"));
  // Itersecting to end.
  EXPECT_EQ(NULL, address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                         RelativeAddress(0x1010),
                                         0x10,
                                         "code"));
  // Intersecting, overlapping the back.
  EXPECT_EQ(NULL, address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                         RelativeAddress(0x1010),
                                         0x20,
                                         "code"));

  // We should be able to insert blocks above and below the one above.
  EXPECT_TRUE(address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                     RelativeAddress(0xFF0),
                                     0x10,
                                     "code") != NULL);
  EXPECT_TRUE(address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                     RelativeAddress(0x1020),
                                     0x10,
                                     "code") != NULL);
}

TEST(BlockGraphAddressSpaceTest, InsertBlock) {
  BlockGraph image;
  BlockGraph::AddressSpace address_space(&image);

  BlockGraph::Block* block1 =
      image.AddBlock(BlockGraph::CODE_BLOCK, 0x10, "code");
  BlockGraph::Block* block2 =
      image.AddBlock(BlockGraph::CODE_BLOCK, 0x10, "code");
  BlockGraph::Block* block3 =
      image.AddBlock(BlockGraph::CODE_BLOCK, 0x10, "code");

  ASSERT_TRUE(address_space.InsertBlock(RelativeAddress(0x1000), block1));
  ASSERT_FALSE(address_space.InsertBlock(RelativeAddress(0x1000), block2));
  ASSERT_TRUE(address_space.InsertBlock(RelativeAddress(0x1010), block2));
  ASSERT_FALSE(address_space.InsertBlock(RelativeAddress(0x1018), block3));
  ASSERT_TRUE(address_space.InsertBlock(RelativeAddress(0x1030), block3));

  RelativeAddress addr;
  EXPECT_TRUE(address_space.GetAddressOf(block1, &addr));
  EXPECT_EQ(0x1000, addr.value());
  EXPECT_EQ(0x1000, block1->addr().value());

  EXPECT_TRUE(address_space.GetAddressOf(block2, &addr));
  EXPECT_EQ(0x1010, addr.value());
  EXPECT_EQ(0x1010, block2->addr().value());

  EXPECT_TRUE(address_space.GetAddressOf(block3, &addr));
  EXPECT_EQ(0x1030, addr.value());
  EXPECT_EQ(0x1030, block3->addr().value());

  // Insert a block into a second address space.
  BlockGraph::AddressSpace address_space2(&image);
  EXPECT_TRUE(address_space2.InsertBlock(RelativeAddress(0x2000), block1));
  EXPECT_TRUE(address_space.GetAddressOf(block1, &addr));
  EXPECT_EQ(0x1000, addr.value());

  EXPECT_TRUE(address_space2.GetAddressOf(block1, &addr));
  EXPECT_EQ(0x2000, addr.value());

  EXPECT_EQ(0x2000, block1->addr().value());
}

TEST(BlockGraphAddressSpaceTest, GetBlockByAddress) {
  BlockGraph image;
  BlockGraph::AddressSpace address_space(&image);

  BlockGraph::Block* block1 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1000),
                                                     0x10,
                                                     "code");
  BlockGraph::Block* block2 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1010),
                                                     0x10,
                                                     "code");
  BlockGraph::Block* block3 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1030),
                                                     0x10,
                                                     "code");

  EXPECT_EQ(NULL, address_space.GetBlockByAddress(RelativeAddress(0xFFF)));

  EXPECT_EQ(block1, address_space.GetBlockByAddress(RelativeAddress(0x1000)));
  EXPECT_EQ(block1, address_space.GetBlockByAddress(RelativeAddress(0x100F)));

  EXPECT_EQ(block2, address_space.GetBlockByAddress(RelativeAddress(0x1010)));
  EXPECT_EQ(block2, address_space.GetBlockByAddress(RelativeAddress(0x101F)));

  EXPECT_EQ(NULL, address_space.GetBlockByAddress(RelativeAddress(0x1020)));
  EXPECT_EQ(NULL, address_space.GetBlockByAddress(RelativeAddress(0x102F)));

  EXPECT_EQ(block3, address_space.GetBlockByAddress(RelativeAddress(0x1030)));
  EXPECT_EQ(block3, address_space.GetBlockByAddress(RelativeAddress(0x103F)));

  EXPECT_EQ(NULL, address_space.GetBlockByAddress(RelativeAddress(0x1040)));
}

TEST(BlockGraphAddressSpaceTest, GetFirstIntersectingBlock) {
  BlockGraph image;
  BlockGraph::AddressSpace address_space(&image);

  BlockGraph::Block* block1 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1000),
                                                     0x10,
                                                     "code");
  BlockGraph::Block* block2 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1010),
                                                     0x10,
                                                     "code");
  BlockGraph::Block* block3 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1030),
                                                     0x10,
                                                     "code");

  EXPECT_EQ(NULL,
      address_space.GetFirstIntersectingBlock(RelativeAddress(0xFFF), 0x1));
  EXPECT_EQ(block1,
      address_space.GetFirstIntersectingBlock(RelativeAddress(0xFFF), 0x2));
  EXPECT_EQ(block1,
      address_space.GetFirstIntersectingBlock(RelativeAddress(0x100F), 0x1));
  EXPECT_EQ(block1,
      address_space.GetFirstIntersectingBlock(RelativeAddress(0x100F), 0x2));

  EXPECT_EQ(block2,
      address_space.GetFirstIntersectingBlock(RelativeAddress(0x1010), 0x40));
}

TEST(BlockGraphAddressSpaceTest, GetBlockAddress) {
  BlockGraph image;
  BlockGraph::AddressSpace address_space(&image);

  BlockGraph::Block* block1 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1000),
                                                     0x10,
                                                     "code");
  BlockGraph::Block* block2 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     RelativeAddress(0x1010),
                                                     0x10,
                                                     "code");
  BlockGraph::Block* block3 =
      image.AddBlock(BlockGraph::CODE_BLOCK, 0x10, "code");

  RelativeAddress addr;
  EXPECT_TRUE(address_space.GetAddressOf(block1, &addr));
  EXPECT_EQ(0x1000, addr.value());

  EXPECT_TRUE(address_space.GetAddressOf(block2, &addr));
  EXPECT_EQ(0x1010, addr.value());

  EXPECT_FALSE(address_space.GetAddressOf(block3, &addr));
}

TEST(BlockGraphAddressSpaceTest, MergeIntersectingBlocks) {
  BlockGraph image;
  BlockGraph::AddressSpace address_space(&image);
  RelativeAddress addr1(0x1000);
  RelativeAddress addr2(0x1010);
  RelativeAddress addr3(0x1030);
  BlockGraph::Block* block1 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     addr1,
                                                     0x10,
                                                     "block1");
  BlockGraph::Block* block2 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     addr2,
                                                     0x10,
                                                     "block2");
  BlockGraph::Block* block3 = address_space.AddBlock(BlockGraph::CODE_BLOCK,
                                                     addr3,
                                                     0x10,
                                                     "block3");
  ASSERT_TRUE(block2->SetLabel(0, "0x1010"));
  ASSERT_TRUE(block2->SetLabel(4, "0x1014"));
  ASSERT_TRUE(block3->SetLabel(0, "0x1030"));
  ASSERT_TRUE(block3->SetLabel(4, "0x1034"));

  block1->source_ranges().Push(BlockGraph::Block::DataRange(0, 0x10),
                               BlockGraph::Block::SourceRange(addr1, 0x10));
  block2->source_ranges().Push(BlockGraph::Block::DataRange(0, 0x10),
                               BlockGraph::Block::SourceRange(addr2, 0x10));
  block3->source_ranges().Push(BlockGraph::Block::DataRange(0, 0x10),
                               BlockGraph::Block::SourceRange(addr3, 0x10));

  ASSERT_TRUE(block1->SetReference(0x1,
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF, 4, block2, 0x0)));
  ASSERT_TRUE(block1->SetReference(0x6,
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF, 4, block3, 0x0)));
  ASSERT_TRUE(block2->SetReference(0x1,
      BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF, 1, block1, 0x4)));
  ASSERT_TRUE(block2->SetReference(0x6,
      BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF, 4, block3, 0x4)));
  ASSERT_TRUE(block3->SetReference(0x1,
      BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF, 4, block2, 0x4)));

  BlockGraph::Block* merged = address_space.MergeIntersectingBlocks(
      BlockGraph::AddressSpace::Range(RelativeAddress(0x1014), 0x30));

  ASSERT_TRUE(merged != NULL);
  ASSERT_EQ(RelativeAddress(0x1010), merged->addr());
  ASSERT_EQ(0x34, merged->size());

  // Expect the merged block to have meaningful source ranges.
  BlockGraph::Block::SourceRanges::RangePairs expected_source_ranges;
  expected_source_ranges.push_back(
      std::make_pair(BlockGraph::Block::DataRange(0, 0x10),
                     BlockGraph::Block::SourceRange(addr2, 0x10)));
  expected_source_ranges.push_back(
      std::make_pair(BlockGraph::Block::DataRange(0x20, 0x10),
                     BlockGraph::Block::SourceRange(addr3, 0x10)));
  EXPECT_THAT(merged->source_ranges().range_pairs(),
              testing::ContainerEq(expected_source_ranges));

  BlockGraph::Block::LabelMap expected_labels;
  expected_labels.insert(std::make_pair(0x00, "0x1010"));
  expected_labels.insert(std::make_pair(0x04, "0x1014"));
  expected_labels.insert(std::make_pair(0x20, "0x1030"));
  expected_labels.insert(std::make_pair(0x24, "0x1034"));
  EXPECT_THAT(merged->labels(), testing::ContainerEq(expected_labels));

  BlockGraph::Block::ReferenceMap expected_refs;
  expected_refs.insert(std::make_pair(0x1,
      BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF, 1, block1, 0x4)));
  expected_refs.insert(std::make_pair(0x6,
      BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF, 4, merged, 0x24)));
  expected_refs.insert(std::make_pair(0x21,
      BlockGraph::Reference(BlockGraph::PC_RELATIVE_REF, 4, merged, 0x4)));
  EXPECT_THAT(merged->references(), testing::ContainerEq(expected_refs));

  expected_refs.clear();
  expected_refs.insert(std::make_pair(0x1,
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF, 4, merged, 0x0)));
  expected_refs.insert(std::make_pair(0x6,
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF, 4, merged, 0x20)));
  EXPECT_THAT(block1->references(), testing::ContainerEq(expected_refs));

  // We expect that the block graph and the address space have the same size,
  // as MergeIntersectingBlocks deletes the old blocks from the BlockGraph.
  EXPECT_EQ(image.blocks().size(), address_space.address_space_impl().size());
}

}  // namespace block_graph
