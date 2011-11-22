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
// Unittests for iteration primitives.

#include "syzygy/block_graph/transforms/iterative_transform.h"

#include "base/bind.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace block_graph {
namespace transforms {

namespace {

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

class IterativeTransformTest : public testing::Test {
 public:
  IterativeTransformTest() : header_block_(NULL) { }

  virtual void SetUp() {
    header_block_ = block_graph_.AddBlock(BlockGraph::DATA_BLOCK, 10, "Header");
    BlockGraph::Block* block =
        block_graph_.AddBlock(BlockGraph::DATA_BLOCK, 10, "Data");
    ASSERT_TRUE(block != NULL);
  }

 protected:
  BlockGraph block_graph_;
  BlockGraph::Block* header_block_;
};

class MockIterativeTransform
    : public IterativeTransformImpl<MockIterativeTransform> {
 public:
  MOCK_METHOD2(PreIteration, bool(BlockGraph*, BlockGraph::Block*));
  MOCK_METHOD2(OnBlock, bool(BlockGraph*, BlockGraph::Block*));
  MOCK_METHOD2(PostIteration, bool(BlockGraph*, BlockGraph::Block*));

  bool DeleteBlock(BlockGraph* block_graph,
                   BlockGraph::Block* block) {
    return block_graph->RemoveBlock(block);
  }

  bool AddBlock(BlockGraph* block_graph,
                BlockGraph::Block* block) {
    BlockGraph::Block* new_block =
        block_graph->AddBlock(BlockGraph::DATA_BLOCK, 10, "Added");
    return new_block != NULL;
  }
};

}  // namespace

const char NamedTransformImpl<MockIterativeTransform>::kTransformName[] =
    "MockIterativeTransform";

TEST_F(IterativeTransformTest, PreIterationFails) {
  StrictMock<MockIterativeTransform> transform;
  EXPECT_CALL(transform, PreIteration(_, _)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(transform, OnBlock(_, _)).Times(0);
  EXPECT_CALL(transform, PostIteration(_, _)).Times(0);
  EXPECT_FALSE(transform.Apply(&block_graph_, header_block_));
  EXPECT_EQ(2u, block_graph_.blocks().size());
}

TEST_F(IterativeTransformTest, OnBlockFails) {
  StrictMock<MockIterativeTransform> transform;
  EXPECT_CALL(transform, PreIteration(_, _)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(transform, OnBlock(_, _)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(transform, PostIteration(_, _)).Times(0);
  EXPECT_FALSE(transform.Apply(&block_graph_, header_block_));
  EXPECT_EQ(2u, block_graph_.blocks().size());
}

TEST_F(IterativeTransformTest, PostIterationFails) {
  StrictMock<MockIterativeTransform> transform;
  EXPECT_CALL(transform, PreIteration(_, _)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(transform, OnBlock(_, _)).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(transform, PostIteration(_, _)).Times(1).WillOnce(Return(false));
  EXPECT_FALSE(transform.Apply(&block_graph_, header_block_));
  EXPECT_EQ(2u, block_graph_.blocks().size());
}

TEST_F(IterativeTransformTest, Normal) {
  StrictMock<MockIterativeTransform> transform;
  EXPECT_CALL(transform, PreIteration(_, _)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(transform, OnBlock(_, _)).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(transform, PostIteration(_, _)).Times(1).WillOnce(Return(true));
  EXPECT_TRUE(transform.Apply(&block_graph_, header_block_));
  EXPECT_EQ(2u, block_graph_.blocks().size());
}

TEST_F(IterativeTransformTest, Add) {
  StrictMock<MockIterativeTransform> transform;
  EXPECT_CALL(transform, PreIteration(_, _)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(transform, PostIteration(_, _)).Times(1).WillOnce(Return(true));

  EXPECT_CALL(transform, OnBlock(_, _)).Times(2).WillOnce(Return(true)).
      WillOnce(Invoke(&transform, &MockIterativeTransform::AddBlock));

  EXPECT_TRUE(transform.Apply(&block_graph_, header_block_));
  EXPECT_EQ(3u, block_graph_.blocks().size());
}

TEST_F(IterativeTransformTest, Delete) {
  StrictMock<MockIterativeTransform> transform;
  EXPECT_CALL(transform, PreIteration(_, _)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(transform, PostIteration(_, _)).Times(1).WillOnce(Return(true));

  EXPECT_CALL(transform, OnBlock(_, _)).Times(2).WillOnce(Return(true)).
      WillOnce(Invoke(&transform, &MockIterativeTransform::DeleteBlock));

  EXPECT_TRUE(transform.Apply(&block_graph_, header_block_));
  EXPECT_EQ(1u, block_graph_.blocks().size());
}

TEST_F(IterativeTransformTest, AddAndDelete) {
  StrictMock<MockIterativeTransform> transform;
  EXPECT_CALL(transform, PreIteration(_, _)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(transform, PostIteration(_, _)).Times(1).WillOnce(Return(true));

  EXPECT_CALL(transform, OnBlock(_, _)).Times(2).
      WillOnce(Invoke(&transform, &MockIterativeTransform::AddBlock)).
      WillOnce(Invoke(&transform, &MockIterativeTransform::DeleteBlock));

  EXPECT_TRUE(transform.Apply(&block_graph_, header_block_));
  EXPECT_EQ(2u, block_graph_.blocks().size());
}

}  // namespace transforms
}  // namespace block_graph
