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

#include "syzygy/pe/pe_utils.h"

#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/block_graph/unittest_util.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pe/pe_file.h"
#include "syzygy/pe/unittest_util.h"
#include "syzygy/pe/transforms/pe_add_imports_transform.h"

namespace pe {

using block_graph::BlockGraph;
using core::RelativeAddress;
using pe::transforms::PEAddImportsTransform;

namespace {

class PEUtilsTest : public testing::Test {
 public:
  PEUtilsTest()
      : nt_headers_block_(NULL),
        dos_header_block_(NULL),
        main_entry_point_block_(NULL),
        tls_initializer_block_(NULL),
        nt_headers_(NULL),
        dos_header_(NULL) {
  }

  virtual void SetUp() {
    // Create the NT headers block.
    ASSERT_NO_FATAL_FAILURE(CreateNtHeadersBlock());
    // And the DOS header block.
    ASSERT_NO_FATAL_FAILURE(CreateDosHeaderBlock());
    // And set-up some entry points.
    ASSERT_NO_FATAL_FAILURE(CreateEntryPoints());
  }

 protected:
  void CreateDosHeaderBlock();
  void CreateNtHeadersBlock();
  void CreateEntryPoints();

  testing::DummyTransformPolicy policy_;
  BlockGraph block_graph_;

  BlockGraph::Block* nt_headers_block_;
  BlockGraph::Block* dos_header_block_;
  BlockGraph::Block* main_entry_point_block_;
  BlockGraph::Block* tls_initializer_block_;
  IMAGE_DOS_HEADER* dos_header_;
  IMAGE_NT_HEADERS* nt_headers_;
};

void PEUtilsTest::CreateNtHeadersBlock() {
  nt_headers_block_ = block_graph_.AddBlock(BlockGraph::DATA_BLOCK,
                                            sizeof(IMAGE_NT_HEADERS),
                                            "NT Headers");
  ASSERT_TRUE(nt_headers_block_ != NULL);

  nt_headers_ = reinterpret_cast<IMAGE_NT_HEADERS*>(
      nt_headers_block_->AllocateData(sizeof(IMAGE_NT_HEADERS)));
  ASSERT_TRUE(nt_headers_ != NULL);

  nt_headers_->Signature = IMAGE_NT_SIGNATURE;
  nt_headers_->FileHeader.SizeOfOptionalHeader =
      sizeof(nt_headers_->OptionalHeader);
  nt_headers_->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
}

void PEUtilsTest::CreateDosHeaderBlock() {
  dos_header_block_ = block_graph_.AddBlock(BlockGraph::DATA_BLOCK,
                                            sizeof(IMAGE_DOS_HEADER),
                                            "DOS Header");
  ASSERT_TRUE(dos_header_block_ != NULL);

  dos_header_ = reinterpret_cast<IMAGE_DOS_HEADER*>(
          dos_header_block_->AllocateData(dos_header_block_->size()));
  ASSERT_TRUE(dos_header_ != NULL);

  // Set the correct magic constants in the manufactured DOS header.
  dos_header_->e_magic = IMAGE_DOS_SIGNATURE;
  // Set the "DOS File Size" headers.
  dos_header_->e_cblp = dos_header_block_->size() % 512;
  dos_header_->e_cp = dos_header_block_->size() / 512;
  if (dos_header_->e_cblp != 0)
    dos_header_->e_cp++;
  // Set the header paragraph size.
  dos_header_->e_cparhdr = dos_header_block_->size() / 16;

  if (nt_headers_block_ != NULL) {
    // Set the NT headers reference.
    dos_header_block_->SetReference(
        offsetof(IMAGE_DOS_HEADER, e_lfanew),
        BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                              sizeof(RelativeAddress),
                              nt_headers_block_,
                              0, 0));
  }
}

void PEUtilsTest::CreateEntryPoints() {
  // Setup the main entry-point.
  main_entry_point_block_ = block_graph_.AddBlock(
      BlockGraph::CODE_BLOCK, 1, "main_entry_point");
  ASSERT_TRUE(main_entry_point_block_ != NULL);
  ASSERT_TRUE(nt_headers_block_->SetReference(
      offsetof(IMAGE_NT_HEADERS, OptionalHeader.AddressOfEntryPoint),
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            BlockGraph::Reference::kMaximumSize,
                            main_entry_point_block_, 0, 0)));

