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
#include "syzygy/block_graph/basic_block_assembler.h"

#include "gtest/gtest.h"
#include "syzygy/block_graph/basic_block_subgraph.h"

namespace block_graph {

namespace {

class BasicBlockAssemblerTest : public testing::Test {
 public:
  typedef BlockGraph::RelativeAddress RelativeAddress;
  typedef BlockGraph::Block::SourceRange SourceRange;

  BasicBlockAssemblerTest();

  void SetUp() OVERRIDE {
  }

  void TearDown() OVERRIDE {
  }

 protected:
  struct Ref {
    size_t offset;
    BasicBlockReference::ReferredType type;
    const void* reference;
  };

  template <size_t N>
  void AssertRefs(const Ref(& refs)[N]) {
    ASSERT_EQ(1, instructions_.size());
    const Instruction& instr = instructions_.front();

    for (size_t i = 0; i < N; ++i) {
      BasicBlock::BasicBlockReferenceMap::const_iterator it =
          instr.references().find(refs[i].offset);
      ASSERT_NE(instr.references().end(), it);
      ASSERT_EQ(refs[i].type, it->second.referred_type());
      switch (refs[i].type) {
        case BasicBlockReference::REFERRED_TYPE_BLOCK:
          ASSERT_EQ(refs[i].reference, it->second.block());
          break;
        case BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK:
          ASSERT_EQ(refs[i].reference, it->second.basic_block());
          break;
        default:
          ASSERT_TRUE(false);
      }

      ASSERT_EQ(refs[i].type, it->second.referred_type());
    }

    instructions_.clear();
  }

  void AssertNoRefs() {
    ASSERT_EQ(1, instructions_.size());
    ASSERT_EQ(0, instructions_.front().references().size());
    instructions_.clear();
  }

