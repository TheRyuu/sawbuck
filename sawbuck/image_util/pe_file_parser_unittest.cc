// Copyright 2010 Google Inc.
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
#include "sawbuck/image_util/pe_file_parser.h"
#include "base/file_path.h"
#include "base/native_library.h"
#include "base/path_service.h"
#include "base/scoped_ptr.h"
#include "base/string_util.h"
#include "base/win/pe_image.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using core::BlockGraph;
using core::RelativeAddress;
using image_util::PEFile;
using image_util::PEFileParser;

FilePath GetExeRelativePath(const wchar_t* image_name) {
  FilePath exe_dir;
  PathService::Get(base::DIR_EXE, &exe_dir);

  return exe_dir.Append(image_name);
}

const wchar_t kDllName[] = L"test_dll.dll";

class PEFileParserTest: public testing::Test {
 public:
  PEFileParserTest() : address_space_(&image_), loaded_image_(NULL) {
  }

  virtual void SetUp() {
    add_reference_.reset(NewCallback(this, &PEFileParserTest::AddReference));
    ASSERT_TRUE(add_reference_ != NULL);

    ASSERT_TRUE(image_file_.Init(GetExeRelativePath(kDllName)));
  }

  virtual void TearDown() {
    if (loaded_image_ != NULL)
      base::UnloadNativeLibrary(loaded_image_);
    loaded_image_ = NULL;
  }

  void AddReference(RelativeAddress src,
                    BlockGraph::ReferenceType type,
                    BlockGraph::Size size,
                    RelativeAddress dst,
                    const char* name) {
    Reference ref = { type, size, dst, name };
    bool inserted = references_.insert(std::make_pair(src, ref)).second;
    EXPECT_TRUE(inserted);
  }

  // Assert that an exported function in the test_dll is referenced
  // in the image.
  bool ExportIsReferenced(const char* function_name_or_ordinal) {
    if (loaded_image_ == NULL)
      loaded_image_ = base::LoadNativeLibrary(GetExeRelativePath(kDllName));

    EXPECT_TRUE(loaded_image_ != NULL);
    if (loaded_image_ == NULL)
      return false;

    void* function = base::GetFunctionPointerFromNativeLibrary(
        loaded_image_, function_name_or_ordinal);

    RelativeAddress addr(reinterpret_cast<const char*>(function) -
                         reinterpret_cast<const char*>(loaded_image_));

    ReferenceMap::const_iterator it(references_.begin());
    for (; it != references_.end(); ++it) {
      if (it->second.dst == addr)
        return true;
    }

    return false;
  }

  void AssertDataDirectoryEntryValid(BlockGraph::Block* block) {
    ASSERT_TRUE(block != NULL);
    ASSERT_NE(0u, block->size());
    ASSERT_EQ(block->size(), block->data_size());
    ASSERT_TRUE(block->data() != NULL);
  }

 protected:
  struct Reference {
    BlockGraph::ReferenceType type;
    BlockGraph::Size size;
    RelativeAddress dst;
    std::string name;
  };

  typedef std::map<RelativeAddress, Reference> ReferenceMap;
  ReferenceMap references_;

  scoped_ptr<PEFileParser::AddReferenceCallback> add_reference_;
  PEFile image_file_;
  BlockGraph image_;
  BlockGraph::AddressSpace address_space_;

  base::NativeLibrary loaded_image_;
};

}  // namespace