  // Setup the TLS directory.
  static const size_t kTlsDirectoryOffset = offsetof(
      IMAGE_NT_HEADERS,
      OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
  BlockGraph::Block* tls_directory_block = block_graph_.AddBlock(
      BlockGraph::DATA_BLOCK,
      sizeof(IMAGE_TLS_DIRECTORY),
      "tls_directory");
  ASSERT_TRUE(tls_directory_block != NULL);
  ASSERT_TRUE(tls_directory_block->AllocateData(tls_directory_block->size()));
  nt_headers_->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size =
      tls_directory_block->size();
  ASSERT_TRUE(nt_headers_block_->SetReference(
      kTlsDirectoryOffset,
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            BlockGraph::Reference::kMaximumSize,
                            tls_directory_block, 0, 0)));

  // Setup the TLS callbacks table. Reserving enough space for one callback
  // and the trailing NULL sentinel.
  BlockGraph::Block* tls_callbacks_block = block_graph_.AddBlock(
      BlockGraph::DATA_BLOCK,
      2 * BlockGraph::Reference::kMaximumSize,
      "tls_callbacks");
  ASSERT_TRUE(tls_callbacks_block != NULL);
  ASSERT_TRUE(tls_callbacks_block->AllocateData(tls_callbacks_block->size()));
  nt_headers_->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size =
      tls_directory_block->size();
  ASSERT_TRUE(tls_directory_block->SetReference(
      offsetof(IMAGE_TLS_DIRECTORY, AddressOfCallBacks),
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            BlockGraph::Reference::kMaximumSize,
                            tls_callbacks_block, 0, 0)));

  // Add a TLS initializer.
  tls_initializer_block_ = block_graph_.AddBlock(
      BlockGraph::CODE_BLOCK, 1, "tls_initializer");
  ASSERT_TRUE(tls_initializer_block_ != NULL);
  ASSERT_TRUE(tls_callbacks_block->SetReference(
      0,
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            BlockGraph::Reference::kMaximumSize,
                            tls_initializer_block_, 0, 0)));
}

}  // namespace

