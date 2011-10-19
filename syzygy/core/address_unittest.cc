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

#include "syzygy/core/address.h"

#include "gtest/gtest.h"
#include "syzygy/core/unittest_util.h"

namespace core {

TEST(AddressTest, DefaultInitialization) {
  EXPECT_EQ(0, RelativeAddress().value());
  EXPECT_EQ(0, AbsoluteAddress().value());
  EXPECT_EQ(0, FileOffsetAddress().value());
}

TEST(AddressTest, CreateInitialialized) {
  const size_t kAddress = 0xCAFEBABE;
  EXPECT_EQ(kAddress, RelativeAddress(kAddress).value());
  EXPECT_EQ(kAddress, AbsoluteAddress(kAddress).value());
  EXPECT_EQ(kAddress, FileOffsetAddress(kAddress).value());
}

TEST(AddressTest, Operators) {
  const RelativeAddress kOne(1);
  const RelativeAddress kTwo(2);
  const RelativeAddress kThree(3);

  EXPECT_TRUE(kOne < kTwo);
  EXPECT_FALSE(kOne < kOne);
  EXPECT_FALSE(kTwo < kOne);

  EXPECT_TRUE(kOne <= kOne);
  EXPECT_TRUE(kOne <= kTwo);
  EXPECT_FALSE(kTwo <= kOne);

  EXPECT_FALSE(kOne > kTwo);
  EXPECT_TRUE(kTwo > kOne);

  RelativeAddress addr(kOne);

  EXPECT_TRUE(kOne == addr);
  EXPECT_FALSE(addr == kTwo);

  EXPECT_TRUE(kOne + 1 == kTwo);
  EXPECT_TRUE(kOne == kTwo - 1);
  EXPECT_EQ(1, kTwo - kOne);

  EXPECT_EQ(1, addr.value());
  addr.set_value(2);
  EXPECT_EQ(2, addr.value());

  addr += 1;
  EXPECT_TRUE(addr == kThree);
  addr -= 1;
  EXPECT_TRUE(addr == kTwo);
}

TEST(AddressTest, AlignUp) {
  const RelativeAddress one(1);
  const RelativeAddress two(2);
  const RelativeAddress four(4);
  const RelativeAddress eight(8);
  const RelativeAddress sixteen(16);

  EXPECT_EQ(one.AlignUp(1), one);
  EXPECT_EQ(one.AlignUp(2), two);
  EXPECT_EQ(one.AlignUp(4), four);
  EXPECT_EQ(one.AlignUp(8), eight);
  EXPECT_EQ(one.AlignUp(16), sixteen);

  EXPECT_TRUE(one.AlignUp(1).IsAligned(1));
  EXPECT_TRUE(one.AlignUp(2).IsAligned(2));
  EXPECT_TRUE(one.AlignUp(4).IsAligned(4));
  EXPECT_TRUE(one.AlignUp(8).IsAligned(8));
  EXPECT_TRUE(one.AlignUp(16).IsAligned(16));
}

TEST(AddressTest, Serialization) {
  const RelativeAddress address(42);

  EXPECT_TRUE(testing::TestSerialization(address));
}

}  // namespace core
