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

#include "syzygy/relink/relinker.h"

#include <ctime>

#include "base/file_util.h"
#include "base/lazy_instance.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "syzygy/common/defs.h"
#include "syzygy/common/syzygy_version.h"
#include "syzygy/core/serialization.h"
#include "syzygy/pdb/pdb_util.h"
#include "syzygy/pe/decomposer.h"
#include "syzygy/pe/metadata.h"
#include "syzygy/pe/pe_data.h"
#include "syzygy/pe/pe_file.h"
#include "syzygy/pe/pe_file_writer.h"
#include "syzygy/pe/pe_utils.h"

using core::BlockGraph;
using core::RelativeAddress;
using pe::Decomposer;
using pe::ImageLayout;
using pe::PEFileWriter;

namespace {

void AddOmapForBlockRange(
    const BlockGraph::AddressSpace::RangeMapConstIterPair& original,
    const BlockGraph::AddressSpace& remapped,
    std::vector<OMAP>* omap) {
  BlockGraph::AddressSpace::RangeMapConstIter it;

  for (it = original.first; it != original.second; ++it) {
    const BlockGraph::Block* block = it->second;
    DCHECK(block != NULL);

    RelativeAddress to_addr;
    if (remapped.GetAddressOf(block, &to_addr)) {
      OMAP entry = { it->first.start().value(), to_addr.value() };
      omap->push_back(entry);
    }
  }
}

void AddOmapForAllSections(
    const std::vector<ImageLayout::SectionInfo>& sections,
    const BlockGraph::AddressSpace& from, const BlockGraph::AddressSpace& to,
    std::vector<OMAP>* omap) {
  for (size_t i = 0; i < sections.size() - 1; ++i) {
    BlockGraph::AddressSpace::RangeMapConstIterPair range =
        from.GetIntersectingBlocks(sections[i].addr, sections[i].size);

    AddOmapForBlockRange(range, to, omap);
  }
}

struct PaddingData {
  enum {
      length = 8192,  // The maximum amount of padding (2 * page_size).
      value  = 0xCC   // The Int3 instruction
  };

  PaddingData() {
    memset(buffer, value, sizeof(buffer));
  }

  uint8 buffer[length];
};

base::LazyInstance<PaddingData> kPaddingData(base::LINKER_INITIALIZED);

}  // namespace