namespace image_util {

TEST_F(PEFileParserTest, ParseImageHeader) {
  PEFileParser parser(image_file_, &address_space_, add_reference_.get());

  PEFileParser::PEHeader header;
  EXPECT_TRUE(parser.ParseImageHeader(&header));

  // Check that the DOS header was read succssfully.
  ASSERT_TRUE(header.dos_header != NULL);
  ASSERT_EQ(sizeof(IMAGE_DOS_HEADER), header.dos_header->size());
  ASSERT_EQ(BlockGraph::DATA_BLOCK, header.dos_header->type());
  // Check the underlying data.
  ASSERT_EQ(sizeof(IMAGE_DOS_HEADER), header.dos_header->data_size());
  const IMAGE_DOS_HEADER* dos_header =
      reinterpret_cast<const IMAGE_DOS_HEADER*>(header.dos_header->data());
  ASSERT_TRUE(dos_header != NULL);
  ASSERT_EQ(IMAGE_DOS_SIGNATURE, dos_header->e_magic);

  // Check the DOS stub.
  ASSERT_TRUE(header.dos_stub != NULL);
  ASSERT_NE(0U, header.dos_stub->size());
  ASSERT_EQ(BlockGraph::CODE_BLOCK, header.dos_stub->type());
  ASSERT_EQ(header.dos_stub->size(), header.dos_stub->data_size());
  ASSERT_TRUE(header.dos_stub->data() != NULL);

  // Check the NT headers.
  ASSERT_TRUE(header.nt_headers != NULL);
  ASSERT_EQ(sizeof(IMAGE_NT_HEADERS), header.nt_headers->size());
  ASSERT_EQ(BlockGraph::DATA_BLOCK, header.nt_headers->type());
  ASSERT_EQ(sizeof(IMAGE_NT_HEADERS), header.nt_headers->data_size());
  const IMAGE_NT_HEADERS* nt_headers =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(header.nt_headers->data());
  ASSERT_TRUE(nt_headers != NULL);
  ASSERT_EQ(IMAGE_NT_OPTIONAL_HDR32_MAGIC, nt_headers->OptionalHeader.Magic);

  // Check the image section headers.
  ASSERT_TRUE(header.image_section_headers != NULL);
  ASSERT_EQ(
      sizeof(IMAGE_SECTION_HEADER) * nt_headers->FileHeader.NumberOfSections,
      header.image_section_headers->size());
  ASSERT_EQ(BlockGraph::DATA_BLOCK, header.image_section_headers->type());
  ASSERT_EQ(header.image_section_headers->size(),
      header.image_section_headers->data_size());

  // Now check the various data directory sections we expect to be non NULL.
  // We know the test dll has exports.
  EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
    header.data_directory[IMAGE_DIRECTORY_ENTRY_EXPORT]));
  // And imports.
  EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
    header.data_directory[IMAGE_DIRECTORY_ENTRY_IMPORT]));
  // And resources.
  EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
    header.data_directory[IMAGE_DIRECTORY_ENTRY_RESOURCE]));
  // And relocs.
  EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
    header.data_directory[IMAGE_DIRECTORY_ENTRY_BASERELOC]));
  // And a debug directory.
  EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
    header.data_directory[IMAGE_DIRECTORY_ENTRY_DEBUG]));
  // And a tls directory?
  // TODO(siggi): add some TLS data to the test DLL.
  // EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
  //   header.data_directory[IMAGE_DIRECTORY_ENTRY_TLS]));
  // And a load configuration directory.
  EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
    header.data_directory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG]));
  // And a delay import directory.
  // TODO(siggi): add some delay import configuration to the test DLL.
  // EXPECT_NO_FATAL_FAILURE(AssertDataDirectoryEntryValid(
  //  header.data_directory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT]));
}

TEST_F(PEFileParserTest, ParseExportDirectory) {
  PEFileParser parser(image_file_, &address_space_, add_reference_.get());

  PEFileParser::PEHeader header;
  EXPECT_TRUE(parser.ParseImageHeader(&header));
  EXPECT_TRUE(parser.ParseExportDirectory(
      header.data_directory[IMAGE_DIRECTORY_ENTRY_EXPORT]));

  loaded_image_ = base::LoadNativeLibrary(GetExeRelativePath(kDllName));
  ASSERT_TRUE(loaded_image_ != NULL);

  ASSERT_TRUE(ExportIsReferenced("function1"));
  // function2 is exported by ordinal only.
  ASSERT_TRUE(ExportIsReferenced(reinterpret_cast<const char*>(7)));
  ASSERT_TRUE(ExportIsReferenced("function3"));
}

}  // namespace image_util
