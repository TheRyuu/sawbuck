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

#include "syzygy/pdb/pdb_file.h"

#include "base/memory/scoped_ptr.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace pdb {

namespace {

class DummyPdbStream : public PdbStream {
 public:
  DummyPdbStream() : PdbStream(10) {
    ++instance_count_;
  }

  ~DummyPdbStream() {
    --instance_count_;
  }

  virtual bool ReadBytes(void* dest, size_t count, size_t* bytes_read) {
    return false;
  }

  static size_t instance_count_;
};

size_t DummyPdbStream::instance_count_;

}  // namespace

TEST(PdbFileTest, WorksAsExpected) {
  scoped_ptr<PdbFile> pdb(new PdbFile());
  EXPECT_EQ(0u, pdb->StreamCount());
  EXPECT_EQ(0u, DummyPdbStream::instance_count_);

  PdbStream* stream = new DummyPdbStream();
  EXPECT_EQ(1u, DummyPdbStream::instance_count_);
  size_t index0 = pdb->AppendStream(stream);
  EXPECT_EQ(0u, index0);
  EXPECT_EQ(1u, pdb->StreamCount());
  EXPECT_EQ(stream, pdb->GetStream(index0));

  stream = new DummyPdbStream();
  EXPECT_EQ(2u, DummyPdbStream::instance_count_);
  size_t index1 = pdb->AppendStream(stream);
  EXPECT_EQ(1u, index1);
  EXPECT_EQ(2u, pdb->StreamCount());
  EXPECT_EQ(stream, pdb->GetStream(index1));
  PdbStream* stream1 = stream;

  stream = new DummyPdbStream();
  EXPECT_EQ(3u, DummyPdbStream::instance_count_);
  pdb->ReplaceStream(index0, stream);
  EXPECT_EQ(2u, DummyPdbStream::instance_count_);
  EXPECT_EQ(2u, pdb->StreamCount());
  EXPECT_EQ(stream, pdb->GetStream(index0));

  std::vector<PdbStream*> expected_streams;
  expected_streams.push_back(stream);
  expected_streams.push_back(stream1);
  EXPECT_THAT(pdb->streams(), ::testing::ContainerEq(expected_streams));

  pdb.reset(NULL);
  EXPECT_EQ(0u, DummyPdbStream::instance_count_);
}

}  // namespace pdb