  BlockGraph block_graph_;
  BlockGraph::Block* test_block_;
  BasicBlockSubGraph subgraph_;
  BasicCodeBlock* test_bb_;
  BasicBlock::Instructions instructions_;
  BasicBlockAssembler asm_;
};

#define ASSERT_REFS(...) \
  do { \
    const Ref refs[] = { __VA_ARGS__ }; \
    ASSERT_NO_FATAL_FAILURE(AssertRefs(refs)); \
  } while (0)

#define ASSERT_NO_REFS() ASSERT_NO_FATAL_FAILURE(AssertNoRefs())

BasicBlockAssemblerTest::BasicBlockAssemblerTest()
    : test_block_(NULL),
      test_bb_(NULL),
      asm_(instructions_.end(), &instructions_) {
  test_block_ = block_graph_.AddBlock(BlockGraph::CODE_BLOCK, 10, "test block");
  test_bb_= subgraph_.AddBasicCodeBlock("foo");
}

void TestValue(const Value& value,
               uint32 expected_value,
               core::ValueSize expected_size) {
  EXPECT_EQ(expected_size, value.size());
  EXPECT_EQ(expected_value, value.value());
  EXPECT_EQ(BasicBlockReference::REFERRED_TYPE_UNKNOWN,
            value.reference().referred_type());

  Value value_copy(value);
  EXPECT_EQ(expected_size, value_copy.size());
  EXPECT_EQ(expected_value, value_copy.value());
  EXPECT_EQ(BasicBlockReference::REFERRED_TYPE_UNKNOWN,
            value_copy.reference().referred_type());

  EXPECT_TRUE(value == value_copy);

  Value value_diff(expected_value - 1);
  EXPECT_FALSE(value == value_diff);
}

void Test8BitValue(uint32 input_value, uint32 expected_value) {
  TestValue(Value(input_value), expected_value, core::kSize8Bit);
  TestValue(
      Value(input_value, core::kSize8Bit), expected_value, core::kSize8Bit);
}

void Test32BitValue(uint32 input_value, uint32 expected_value) {
  TestValue(Value(input_value), expected_value, core::kSize32Bit);
  TestValue(
      Value(input_value, core::kSize32Bit), expected_value, core::kSize32Bit);
}

typedef BasicBlockAssemblerTest ValueTest;

void TestValueCopy(const Value& input) {
  // Make sure copy-constructing the value works.
  Value copy(input);

  ASSERT_EQ(input.value(), copy.value());
  ASSERT_EQ(input.size(), copy.size());
  ASSERT_EQ(input.reference().referred_type(),
            copy.reference().referred_type());
  ASSERT_EQ(input.reference().block(), copy.reference().block());
  ASSERT_EQ(input.reference().basic_block(), copy.reference().basic_block());
  ASSERT_EQ(input.reference().offset(), copy.reference().offset());
  ASSERT_EQ(input.reference().base(), copy.reference().base());

  // Make sure assignment operator works.
  Value copy2;
  copy2 = input;
  ASSERT_EQ(input.value(), copy2.value());
  ASSERT_EQ(input.size(), copy2.size());
  ASSERT_EQ(input.reference().referred_type(),
            copy2.reference().referred_type());
  ASSERT_EQ(input.reference().block(), copy2.reference().block());
  ASSERT_EQ(input.reference().basic_block(), copy2.reference().basic_block());
  ASSERT_EQ(input.reference().offset(), copy2.reference().offset());
  ASSERT_EQ(input.reference().base(), copy2.reference().base());
}

}  // namespace

TEST(UntypedReferenceTest, DefaultConstructor) {
  UntypedReference r;
  EXPECT_EQ(NULL, r.basic_block());
  EXPECT_EQ(NULL, r.block());
  EXPECT_EQ(0, r.offset());
  EXPECT_EQ(0, r.base());
  EXPECT_FALSE(r.IsValid());
  EXPECT_EQ(BasicBlockReference::REFERRED_TYPE_UNKNOWN, r.referred_type());
}

TEST(UntypedReferenceTest, BasicBlockReferenceToBasicBlockConstructor) {
  BasicBlockSubGraph subgraph;
  BasicCodeBlock* bcb = subgraph.AddBasicCodeBlock("foo");
  BasicBlock* bb = bcb;
  BasicBlockReference bbref(BlockGraph::ABSOLUTE_REF, 4, bcb);
  UntypedReference r(bbref);
  EXPECT_EQ(bb, r.basic_block());
  EXPECT_EQ(NULL, r.block());
  EXPECT_EQ(0, r.offset());
  EXPECT_EQ(0, r.base());
  EXPECT_TRUE(r.IsValid());
  EXPECT_EQ(BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, r.referred_type());
}

TEST(UntypedReferenceTest, BasicBlockReferenceToBlockConstructor) {
  BlockGraph block_graph;
  BlockGraph::Block* b =
      block_graph.AddBlock(BlockGraph::CODE_BLOCK, 20, "foo");
  BasicBlockReference bbref(BlockGraph::ABSOLUTE_REF, 4, b, 4, 10);
  UntypedReference r(bbref);
  EXPECT_EQ(NULL, r.basic_block());
  EXPECT_EQ(b, r.block());
  EXPECT_EQ(4, r.offset());
  EXPECT_EQ(10, r.base());
  EXPECT_TRUE(r.IsValid());
  EXPECT_EQ(BasicBlockReference::REFERRED_TYPE_BLOCK, r.referred_type());
}

TEST(UntypedReferenceTest, BasicBlockConstructor) {
  BasicBlockSubGraph subgraph;
  BasicCodeBlock* bcb = subgraph.AddBasicCodeBlock("foo");
  BasicBlock* bb = bcb;
  UntypedReference r(bcb);
  EXPECT_EQ(bb, r.basic_block());
  EXPECT_EQ(NULL, r.block());
  EXPECT_EQ(0, r.offset());
  EXPECT_EQ(0, r.base());
  EXPECT_TRUE(r.IsValid());
  EXPECT_EQ(BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, r.referred_type());
}

TEST(UntypedReferenceTest, BlockConstructor) {
  BlockGraph block_graph;
  BlockGraph::Block* b =
      block_graph.AddBlock(BlockGraph::CODE_BLOCK, 0, "dummy");
  UntypedReference r(b, 4, 10);
  EXPECT_EQ(NULL, r.basic_block());
  EXPECT_EQ(b, r.block());
  EXPECT_EQ(4, r.offset());
  EXPECT_EQ(10, r.base());
  EXPECT_TRUE(r.IsValid());
  EXPECT_EQ(BasicBlockReference::REFERRED_TYPE_BLOCK, r.referred_type());
}

TEST(UntypedReferenceTest, CopyConstructor) {
  BlockGraph block_graph;
  BlockGraph::Block* b =
      block_graph.AddBlock(BlockGraph::CODE_BLOCK, 0, "dummy");
  UntypedReference r1(b, 4, 10);

  UntypedReference r2(r1);
  EXPECT_EQ(r1.basic_block(), r2.basic_block());
  EXPECT_EQ(r1.block(), r2.block());
  EXPECT_EQ(r1.offset(), r2.offset());
  EXPECT_EQ(r1.base(), r2.base());
  EXPECT_EQ(r1.IsValid(), r2.IsValid());
}

TEST(UntypedReferenceTest, Comparison) {
  BlockGraph block_graph;
  BasicBlockSubGraph subgraph;
  BlockGraph::Block* b =
      block_graph.AddBlock(BlockGraph::CODE_BLOCK, 0, "dummy");
  UntypedReference r1(b, 4, 10);

  UntypedReference r2(b, 0, 0);
  EXPECT_FALSE(r1 == r2);

  BasicCodeBlock* bcb = subgraph.AddBasicCodeBlock("foo");
  BasicBlock* bb = bcb;
  UntypedReference r3(bcb);
  EXPECT_FALSE(r1 == r3);
  EXPECT_FALSE(r2 == r3);

  UntypedReference r4(r1);
  EXPECT_TRUE(r1 == r4);

  UntypedReference r5(r2);
  EXPECT_TRUE(r2 == r5);
}

TEST_F(ValueTest, Construction) {
  {
    Value value_empty;
    ASSERT_EQ(0, value_empty.value());
    ASSERT_EQ(core::kSizeNone, value_empty.size());
    ASSERT_EQ(BasicBlockReference::REFERRED_TYPE_UNKNOWN,
              value_empty.reference().referred_type());

    TestValueCopy(value_empty);
  }

  Test8BitValue(0, 0);
  Test8BitValue(127, 127);

  Test8BitValue(-128, 0xFFFFFF80);
  Test8BitValue(0, 0);
  Test8BitValue(127, 0x0000007F);

  Test32BitValue(128, 0x00000080);
  Test32BitValue(0xCAFEBABE, 0xCAFEBABE);

  Test32BitValue(-129, 0xFFFFFF7F);
  Test32BitValue(128, 0x000000080);
  Test32BitValue(0xBABE, 0xBABE);

  {
    const BlockGraph::Offset kOffs = 10;
    Value value_block_ref(test_block_, kOffs);

    ASSERT_EQ(0, value_block_ref.value());
    ASSERT_EQ(core::kSize32Bit, value_block_ref.size());
    ASSERT_EQ(BasicBlockReference::REFERRED_TYPE_BLOCK,
              value_block_ref.reference().referred_type());
    ASSERT_EQ(test_block_, value_block_ref.reference().block());
    ASSERT_EQ(kOffs, value_block_ref.reference().offset());
    ASSERT_EQ(kOffs, value_block_ref.reference().base());

    TestValueCopy(value_block_ref);
  }

  {
    Value value_bb_ref(test_bb_);

    ASSERT_EQ(0, value_bb_ref.value());
    ASSERT_EQ(core::kSize32Bit, value_bb_ref.size());
    ASSERT_EQ(BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK,
              value_bb_ref.reference().referred_type());
    ASSERT_EQ(test_bb_, value_bb_ref.reference().basic_block());
    ASSERT_EQ(0, value_bb_ref.reference().offset());
    ASSERT_EQ(0, value_bb_ref.reference().base());

    TestValueCopy(value_bb_ref);
  }

  {
    // Explicitly specified size and reference info.
    UntypedReference ref(test_block_, 1, 2);
    Value value_expl_ref(0xBE, core::kSize8Bit, ref);

    ASSERT_EQ(0xBE, value_expl_ref.value());
    ASSERT_EQ(core::kSize8Bit, value_expl_ref.size());
    ASSERT_EQ(BasicBlockReference::REFERRED_TYPE_BLOCK,
              value_expl_ref.reference().referred_type());
    ASSERT_EQ(test_block_, value_expl_ref.reference().block());
    ASSERT_EQ(1, value_expl_ref.reference().offset());
    ASSERT_EQ(2, value_expl_ref.reference().base());

    TestValueCopy(value_expl_ref);
  }
}

typedef BasicBlockAssemblerTest OperandTest;

void TestOperandCopy(const Operand& input) {
  Operand copy(input);

  ASSERT_EQ(input.base(), copy.base());
  ASSERT_EQ(input.index(), copy.index());
  ASSERT_EQ(input.scale(), copy.scale());
  ASSERT_EQ(input.displacement().value(), copy.displacement().value());
  ASSERT_EQ(input.displacement().size(), copy.displacement().size());
  ASSERT_EQ(input.displacement().reference(), copy.displacement().reference());

  Operand copy2(core::eax);

  copy2 = input;
  ASSERT_EQ(input.base(), copy2.base());
  ASSERT_EQ(input.index(), copy2.index());
  ASSERT_EQ(input.scale(), copy2.scale());
  ASSERT_EQ(input.displacement().value(), copy2.displacement().value());
  ASSERT_EQ(input.displacement().size(), copy2.displacement().size());
  ASSERT_EQ(input.displacement().reference(), copy2.displacement().reference());
}

TEST_F(OperandTest, Construction) {
  // A register-indirect.
  TestOperandCopy(Operand(core::eax));

  // Register-indirect with displacement.
  TestOperandCopy(Operand(core::eax, Displacement(100)));
  TestOperandCopy(Operand(core::eax, Displacement(test_block_, 2)));
  TestOperandCopy(Operand(core::eax, Displacement(test_bb_)));

  // Displacement-only mode.
  TestOperandCopy(Operand(Displacement(100)));
  TestOperandCopy(Operand(Displacement(test_block_, 2)));
  TestOperandCopy(Operand(Displacement(test_bb_)));

  TestOperandCopy(Operand(core::eax,
                          core::ebp,
                          core::kTimes2,
                          Displacement(100)));
  TestOperandCopy(Operand(core::eax,
                          core::ebp,
                          core::kTimes2,
                          Displacement(test_block_, 2)));
  TestOperandCopy(Operand(core::eax,
                          core::ebp,
                          core::kTimes2,
                          Displacement(test_bb_)));

  // The [base + index * scale] mode - no displ.
  TestOperandCopy(Operand(core::eax, core::ebp, core::kTimes2));

  // The [index * scale + displ32] mode - no base.
  TestOperandCopy(Operand(core::ebp,
                          core::kTimes2,
                          Displacement(100)));
  TestOperandCopy(Operand(core::ebp,
                          core::kTimes2,
                          Displacement(test_block_, 2)));
  TestOperandCopy(Operand(core::ebp,
                          core::kTimes2,
                          Displacement(test_bb_)));
}

TEST_F(BasicBlockAssemblerTest, nop) {
  // We can't use ASSERT_NO_REFS here as nop may generate more than 1
  // instruction, an exception to the rule of 1 instruction that ASSERT_NO_REFS
  // enforces.
  asm_.nop(0);
  ASSERT_EQ(0u, instructions_.size());

  // Exactly 1 or 2 instructions should be emitted per NOP length from
  // 1 to 15.
  for (size_t i = 1; i <= 15; ++i) {
    asm_.nop(i);
    ASSERT_LT(0u, instructions_.size());
    ASSERT_GE(2u, instructions_.size());

    // NOP instructions should have no references.
    for (BasicCodeBlock::Instructions::const_iterator inst_it =
             instructions_.begin();
         inst_it != instructions_.end();
         ++inst_it) {
      ASSERT_EQ(0u, inst_it->references().size());
    }
    instructions_.clear();
  }
}

TEST_F(BasicBlockAssemblerTest, call) {
  asm_.call(Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  asm_.call(Operand(Displacement(test_bb_)));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);
}

TEST_F(BasicBlockAssemblerTest, jmp) {
  asm_.jmp(Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  asm_.jmp(Operand(Displacement(test_bb_)));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);
}

TEST_F(BasicBlockAssemblerTest, mov_b) {
  // mov BYTE PTR [base + index * scale + displ], immediate
  asm_.mov_b(Operand(core::eax, core::ebx, core::kTimes4,
                     Displacement(test_block_, 0)),
           Immediate(10));
  ASSERT_REFS(3, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);
}

TEST_F(BasicBlockAssemblerTest, movzx_b) {
  // movzx eax, BYTE PTR [base + index * scale + displ]
  asm_.movzx_b(core::eax,
               Operand(core::eax, core::ebx, core::kTimes4,
                       Displacement(test_block_, 0)));
  ASSERT_REFS(4, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);
}

TEST_F(BasicBlockAssemblerTest, mov) {
  // Simple register-register move.
  asm_.mov(core::eax, core::ebx);
  ASSERT_NO_REFS();

  // Simple immediate-register move.
  asm_.mov(core::eax, Immediate(10));
  ASSERT_NO_REFS();

  // Immediate-with reference to register.
  asm_.mov(core::eax, Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  // Torture test; mov [displ], immediate,
  // both src and dst contain references.
  asm_.mov(Operand(Displacement(test_block_, 0)), Immediate(test_bb_));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_,
              6, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);

  // Torture test; mov [base + index * scale + displ], immediate,
  // both src and dst contain references.
  asm_.mov(Operand(core::eax, core::ebx, core::kTimes4,
                   Displacement(test_block_, 0)),
           Immediate(test_bb_));
  ASSERT_REFS(3, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_,
              7, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);
}

TEST_F(BasicBlockAssemblerTest, mov_fs) {
  asm_.mov_fs(Operand(core::eax, core::ebx, core::kTimes4,
                      Displacement(test_block_, 0)),
              core::eax);
  ASSERT_REFS(4, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  asm_.mov_fs(core::eax,
              Operand(core::eax, core::ebx, core::kTimes4,
                      Displacement(test_block_, 0)));
  ASSERT_REFS(4, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);
}

TEST_F(BasicBlockAssemblerTest, lea) {
  asm_.lea(core::eax, Operand(core::eax));
  ASSERT_NO_REFS();

  asm_.lea(core::eax,
           Operand(core::eax, core::ebx, core::kTimes4,
                   Displacement(test_bb_)));
  ASSERT_REFS(3, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);
}

TEST_F(BasicBlockAssemblerTest, push) {
  asm_.push(core::esp);
  ASSERT_NO_REFS();

  asm_.push(Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  asm_.push(Operand(core::eax, core::ebx, core::kTimes4,
                    Displacement(test_bb_)));
  ASSERT_REFS(3, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);
}

TEST_F(BasicBlockAssemblerTest, pop) {
  asm_.pop(core::ebp);
  ASSERT_NO_REFS();

  asm_.pop(Operand(core::eax, core::ebx, core::kTimes4,
                    Displacement(test_bb_)));
  ASSERT_REFS(3, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);
}

TEST_F(BasicBlockAssemblerTest, pushfd) {
  asm_.pushfd();
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, popfd) {
  asm_.popfd();
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, lahf) {
  asm_.lahf();
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, sahf) {
  asm_.sahf();
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, setxx) {
  // Simple register-register operation.
  asm_.set(core::kParityEven, core::eax);
  ASSERT_NO_REFS();

  asm_.set(core::kOverflow, core::ebx);
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, test) {
  // Simple register-register operation.
  asm_.test(core::al, core::bl);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.test(core::al, Immediate(10, core::kSize8Bit));
  ASSERT_NO_REFS();

  // Simple register-register operation.
  asm_.test(core::eax, core::ebx);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.test(core::eax, Immediate(10));
  ASSERT_NO_REFS();

  // Immediate-with reference to register.
  asm_.test(core::eax, Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  // Torture test: both src and dst contain references.
  asm_.test(Operand(Displacement(test_block_, 0)), Immediate(test_bb_));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_,
              6, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);

  asm_.test(Operand(Displacement(test_block_, 0)), Immediate(10));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);
}

TEST_F(BasicBlockAssemblerTest, cmp) {
    // Simple register-register operation.
  asm_.cmp(core::al, core::bl);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.cmp(core::al, Immediate(10, core::kSize8Bit));
  ASSERT_NO_REFS();

  // Simple register-register operation.
  asm_.cmp(core::eax, core::ebx);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.cmp(core::eax, Immediate(10));
  ASSERT_NO_REFS();

  // Immediate-with reference to register.
  asm_.cmp(core::eax, Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  // Torture test: both src and dst contain references.
  asm_.cmp(Operand(Displacement(test_block_, 0)), Immediate(test_bb_));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_,
              6, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);

  asm_.cmp(Operand(Displacement(test_block_, 0)), Immediate(10));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);
}

TEST_F(BasicBlockAssemblerTest, add) {
  // Simple register-register operation.
  asm_.add(core::al, core::bl);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.add(core::al, Immediate(10, core::kSize8Bit));
  ASSERT_NO_REFS();

  // Simple register-register operation.
  asm_.add(core::eax, core::ebx);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.add(core::eax, Immediate(10));
  ASSERT_NO_REFS();

  // Immediate-with reference to register.
  asm_.add(core::eax, Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  // Torture test: both src and dst contain references.
  asm_.add(Operand(Displacement(test_block_, 0)), Immediate(test_bb_));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_,
              6, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);

  asm_.add(Operand(Displacement(test_block_, 0)), Immediate(10));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);
}