TEST_F(PEUtilsTest, IsValidDosHeaderBlockSuccess) {
  // This DOS header should test valid.
  EXPECT_TRUE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidDosHeaderBlockNoDataFails) {
  dos_header_block_->SetData(NULL, 0);
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidDosHeaderBlockTooShortFails) {
  dos_header_block_->ResizeData(sizeof(IMAGE_DOS_HEADER) - 1);
  dos_header_block_->set_size(sizeof(IMAGE_DOS_HEADER) - 1);
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidDosHeaderBlockInvalidMagicFails) {
  ++dos_header_->e_magic;
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidDosHeaderBlockInvalidDosFileSizeFails) {
  dos_header_->e_cp = 0;
  dos_header_->e_cblp = 0;
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));

  // This is invalid, as there are zero pages, and thus no last page.
  dos_header_->e_cblp = 10;
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidDosHeaderBlockInvalidHeaderSizeFails) {
  --dos_header_->e_cparhdr;
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidDosHeaderBlockInvalidNTHeaderRefFails) {
  // Set the NT headers reference to a non-zero offset.
  dos_header_block_->SetReference(
      offsetof(IMAGE_DOS_HEADER, e_lfanew),
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            sizeof(RelativeAddress),
                            nt_headers_block_,
                            10, 10));
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidDosHeaderBlockNoNTHeaderRefFails) {
  // Clear the NT headers reference.
  dos_header_block_->RemoveReference(offsetof(IMAGE_DOS_HEADER, e_lfanew));
  EXPECT_FALSE(IsValidDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, IsValidNtHeaderBlockSuccess) {
  // The NT headers are valid.
  EXPECT_TRUE(IsValidNtHeadersBlock(nt_headers_block_));
}

TEST_F(PEUtilsTest, IsValidNtHeaderBlockInvalidSigFails) {
  ++nt_headers_->Signature;
  // Invalid signature.
  EXPECT_FALSE(IsValidNtHeadersBlock(nt_headers_block_));
}

TEST_F(PEUtilsTest, IsValidNtHeaderBlockInvalidOptionalSigFails) {
  ++nt_headers_->OptionalHeader.Magic;
  // Invalid signature.
  EXPECT_FALSE(IsValidNtHeadersBlock(nt_headers_block_));
}

TEST_F(PEUtilsTest, IsValidNtHeaderBlockInvalidOptionalSizeFails) {
  ++nt_headers_->FileHeader.SizeOfOptionalHeader;
  // Invalid signature.
  EXPECT_FALSE(IsValidNtHeadersBlock(nt_headers_block_));
}

TEST_F(PEUtilsTest, GetNtHeadersBlockFromDosHeaderBlock) {
  ASSERT_EQ(nt_headers_block_,
            GetNtHeadersBlockFromDosHeaderBlock(dos_header_block_));
}

TEST_F(PEUtilsTest, GetNtHeadersBlockFromDosHeaderBlockConst) {
  ASSERT_EQ(nt_headers_block_,
            GetNtHeadersBlockFromDosHeaderBlock(
                const_cast<const BlockGraph::Block*>(dos_header_block_)));
}

TEST_F(PEUtilsTest, GetSectionType) {
  // Only the characteristics of the header should matter in determining the
  // type of a section.
  IMAGE_SECTION_HEADER code_header = {};
  code_header.Characteristics = IMAGE_SCN_CNT_CODE |
                                IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

  // Anything that is readable and isn't code will be identified as data.
  IMAGE_SECTION_HEADER data_header = {};
  data_header.Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA |
                                IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE |
                                IMAGE_SCN_ALIGN_4096BYTES;
  IMAGE_SECTION_HEADER rdata_header = {};
  rdata_header.Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA |
                                 IMAGE_SCN_MEM_READ |
                                 IMAGE_SCN_LNK_COMDAT;
  IMAGE_SECTION_HEADER bss_header = {};
  bss_header.Characteristics = IMAGE_SCN_CNT_UNINITIALIZED_DATA |
                               IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE |
                               IMAGE_SCN_ALIGN_2BYTES;

  // Anything that is neither readable nor code nor initialized data will be
  // flagged as unknown.
  IMAGE_SECTION_HEADER strange_header = {};
  strange_header.Characteristics = IMAGE_SCN_CNT_UNINITIALIZED_DATA |
                                   IMAGE_SCN_MEM_WRITE;

  EXPECT_EQ(kSectionCode, GetSectionType(code_header));
  EXPECT_EQ(kSectionData, GetSectionType(data_header));
  EXPECT_EQ(kSectionData, GetSectionType(rdata_header));
  EXPECT_EQ(kSectionData, GetSectionType(bss_header));
  EXPECT_EQ(kSectionUnknown, GetSectionType(strange_header));
}

TEST_F(PEUtilsTest, AddLabelToBlock) {
  BlockGraph::Block* block =
      block_graph_.AddBlock(BlockGraph::CODE_BLOCK, 0x100, "AddLabelToBlock");
  ASSERT_TRUE(block != NULL);

  ASSERT_TRUE(AddLabelToBlock(0x10, "L1", BlockGraph::DEBUG_START_LABEL,
                              block));
  ASSERT_TRUE(AddLabelToBlock(0x20, "L2", BlockGraph::CALL_SITE_LABEL,
                              block));

  BlockGraph::Label label;
  ASSERT_TRUE(block->GetLabel(0x10, &label));
  EXPECT_TRUE(label.name() == "L1");
  EXPECT_TRUE(label.attributes() == BlockGraph::DEBUG_START_LABEL);
  ASSERT_TRUE(block->GetLabel(0x20, &label));
  EXPECT_TRUE(label.name() == "L2");
  EXPECT_TRUE(label.attributes() == BlockGraph::CALL_SITE_LABEL);

  ASSERT_TRUE(AddLabelToBlock(0x10, "L3", BlockGraph::CODE_LABEL, block));
  ASSERT_TRUE(block->GetLabel(0x10, &label));

  std::vector<std::string> names;
  base::SplitStringUsingSubstr(label.name(), kLabelNameSep, &names);
  EXPECT_TRUE(std::find(names.begin(), names.end(), "L1") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "L3") != names.end());
  EXPECT_EQ(BlockGraph::DEBUG_START_LABEL | BlockGraph::CODE_LABEL,
            label.attributes());
}