namespace relink {

RelinkerBase::RelinkerBase() : image_layout_(NULL), block_graph_(NULL) {
}

RelinkerBase::~RelinkerBase() {
}

bool RelinkerBase::Initialize(const ImageLayout& image_layout,
                              BlockGraph* block_graph) {
  DCHECK(block_graph != NULL);

  // Retrieve the DOS header block from the image layout.
  BlockGraph::Block* dos_header_block =
      image_layout.blocks.GetBlockByAddress(RelativeAddress(0));

  builder_.reset(new PEFileBuilder(block_graph));
  if (!builder_->SetImageHeaders(dos_header_block)) {
    LOG(ERROR) << "Invalid image headers.";
    return false;
  }

  image_layout_ = &image_layout;
  block_graph_ = block_graph;

  return true;
}

bool RelinkerBase::FinalizeImageHeaders() {
  if (!builder().CreateRelocsSection())  {
    LOG(ERROR) << "Unable to create new relocations section";
    return false;
  }

  if (!builder().FinalizeHeaders()) {
    LOG(ERROR) << "Unable to finalize header information";
    return false;
  }

  return true;
}

bool RelinkerBase::WriteImage(const FilePath& output_path) {
  PEFileWriter writer(builder_->image_layout());

  if (!writer.WriteImage(output_path)) {
    LOG(ERROR) << "Unable to write new executable";
    return false;
  }

  return true;
}

bool RelinkerBase::CopySection(const ImageLayout::SectionInfo& section) {
  BlockGraph::AddressSpace::Range section_range(section.addr, section.size);

  // Duplicate the section in the new image.
  RelativeAddress start = builder().AddSection(section.name.c_str(),
                                               section.size,
                                               section.data_size,
                                               section.characteristics);
  BlockGraph::AddressSpace::RangeMapConstIterPair section_blocks =
      original_addr_space().GetIntersectingBlocks(section_range.start(),
                                                  section_range.size());

  // Copy the blocks.
  size_t bytes_copied = 0;
  if (!CopyBlocks(section_blocks, start, &bytes_copied)) {
    LOG(ERROR) << "Unable to copy blocks to new image";
    return false;
  }

  DCHECK(bytes_copied == section.size);
  return true;
}

bool RelinkerBase::CopyBlocks(
    const AddressSpace::RangeMapConstIterPair& iter_pair,
    RelativeAddress insert_at,
    size_t* bytes_copied) {
  DCHECK(bytes_copied != NULL);
  RelativeAddress start = insert_at;
  AddressSpace::RangeMapConstIter it = iter_pair.first;
  const AddressSpace::RangeMapConstIter& end = iter_pair.second;
  for (; it != end; ++it) {
    BlockGraph::Block* block = it->second;
    if (!builder().image_layout().blocks.InsertBlock(insert_at, block)) {
      LOG(ERROR) << "Failed to insert block '" << block->name() <<
          "' at " << insert_at;
      return false;
    }

    insert_at += block->size();
  }

  (*bytes_copied) = insert_at - start;
  return true;
}

Relinker::Relinker()
    : padding_length_(0),
      resource_section_id_(pe::kInvalidSection) {
}

size_t Relinker::max_padding_length() {
  return PaddingData::length;
}

void Relinker::set_padding_length(size_t length) {
  DCHECK_LE(length, max_padding_length());
  padding_length_ = std::min<size_t>(length, max_padding_length());
}

const uint8* Relinker::padding_data() {
  return kPaddingData.Get().buffer;
}

bool Relinker::Relink(const FilePath& input_dll_path,
                      const FilePath& input_pdb_path,
                      const FilePath& output_dll_path,
                      const FilePath& output_pdb_path,
                      bool output_metadata) {
  DCHECK(!input_dll_path.empty());
  DCHECK(!input_pdb_path.empty());
  DCHECK(!output_dll_path.empty());
  DCHECK(!output_pdb_path.empty());

  // Read and decompose the input image for starters.
  LOG(INFO) << "Reading input image.";
  pe::PEFile input_dll;
  if (!input_dll.Init(input_dll_path)) {
    LOG(ERROR) << "Unable to read " << input_dll_path.value() << ".";
    return false;
  }

  LOG(INFO) << "Decomposing input image.";
  Decomposer decomposer(input_dll);
  BlockGraph block_graph;
  ImageLayout image_layout(&block_graph);
  if (!decomposer.Decompose(&image_layout)) {
    LOG(ERROR) << "Unable to decompose " << input_dll_path.value() << ".";
    return false;
  }

  LOG(INFO) << "Initializing relinker.";
  if (!Initialize(image_layout, &block_graph)) {
    LOG(ERROR) << "Unable to initialize the relinker.";
    return false;
  }

  LOG(INFO) << "Setting up the new ordering.";
  Reorderer::Order order;
  if (!SetupOrdering(input_dll, image_layout, &order)) {
    LOG(ERROR) << "Unable to setup the ordering.";
    return false;
  }

  // Reorder, section by section.
  for (size_t i = 0; i < original_sections().size() - 1; ++i) {
    const ImageLayout::SectionInfo& section = original_sections()[i];

    // Skip the resource section if we encounter it.
    if (section.name == common::kResourceSectionName) {
      // We should only ever come across one of these, and it should be
      // second to last.
      DCHECK_EQ(i, original_sections().size() - 2);
      DCHECK_EQ(pe::kInvalidSection, resource_section_id_);
      resource_section_id_ = i;
      continue;
    }

    LOG(INFO) << "Reordering section " << i << " (" << section.name << ").";
    if (!ReorderSection(i, section, order)) {
      LOG(ERROR) << "Unable to reorder the '" << section.name << "' section.";
      return false;
    }
  }

  // Update the debug info and copy the data directory.
  LOG(INFO) << "Updating debug information.";

  // Retrieve the debug sdirectory entry block.
  BlockGraph::Reference debug_entry;
  size_t debug_entry_size = 0;
  if (!builder().GetDataDirectoryEntry(IMAGE_DIRECTORY_ENTRY_DEBUG,
                                       &debug_entry,
                                       &debug_entry_size) ||
      debug_entry.offset() != 0) {
    LOG(ERROR) << "Missing or invalid debug directory entry.";
    return false;
  }

  if (!UpdateDebugInformation(debug_entry.referenced(), output_pdb_path)) {
    LOG(ERROR) << "Unable to update debug information.";
    return false;
  }

  // Create the metadata section if we're been requested to.
  if (output_metadata && !WriteMetadataSection(input_dll))
    return false;

  // We always want the resource section to be next to last (before .relocs).
  // We currently do not support ordering of the resource section, even if
  // ordering information was provided!
  if (!CopyResourceSection())
    return false;

  // Finalize the headers and write the image and pdb.
  LOG(INFO) << "Finalizing the image headers.";
  if (!FinalizeImageHeaders()) {
    LOG(ERROR) << "Unable to finalize image headers.";
    return false;
  }

  // Write the new PE Image file.
  LOG(INFO) << "Writing the new image file.";
  if (!WriteImage(output_dll_path)) {
    LOG(ERROR) << "Unable to write " << output_dll_path.value();
    return false;
  }

  // Write the new PDB file.
  LOG(INFO) << "Writing the new PDB file.";
  if (!WritePDBFile(input_pdb_path, output_pdb_path)) {
    LOG(ERROR) << "Unable to write " << output_pdb_path.value();
    return false;
  }

  return true;
}

bool Relinker::Initialize(const ImageLayout& image_layout,
                          BlockGraph* block_graph) {
  if (!RelinkerBase::Initialize(image_layout, block_graph))
    return false;

  if (FAILED(::CoCreateGuid(&new_image_guid_))) {
    LOG(ERROR) << "Failed to create image GUID!";
    return false;
  }

  return true;
}

bool Relinker::InsertPaddingBlock(BlockGraph::BlockType block_type,
                                  size_t size,
                                  RelativeAddress* insert_at) {
  DCHECK(insert_at != NULL);
  DCHECK(size <= max_padding_length());

  if (size == 0)
    return true;

  BlockGraph::Block* new_block = builder().image_layout().blocks.AddBlock(
      block_type, *insert_at, size, "Padding block");

  if (new_block == NULL) {
    LOG(ERROR) << "Failed to allocate padding block at " << insert_at << ".";
    return false;
  }

  new_block->SetData(padding_data(), size);
  *insert_at += size;

  return true;
}

bool Relinker::UpdateDebugInformation(BlockGraph::Block* debug_directory_block,
                                      const FilePath& output_pdb_path) {
  IMAGE_DEBUG_DIRECTORY debug_dir;
  if (debug_directory_block->data_size() != sizeof(debug_dir)) {
    LOG(ERROR) << "Debug directory is unexpected size.";
    return false;
  }
  memcpy(&debug_dir, debug_directory_block->data(), sizeof(debug_dir));
  if (debug_dir.Type != IMAGE_DEBUG_TYPE_CODEVIEW) {
    LOG(ERROR) << "Debug directory with unexpected type.";
    return false;
  }

  // Calculate the new debug info size (note that the trailing NUL character is
  // already accounted for in the structure).
  std::string new_pdb_path;
  if (!WideToUTF8(output_pdb_path.value().c_str(),
                  output_pdb_path.value().length(),
                  &new_pdb_path)) {
    LOG(ERROR) << "Failed to convert the PDB path to UTF8.";
    return false;
  }
  size_t new_debug_info_size = sizeof(pe::CvInfoPdb70) + new_pdb_path.length();

  // Update the timestamp.
  debug_dir.TimeDateStamp = static_cast<uint32>(time(NULL));
  debug_dir.SizeOfData = new_debug_info_size;

  // Update the debug directory block.
  if (debug_directory_block->CopyData(sizeof(debug_dir), &debug_dir) == NULL) {
    LOG(ERROR) << "Unable to copy debug directory data";
    return false;
  }

  // Get the current debug info.
  BlockGraph::Reference ref;
  if (!debug_directory_block->GetReference(
          FIELD_OFFSET(IMAGE_DEBUG_DIRECTORY, AddressOfRawData), &ref) ||
      ref.offset() != 0 ||
      ref.referenced()->size() < sizeof(pe::CvInfoPdb70)) {
    LOG(ERROR) << "Unexpected or no data in debug directory.";
    return false;
  }

  BlockGraph::Block* debug_info_block = ref.referenced();
  DCHECK(debug_info_block != NULL);

  const pe::CvInfoPdb70* debug_info =
      reinterpret_cast<const pe::CvInfoPdb70*>(debug_info_block->data());
  DCHECK(debug_info != NULL);

  // Allocate a new debug info block.
  // TODO(rogerm): Remove the old (and now orphaned) debug info block once
  //    we have generic layout implemented.
  RelativeAddress new_debug_block_addr = builder().AddSection(
      ".pdbinfo", new_debug_info_size, new_debug_info_size,
      IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);

  BlockGraph::Block* new_debug_info_block =
      builder().image_layout().blocks.AddBlock(BlockGraph::DATA_BLOCK,
                                               new_debug_block_addr,
                                               new_debug_info_size,
                                               "New Debug Info");
  DCHECK(new_debug_info_block != NULL);

  pe::CvInfoPdb70* new_debug_info =
      reinterpret_cast<pe::CvInfoPdb70*>(
          new_debug_info_block->AllocateData(new_debug_info_size));
  if (debug_info == NULL) {
    LOG(ERROR) << "Unable to allocate new debug info.";
    return false;
  }

  // Populate the new debug info block.
  new_debug_info->cv_signature = debug_info->cv_signature;
  new_debug_info->pdb_age = debug_info->pdb_age;
  new_debug_info->signature = new_image_guid_;
  base::strlcpy(&new_debug_info->pdb_file_name[0],
                new_pdb_path.c_str(),
                new_pdb_path.length() + 1);

  // Transfer pointers from the old debug info block to the new.
  if (!debug_info_block->TransferReferrers(0, new_debug_info_block)) {
    LOG(ERROR) << "Unable to update references to new PDB info block.";
    return false;
  }

  return true;
}

bool Relinker::WritePDBFile(const FilePath& input_path,
                            const FilePath& output_path) {
  // Generate the map data for both directions.
  std::vector<OMAP> omap_to;
  AddOmapForAllSections(builder().image_layout().sections,
                        builder().image_layout().blocks,
                        original_addr_space(),
                        &omap_to);

  std::vector<OMAP> omap_from;
  AddOmapForAllSections(builder().image_layout().sections,
                        original_addr_space(),
                        builder().image_layout().blocks,
                        &omap_from);

  FilePath temp_pdb;
  if (!file_util::CreateTemporaryFileInDir(output_path.DirName(), &temp_pdb)) {
    LOG(ERROR) << "Unable to create working file in \""
        << output_path.DirName().value() << "\".";
    return false;
  }

  if (!pdb::AddOmapStreamToPdbFile(input_path,
                                   temp_pdb,
                                   new_image_guid_,
                                   omap_to,
                                   omap_from)) {
    LOG(ERROR) << "Unable to add OMAP data to PDB";
    file_util::Delete(temp_pdb, false);
    return false;
  }

  if (!file_util::ReplaceFile(temp_pdb, output_path)) {
    LOG(ERROR) << "Unable to write PDB file to \""
        << output_path.value() << "\".";
    file_util::Delete(temp_pdb, false);
    return false;
  }

  return true;
}

bool Relinker::WriteMetadataSection(const pe::PEFile& input_dll) {
  LOG(INFO) << "Writing metadata.";
  pe::Metadata metadata;
  pe::PEFile::Signature input_dll_sig;
  input_dll.GetSignature(&input_dll_sig);
  if (!metadata.Init(input_dll_sig) ||
      !metadata.SaveToPE(&builder())) {
    LOG(ERROR) << "Unable to write metadata.";
    return false;
  }

  return true;
}

bool Relinker::CopyResourceSection() {
  if (resource_section_id_ == pe::kInvalidSection)
    return true;

  const ImageLayout::SectionInfo& section =
      original_sections()[resource_section_id_];

  LOG(INFO) << "Copying section " << resource_section_id_ << " ("
            << section.name << ").";
  if (!CopySection(section)) {
    LOG(ERROR) << "Unable to copy section.";
    return false;
  }

  return true;
}

}  // namespace relink