TEST_F(BasicBlockAssemblerTest, sub) {
  // Simple register-register operation.
  asm_.sub(core::al, core::bl);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.sub(core::al, Immediate(10, core::kSize8Bit));
  ASSERT_NO_REFS();

  // Simple register-register operation.
  asm_.sub(core::eax, core::ebx);
  ASSERT_NO_REFS();

  // Simple immediate-register operation.
  asm_.sub(core::eax, Immediate(10));
  ASSERT_NO_REFS();

  // Immediate-with reference to register.
  asm_.sub(core::eax, Immediate(test_block_, 0));
  ASSERT_REFS(1, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);

  // Torture test: both src and dst contain references.
  asm_.sub(Operand(Displacement(test_block_, 0)), Immediate(test_bb_));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_,
              6, BasicBlockReference::REFERRED_TYPE_BASIC_BLOCK, test_bb_);

  asm_.sub(Operand(Displacement(test_block_, 0)), Immediate(10));
  ASSERT_REFS(2, BasicBlockReference::REFERRED_TYPE_BLOCK, test_block_);
}

TEST_F(BasicBlockAssemblerTest, shl) {
  // Simple immediate-register operation.
  asm_.shl(core::eax, Immediate(1));
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, shr) {
  // Simple immediate-register operation.
  asm_.shr(core::eax, Immediate(1));
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, ret) {
  asm_.ret();
  ASSERT_NO_REFS();

  asm_.ret(4);
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, xchg) {
  asm_.xchg(core::eax, core::ecx);
  ASSERT_NO_REFS();

  asm_.xchg(core::esp, core::edx);
  ASSERT_NO_REFS();

  asm_.xchg(core::ax, core::cx);
  ASSERT_NO_REFS();

  asm_.xchg(core::sp, core::dx);
  ASSERT_NO_REFS();

  asm_.xchg(core::al, core::ch);
  ASSERT_NO_REFS();

  asm_.xchg(core::dh, core::bl);
  ASSERT_NO_REFS();
}

TEST_F(BasicBlockAssemblerTest, UndefinedSourceRange) {
  ASSERT_EQ(asm_.source_range(), SourceRange());
  asm_.call(Immediate(test_block_, 0));
  ASSERT_EQ(instructions_.back().source_range(), SourceRange());
}

TEST_F(BasicBlockAssemblerTest, SetSourceRange) {
  SourceRange range(RelativeAddress(10), 10);
  asm_.set_source_range(range);
  asm_.call(Immediate(test_block_, 0));
  ASSERT_EQ(instructions_.back().source_range(), range);
}

TEST_F(BasicBlockAssemblerTest, SetMultipleSourceRange) {
  SourceRange range1(RelativeAddress(10), 10);
  SourceRange range2(RelativeAddress(20), 20);

  asm_.set_source_range(range1);
  asm_.call(Immediate(test_block_, 0));
  ASSERT_EQ(instructions_.back().source_range(), range1);

  asm_.set_source_range(range2);
  asm_.pop(core::ebp);
  ASSERT_EQ(instructions_.back().source_range(), range2);

  asm_.ret(4);
  ASSERT_EQ(instructions_.back().source_range(), range2);
}

}  // namespace block_graph
