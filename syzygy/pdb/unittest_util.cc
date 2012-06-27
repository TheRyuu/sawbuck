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

#include "syzygy/pdb/unittest_util.h"

namespace testing {

const wchar_t kTestPdbFilePath[] =
    L"syzygy\\pdb\\test_data\\test_dll.pdb";

const wchar_t kTestDllFilePath[] =
    L"syzygy\\pdb\\test_data\\test_dll.dll";

const wchar_t kOmappedTestPdbFilePath[] =
    L"syzygy\\pdb\\test_data\\omapped_test_dll.pdb";

const wchar_t kValidPDBSymbolRecordStreamPath[] =
    L"syzygy\\pdb\\test_data\\valid_sym_record.pdb_stream";

const wchar_t kInvalidPDBSymbolRecordStreamPath[] =
    L"syzygy\\pdb\\test_data\\invalid_sym_record.pdb_stream";

const wchar_t kValidPDBTypeInfoStreamPath[] =
    L"syzygy\\pdb\\test_data\\valid_type_info.pdb_stream";

const wchar_t kInvalidHeaderPDBTypeInfoStreamPath[] =
    L"syzygy\\pdb\\test_data\\invalid_type_info_header_corrupted.pdb_stream";

const wchar_t kInvalidDataPDBTypeInfoStreamPath[] =
    L"syzygy\\pdb\\test_data\\invalid_type_info_data_corrupted.pdb_stream";

scoped_refptr<pdb::PdbFileStream> GetStreamFromFile(FilePath file_path) {
  int64 file_size = 0;
  file_util::GetFileSize(file_path, &file_size);
  size_t pages[] = {0};

  scoped_refptr<pdb::RefCountedFILE> file = new pdb::RefCountedFILE(
      file_util::OpenFile(file_path, "rb"));
  scoped_refptr<pdb::PdbFileStream> stream(
    new pdb::PdbFileStream(file, file_size, pages, file_size));

  return stream;
}

}  // namespace testing
