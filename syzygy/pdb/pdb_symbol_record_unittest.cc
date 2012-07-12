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

#include "syzygy/pdb/pdb_symbol_record.h"

#include "base/file_util.h"
#include "gtest/gtest.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pdb/pdb_byte_stream.h"
#include "syzygy/pdb/unittest_util.h"
#include "syzygy/pe/unittest_util.h"

namespace pdb {

TEST(PdbReadSymbolRecordTest, ReadValidSymRecordStream) {
  FilePath valid_sym_record_path = testing::GetSrcRelativePath(
      testing::kValidPDBSymbolRecordStreamPath);

  scoped_refptr<pdb::PdbFileStream> valid_sym_record_stream =
      testing::GetStreamFromFile(valid_sym_record_path);
  SymbolRecordVector symbol_vector;
  EXPECT_TRUE(ReadSymbolRecord(valid_sym_record_stream.get(),
                               valid_sym_record_stream->length(),
                               &symbol_vector));
}

TEST(PdbReadSymbolRecordTest, ReadInvalidSymRecordStream) {
  FilePath invalid_sym_record_path = testing::GetSrcRelativePath(
      testing::kInvalidPDBSymbolRecordStreamPath);

  scoped_refptr<pdb::PdbFileStream> invalid_sym_record_stream =
      testing::GetStreamFromFile(invalid_sym_record_path);
  SymbolRecordVector symbol_vector;
  EXPECT_FALSE(ReadSymbolRecord(invalid_sym_record_stream.get(),
                                invalid_sym_record_stream->length(),
                                &symbol_vector));
}

}  // namespace pdb