TEST_F(PEUtilsTest, CopySectionInfoToBlockGraph) {
  pe::PEFile image_file;

  ASSERT_TRUE(image_file.Init(
      testing::GetExeRelativePath(testing::kTestDllName)));
  ASSERT_TRUE(CopySectionInfoToBlockGraph(image_file, &block_graph_));

  const BlockGraph::SectionMap& sections = block_graph_.sections();
  size_t num_sections = sections.size();
  ASSERT_EQ(image_file.file_header()->NumberOfSections, num_sections);

  for (size_t i = 0; i < num_sections; ++i) {
    const IMAGE_SECTION_HEADER* header = image_file.section_header(i);
    BlockGraph::SectionMap::const_iterator it = sections.find(i);
    ASSERT_TRUE(it != sections.end());

    EXPECT_EQ(i, it->second.id());
    EXPECT_EQ(header->Characteristics, it->second.characteristics());

    char name[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
    strncpy(name, reinterpret_cast<const char*>(header->Name),
            IMAGE_SIZEOF_SHORT_NAME);
    EXPECT_EQ(name, it->second.name());
  }
}

TEST_F(PEUtilsTest, GetExeEntryPoint) {
  EntryPoint entry_point;

  // Get the entry point for an EXE.
  nt_headers_->FileHeader.Characteristics &= ~IMAGE_FILE_DLL;
  EXPECT_TRUE(GetExeEntryPoint(dos_header_block_, &entry_point));
  EXPECT_EQ(main_entry_point_block_, entry_point.first);
  EXPECT_EQ(0, entry_point.second);

  // Should return no entry points if the image is a DLL.
  nt_headers_->FileHeader.Characteristics |= IMAGE_FILE_DLL;
  EXPECT_TRUE(GetExeEntryPoint(dos_header_block_, &entry_point));
  EXPECT_EQ(NULL, entry_point.first);

  // Should fail if the image is an EXE with no entry-point.
  nt_headers_->FileHeader.Characteristics &= ~IMAGE_FILE_DLL;
  ASSERT_TRUE(nt_headers_block_->RemoveReference(
      offsetof(IMAGE_NT_HEADERS, OptionalHeader.AddressOfEntryPoint)));
  EXPECT_FALSE(GetExeEntryPoint(dos_header_block_, &entry_point));
}

TEST_F(PEUtilsTest, GetDllEntryPoint) {
  EntryPoint entry_point;

  // Get the DLL entry point.
  nt_headers_->FileHeader.Characteristics |= IMAGE_FILE_DLL;
  EXPECT_TRUE(GetDllEntryPoint(dos_header_block_, &entry_point));
  EXPECT_EQ(main_entry_point_block_, entry_point.first);
  EXPECT_EQ(0, entry_point.second);

  // Should return no entry points if the image is an EXE.
  nt_headers_->FileHeader.Characteristics &= ~IMAGE_FILE_DLL;
  EXPECT_TRUE(GetDllEntryPoint(dos_header_block_, &entry_point));
  EXPECT_EQ(NULL, entry_point.first);

  // Should return no entry points if the image is a DLL without an entry-point.
  nt_headers_->FileHeader.Characteristics |= IMAGE_FILE_DLL;
  ASSERT_TRUE(nt_headers_block_->RemoveReference(
      offsetof(IMAGE_NT_HEADERS, OptionalHeader.AddressOfEntryPoint)));
  EXPECT_TRUE(GetDllEntryPoint(dos_header_block_, &entry_point));
  EXPECT_EQ(NULL, entry_point.first);
}

TEST_F(PEUtilsTest, GetTlsInitializers) {
  // A container to store the entry-points.
  EntryPointSet entry_points;

  // Get the entry points.
  EXPECT_TRUE(GetTlsInitializers(dos_header_block_, &entry_points));
  EXPECT_EQ(1U, entry_points.size());
  EXPECT_EQ(tls_initializer_block_, entry_points.begin()->first);
}

TEST_F(PEUtilsTest, HasImportEntry) {
  // Creates an imported module.
  transforms::ImportedModule module("foo.dll");
  const char* kFooFunc = "foo_func";
  size_t function_foo = module.AddSymbol(
      kFooFunc, transforms::ImportedModule::kAlwaysImport);
  ASSERT_EQ(kFooFunc, module.GetSymbolName(function_foo));

  // Apply the transform to add this module import to the block-graph.
  PEAddImportsTransform transform;
  transform.AddModule(&module);
  ASSERT_TRUE(block_graph::ApplyBlockGraphTransform(
      &transform, &policy_, &block_graph_, dos_header_block_));

  // Ensure that we can find this module, and that we can't find a
  // non-imported module.
  bool has_import = false;
  EXPECT_TRUE(HasImportEntry(dos_header_block_, "foo.dll", &has_import));
  EXPECT_TRUE(has_import);
  EXPECT_TRUE(HasImportEntry(dos_header_block_, "bar.dll", &has_import));
  EXPECT_FALSE(has_import);
}

TEST_F(PEUtilsTest, FindCoffSpecialBlocks) {
  BlockGraph::Block* actual_headers_block = NULL;
  BlockGraph::Block* actual_symbols_block = NULL;
  BlockGraph::Block* actual_strings_block = NULL;

  BlockGraph::Block* headers_block =
      block_graph_.AddBlock(
          BlockGraph::DATA_BLOCK,
          sizeof(IMAGE_FILE_HEADER) + 12 * sizeof(IMAGE_SECTION_HEADER),
          "COFF Headers");
  ASSERT_TRUE(headers_block != NULL);
  headers_block->set_attribute(BlockGraph::COFF_HEADERS);

  // FindCoffSpecialBlocks() should fail even if we don't request the other
  // special blocks.
  EXPECT_FALSE(FindCoffSpecialBlocks(&block_graph_,
                                     &actual_headers_block,
                                     &actual_symbols_block,
                                     &actual_strings_block));
  EXPECT_FALSE(FindCoffSpecialBlocks(&block_graph_,
                                     &actual_headers_block, NULL, NULL));

  BlockGraph::Block* symbols_block =
      block_graph_.AddBlock(BlockGraph::DATA_BLOCK,
                            30 * sizeof(IMAGE_SYMBOL),
                            "COFF Symbol Table");
  ASSERT_TRUE(symbols_block != NULL);
  symbols_block->set_attribute(BlockGraph::COFF_SYMBOL_TABLE);

  EXPECT_FALSE(FindCoffSpecialBlocks(&block_graph_,
                                     &actual_headers_block,
                                     &actual_symbols_block,
                                     &actual_strings_block));
  EXPECT_FALSE(FindCoffSpecialBlocks(&block_graph_,
                                     &actual_headers_block,
                                     &actual_symbols_block,
                                     NULL));

  BlockGraph::Block* strings_block =
      block_graph_.AddBlock(BlockGraph::DATA_BLOCK, 242, "COFF String Table");
  ASSERT_TRUE(strings_block != NULL);
  strings_block->set_attribute(BlockGraph::COFF_STRING_TABLE);

  EXPECT_TRUE(FindCoffSpecialBlocks(&block_graph_,
                                    &actual_headers_block,
                                    &actual_symbols_block,
                                    &actual_strings_block));
  EXPECT_EQ(headers_block, actual_headers_block);
  EXPECT_EQ(symbols_block, actual_symbols_block);
  EXPECT_EQ(strings_block, actual_strings_block);
}

TEST_F(PEUtilsTest, GuessFileType) {
  base::FilePath fake(L"C:\\this\\path\\should\\not\\exist-at.all");
  base::FilePath dir = testing::GetExeRelativePath(L"test_data");
  base::FilePath pe_dll = testing::GetOutputRelativePath(
      testing::kTestDllName);
  base::FilePath coff_obj = testing::GetExeTestDataRelativePath(
      testing::kTestDllCoffObjName);
  base::FilePath ltcg_obj = testing::GetExeTestDataRelativePath(
      testing::kTestDllLtcgObjName);
  base::FilePath pe_exe;
  ASSERT_TRUE(PathService::Get(base::FILE_EXE, &pe_exe));
  base::FilePath pdb = testing::GetOutputRelativePath(
      testing::kTestDllPdbName);

  // Doesn't exist.
  FileType file_type = kUnknownFileType;
  EXPECT_FALSE(GuessFileType(fake, &file_type));
  EXPECT_EQ(kUnknownFileType, file_type);

  // Can't be opened for reading.
  file_type = kUnknownFileType;
  EXPECT_FALSE(GuessFileType(dir, &file_type));
  EXPECT_EQ(kUnknownFileType, file_type);

  file_type = kUnknownFileType;
  EXPECT_TRUE(GuessFileType(pe_dll, &file_type));
  EXPECT_EQ(kPeFileType, file_type);

  file_type = kUnknownFileType;
  EXPECT_TRUE(GuessFileType(coff_obj, &file_type));
  EXPECT_EQ(kCoffFileType, file_type);

  file_type = kUnknownFileType;
  EXPECT_TRUE(GuessFileType(ltcg_obj, &file_type));
  EXPECT_EQ(kUnknownFileType, file_type);

  file_type = kUnknownFileType;
  EXPECT_TRUE(GuessFileType(pe_exe, &file_type));
  EXPECT_EQ(kPeFileType, file_type);

  file_type = kUnknownFileType;
  EXPECT_TRUE(GuessFileType(pdb, &file_type));
  EXPECT_EQ(kPdbFileType, file_type);
}

}  // namespace pe
