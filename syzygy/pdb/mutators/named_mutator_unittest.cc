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

#include "syzygy/pdb/mutators/named_mutator.h"

#include "gtest/gtest.h"

namespace pdb {
namespace mutators {

namespace {

class MockNamedPdbMutator
    : public NamedPdbMutatorImpl<MockNamedPdbMutator> {
 public:
  bool MutatePdb(PdbFile* /*pdb_file*/) {
    return true;
  }

  static const char kMutatorName[];
};

const char MockNamedPdbMutator::kMutatorName[] = "MockNamedPdbMutator";

}  // namespace

TEST(NamedPdbMutatorTest, NameWorks) {
  MockNamedPdbMutator mutator;
  EXPECT_EQ(std::string("MockNamedPdbMutator"), mutator.name());
}

}  // namespace mutators
}  // namespace pdb
