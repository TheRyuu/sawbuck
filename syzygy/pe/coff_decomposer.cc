// Copyright 2013 Google Inc. All Rights Reserved.
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

#include "syzygy/pe/coff_decomposer.h"

#include "base/auto_reset.h"
#include "base/strings/string_split.h"
#include "syzygy/pe/pe_utils.h"

namespace pe {

namespace {

using base::AutoReset;
using block_graph::BlockGraph;
using core::AbsoluteAddress;
using core::RelativeAddress;

typedef BlockGraph::Block Block;
typedef BlockGraph::BlockType BlockType;
typedef BlockGraph::Reference Reference;
typedef BlockGraph::ReferenceType ReferenceType;

typedef std::map<size_t, const char*> ComdatMap;

const char kHeadersBlockName[] = "<headers>";
const char kSymbolsBlockName[] = "<symbols>";
const char kStringsBlockName[] = "<strings>";
const char kRelocsBlockName[] = "<relocs>";

// Retrieve the relocation type and size for the specified COFF
// relocation.
//
// @param reloc the relocation.
// @param ref_type where to store the resulting reference type.
// @param ref_size where to store the resulting reference size.
// @returns true on success, or false if the information cannot be
//     determined for the specified relocation.
bool GetRelocationTypeAndSize(const IMAGE_RELOCATION& reloc,
                              ReferenceType* ref_type,
                              BlockGraph::Size* ref_size) {
  DCHECK(ref_type != NULL);
  DCHECK(ref_size != NULL);

  switch (reloc.Type) {
    case IMAGE_REL_I386_ABSOLUTE:
      // Ignored, as per the specifications.
      return false;
    case IMAGE_REL_I386_DIR32:
      *ref_type = BlockGraph::RELOC_ABSOLUTE_REF;
      *ref_size = sizeof(uint32);
      return true;
    case IMAGE_REL_I386_DIR32NB:
      *ref_type = BlockGraph::RELOC_RELATIVE_REF;
      *ref_size = sizeof(uint32);
      return true;
    case IMAGE_REL_I386_SECTION:
      *ref_type = BlockGraph::RELOC_SECTION_REF;
      *ref_size = sizeof(uint16);
      return true;
    case IMAGE_REL_I386_SECREL:
      *ref_type = BlockGraph::RELOC_SECTION_OFFSET_REF;
      *ref_size = sizeof(uint32);
      return true;
    case IMAGE_REL_I386_SECREL7:
      *ref_type = BlockGraph::RELOC_SECTION_OFFSET_REF;
      // TODO(lenh): This is actually a 7-bit offset;
      // BlockGraph::Reference only represents byte sizes. We pass
      // as a 1-byte reference as there are no actual 8-bit
      // references in COFF files.
      *ref_size = 1;
      return true;
    case IMAGE_REL_I386_REL32:
      *ref_type = BlockGraph::RELOC_PC_RELATIVE_REF;
      *ref_size = sizeof(uint32);
      return true;
    default:
      // Ignore other types; they are either explicitly mentioned as
      // unsupported in the specifications, or for managed code.
      LOG(WARNING) << "Unexpected COFF relocation type.";
      return false;
  }
}

}  // namespace

const char CoffDecomposer::kSectionComdatSep[] = "; COMDAT=";

CoffDecomposer::CoffDecomposer(const CoffFile& image_file)
    : image_file_(image_file),
      image_layout_(NULL),
      image_(NULL) {
}

bool CoffDecomposer::Decompose(ImageLayout* image_layout) {
  DCHECK(image_layout != NULL);

  // Internal temporaries.
  DCHECK(image_layout_ == NULL);
  DCHECK(image_ == NULL);
  AutoReset<ImageLayout*> auto_reset_image_layout(&image_layout_, image_layout);
  AutoReset<BlockGraph::AddressSpace*> auto_reset_image(&image_,
                                                        &image_layout->blocks);

  // Copy the image headers to the layout.
  CopySectionHeadersToImageLayout(
      image_file_.file_header()->NumberOfSections,
      image_file_.section_headers(),
      &image_layout_->sections);

  if (!CopySectionInfoToBlockGraph(image_file_, image_->graph()))
    return false;

  if (!CreateBlocksFromSections())
    return false;

  if (!CreateBlocksAndReferencesFromNonSections())
    return false;

  // TODO(lenh): Add references from debug sections.

  if (!CreateReferencesFromRelocations())
    return false;

  if (!CreateLabelsFromSymbols())
    return false;

  return true;
}

bool CoffDecomposer::CreateBlocksAndReferencesFromNonSections() {
  DCHECK(image_ != NULL);

  if (!CreateBlocksAndReferencesFromSymbolAndStringTables())
    return false;

  if (!CreateBlocksFromRelocationTables())
    return false;

  if (!CreateBlocksAndReferencesFromHeaders())
    return false;

  return true;
}

bool CoffDecomposer::CreateBlocksAndReferencesFromHeaders() {
  const IMAGE_FILE_HEADER* file_header = image_file_.file_header();
  DCHECK(file_header != NULL);

  // Create a block for COFF and section headers.
  size_t headers_size =
      sizeof(*file_header) +
      file_header->NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
  Block* block = CreateBlock(BlockGraph::DATA_BLOCK, FileOffsetAddress(0),
                             headers_size, kHeadersBlockName);
  if (block == NULL) {
    LOG(ERROR) << "Unable to create block for headers.";
    return false;
  }
  block->set_attribute(BlockGraph::COFF_HEADERS);

  // Create a reference for the symbol table pointer.
  FileOffsetAddress symbols_ptr_addr(
      offsetof(IMAGE_FILE_HEADER, PointerToSymbolTable));
  if (!CreateFileOffsetReference(
          symbols_ptr_addr,
          BlockGraph::FILE_OFFSET_REF,
          sizeof(file_header->PointerToSymbolTable),
          FileOffsetAddress(file_header->PointerToSymbolTable))) {
    return false;
  }

  // Create a reference for the section and relocation pointers in
  // each section header.
  FileOffsetAddress section_headers_start(
      sizeof(*file_header) + file_header->SizeOfOptionalHeader);
  size_t num_sections = image_file_.file_header()->NumberOfSections;
  for (size_t i = 0; i < num_sections; ++i) {
    const IMAGE_SECTION_HEADER* header = image_file_.section_header(i);
    DCHECK(header != NULL);
    FileOffsetAddress start(section_headers_start + i * sizeof(*header));

    FileOffsetAddress data_ptr_addr(
        start + offsetof(IMAGE_SECTION_HEADER, PointerToRawData));
    if (!CreateFileOffsetReference(
            data_ptr_addr,
            BlockGraph::FILE_OFFSET_REF,
            sizeof(header->PointerToRawData),
            FileOffsetAddress(header->PointerToRawData))) {
      return false;
    }

    FileOffsetAddress relocs_ptr_addr(
        start + offsetof(IMAGE_SECTION_HEADER, PointerToRelocations));
    if (!CreateFileOffsetReference(
            relocs_ptr_addr,
            BlockGraph::FILE_OFFSET_REF,
            sizeof(header->PointerToRelocations),
            FileOffsetAddress(header->PointerToRelocations))) {
      return false;
    }
  }

  return true;
}

bool CoffDecomposer::CreateBlocksAndReferencesFromSymbolAndStringTables() {
  // Create a block for the symbol table.
  FileOffsetAddress symbols_start(image_file_.symbols_address());
  size_t symbols_size = image_file_.symbols_size();
  Block* block = CreateBlock(BlockGraph::DATA_BLOCK,
                             symbols_start, symbols_size, kSymbolsBlockName);
  if (block == NULL) {
    LOG(ERROR) << "Unable to create block for symbols.";
    return false;
  }
  block->set_attribute(BlockGraph::COFF_SYMBOL_TABLE);

  // Create a block for the strings table that follows.
  FileOffsetAddress strings_start(image_file_.strings_address());
  size_t strings_size = image_file_.strings_size();
  block = CreateBlock(BlockGraph::DATA_BLOCK,
                      strings_start, strings_size, kStringsBlockName);
  if (block == NULL) {
    LOG(ERROR) << "Unable to create block for strings.";
    return false;
  }
  block->set_attribute(BlockGraph::COFF_STRING_TABLE);

  // Add references.
  size_t num_symbols = image_file_.file_header()->NumberOfSymbols;
  const IMAGE_SYMBOL* symbol = NULL;
  for (size_t i = 0; i < num_symbols; i += 1 + symbol->NumberOfAuxSymbols) {
    symbol = image_file_.symbol(i);

    // Ignore external symbols (no references to blocks) and other
    // kinds of non-reference symbols.
    if (symbol->SectionNumber <= 0)
      continue;

    FileOffsetAddress start(symbols_start + i * sizeof(*symbol));

    FileOffsetAddress value_addr(start + offsetof(IMAGE_SYMBOL, Value));
    if (!CreateSymbolOffsetReference(
            value_addr,
            BlockGraph::SECTION_OFFSET_REF,
            sizeof(symbol->Value),
            symbol,
            symbol->Value)) {
      return false;
    }

    FileOffsetAddress section_addr(
        start + offsetof(IMAGE_SYMBOL, SectionNumber));
    if (!CreateSymbolOffsetReference(
            section_addr,
            BlockGraph::SECTION_REF,
            sizeof(symbol->SectionNumber),
            symbol,
            0)) {
      return false;
    }

    // Section definitions for associative COMDAT sections require an
    // additional section reference within the auxiliary symbol.
    if (symbol->StorageClass == IMAGE_SYM_CLASS_STATIC &&
        symbol->Type >> 4 != IMAGE_SYM_DTYPE_FUNCTION &&
        symbol->NumberOfAuxSymbols == 1) {
      const IMAGE_AUX_SYMBOL* aux =
          reinterpret_cast<const IMAGE_AUX_SYMBOL*>(image_file_.symbol(i + 1));
      DCHECK(aux != NULL);
      if (aux->Section.Selection == IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
        FileOffsetAddress number_addr(
            start + sizeof(IMAGE_SYMBOL) +
            offsetof(IMAGE_AUX_SYMBOL, Section.Number));
        if (!CreateSectionOffsetReference(
                number_addr,
                BlockGraph::SECTION_REF,
                sizeof(short),
                aux->Section.Number - 1,
                0)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool CoffDecomposer::CreateBlocksFromRelocationTables() {
  size_t num_sections = image_file_.file_header()->NumberOfSections;
  for (size_t i = 0; i < num_sections; ++i) {
    const IMAGE_SECTION_HEADER* header = image_file_.section_header(i);
    DCHECK(header != NULL);
    if (header->NumberOfRelocations == 0)
      continue;

    FileOffsetAddress relocs_start(header->PointerToRelocations);
    size_t relocs_size(header->NumberOfRelocations * sizeof(IMAGE_RELOCATION));

    // Create a block for this relocation table.
    Block* block =
        CreateBlock(BlockGraph::DATA_BLOCK,
                    relocs_start, relocs_size, kRelocsBlockName);
    if (block == NULL)
      return false;
    block->set_attribute(BlockGraph::COFF_RELOC_DATA);
  }
  return true;
}

bool CoffDecomposer::CreateBlocksFromSections() {
  DCHECK(image_ != NULL);

  // Build COMDAT symbol map, which associates each COMDAT section
  // with the COMDAT (secondary) symbol. When compiling with
  // function-level linking (/Gy for MSVC), all data and code lives in
  // COMDAT sections. Each COMDAT section is associated with at least
  // one symbol in the symbol table (the primary symbol), but usually
  // two or more.
  //
  // The primary symbol must always be the section symbol, which
  // indicates which final executable section the COMDAT section will
  // need to be merged into (e.g., .text or .data).
  //
  // The secondary symbol, when it exists, is the first symbol bound
  // to the COMDAT section that comes after the primary (usually but
  // not necessarily right after). With function-level linking, the
  // secondary symbol is always the name of the function or variable
  // defined in the section.
  //
  // The COFF decomposer assumes functions live in their own sections,
  // which is guaranteed by the MSVC compiler documentation for /Gy,
  // but is more forgiving when it comes to variables, which may be
  // grouped together in one or multiple data sections.
  ComdatMap comdat_map;
  size_t num_symbols = image_file_.file_header()->NumberOfSymbols;
  const IMAGE_SYMBOL* symbol = NULL;
  for (size_t i = 0; i < num_symbols; i += 1 + symbol->NumberOfAuxSymbols) {
    symbol = image_file_.symbol(i);
    DCHECK(symbol != NULL);
    if (symbol->SectionNumber <= 0)
      continue;
    size_t section_index = symbol->SectionNumber - 1;

    // Skip non-COMDAT sections.
    const IMAGE_SECTION_HEADER* header =
        image_file_.section_header(section_index);
    DCHECK(header != NULL);
    if ((header->Characteristics & IMAGE_SCN_LNK_COMDAT) == 0)
      continue;

    // Skip primary section symbols.
    ComdatMap::iterator it = comdat_map.find(section_index);
    if (it == comdat_map.end()) {
      comdat_map.insert(std::make_pair(section_index,
                                       static_cast<const char*>(0)));
    } else {
      // Skip symbols after the second one.
      if (it->second != NULL)
        continue;

      // This should be the second symbol (assuming the first one is
      // the section symbol, as mandated by the specifications), that
      // is, the COMDAT symbol.
      it->second = image_file_.GetSymbolName(i);
    }
  }

  // Build a block for each data or code section.
  size_t num_sections = image_file_.file_header()->NumberOfSections;
  for (size_t i = 0; i < num_sections; ++i) {
    const IMAGE_SECTION_HEADER* header = image_file_.section_header(i);
    DCHECK(header != NULL);
    BlockType block_type = GetSectionType(*header) == kSectionCode ?
                           BlockGraph::CODE_BLOCK : BlockGraph::DATA_BLOCK;

    // Retrieve or make up a suitable name for the block.
    std::string name(image_file_.GetSectionName(*header));
    ComdatMap::iterator it = comdat_map.find(i);
    if (it != comdat_map.end()) {
      name.append(kSectionComdatSep);
      if (it->second != NULL)
        name.append(it->second);
    }

    // Compute the address of the block; when using function-level linking,
    // each function begins at offset zero. Unmapped sections (BSS) get an
    // unmapped block with an invalid address.
    FileOffsetAddress addr(FileOffsetAddress::kInvalidAddress);
    if (image_file_.IsSectionMapped(i)) {
      CHECK(image_file_.SectionOffsetToFileOffset(i, 0, &addr));
    }

    // Put everything together into a block.
    Block* block = CreateBlock(block_type,
                               addr, header->SizeOfRawData, name.c_str());
    if (block == NULL) {
      LOG(ERROR) << "Unable to create block for section " << i << " \""
                 << name << "\".";
      return false;
    }

    // Assuming block graph section IDs match those of the image file.
    block->set_section(i);
    block->set_attribute(image_file_.IsSectionMapped(i) ?
                         BlockGraph::SECTION_CONTRIB : BlockGraph::COFF_BSS);

    // Add to section-block map so we can find it later.
    section_block_map_.insert(std::make_pair(i, block));
  }

  return true;
}

bool CoffDecomposer::CreateReferencesFromRelocations() {
  DCHECK(image_ != NULL);

  CoffFile::RelocMap reloc_map;
  image_file_.DecodeRelocs(&reloc_map);

  CoffFile::RelocMap::iterator it = reloc_map.begin();
  for (; it != reloc_map.end(); ++it) {
    DCHECK(it->second != NULL);
    const IMAGE_SYMBOL* symbol =
        image_file_.symbol(it->second->SymbolTableIndex);
    DCHECK(symbol != NULL);

    // Compute reference attributes.
    ReferenceType ref_type = BlockGraph::REFERENCE_TYPE_MAX;
    BlockGraph::Size ref_size = 0;
    if (!GetRelocationTypeAndSize(*it->second, &ref_type, &ref_size))
      continue;
    DCHECK_LT(ref_type, BlockGraph::REFERENCE_TYPE_MAX);
    DCHECK_GT(ref_size, 0u);

    // Add reference.
    size_t offset = symbol->SectionNumber == 0 ? 0 : symbol->Value;
    if (!CreateSymbolOffsetReference(it->first, ref_type, ref_size,
                                     symbol, offset)) {
      return false;
    }
  }

  return true;
}

bool CoffDecomposer::CreateLabelsFromSymbols() {
  DCHECK(image_ != NULL);

  size_t num_symbols = image_file_.file_header()->NumberOfSymbols;
  const IMAGE_SYMBOL* symbol;
  for (size_t i = 0; i < num_symbols; i += 1 + symbol->NumberOfAuxSymbols) {
    symbol = image_file_.symbol(i);

    // Data labels should reference a valid section, have storage
    // class STATIC, a non-function type (contrary to static
    // functions), and no auxiliary record (contrary to section
    // definitions). Skip the rest.
    //
    // MSVC records section descriptions in the symbol table as STATIC
    // data symbols; hence a section symbol and the first data symbol
    // at offset zero will have the same storage class and offset;
    // data symbols, however, occupy a single entry in the table,
    // whereas section symbols take two records (hence one auxiliary
    // record with class-specific data in addition of the main
    // record).
    if (!(symbol->SectionNumber > 0 &&
          symbol->StorageClass == IMAGE_SYM_CLASS_STATIC &&
          symbol->Type >> 4 != IMAGE_SYM_DTYPE_FUNCTION &&
          symbol->NumberOfAuxSymbols == 0)) {
      continue;
    }
    size_t section_index = symbol->SectionNumber - 1;

    // Skip labels in non-code sections.
    const IMAGE_SECTION_HEADER* header =
        image_file_.section_header(section_index);
    DCHECK(header != NULL);
    if (GetSectionType(*header) != kSectionCode)
      continue;

    // Get block and offset.
    SectionBlockMap::iterator it = section_block_map_.find(section_index);
    DCHECK(it != section_block_map_.end());
    Block* block = it->second;
    DCHECK(block != NULL);
    BlockGraph::Offset offset = symbol->Value;

    // Tables only appear in code blocks; ignore others.
    if (block->type() != BlockGraph::CODE_BLOCK)
      continue;

    // Compute label attributes. Jump tables are always an array of
    // pointers, thus they coincide exactly with a reference. Case
    // tables are simple arrays of integer values, thus do not
    // coincide with a reference.
    BlockGraph::LabelAttributes attrs = 0;
    if (block->references().find(offset) != block->references().end()) {
      attrs |= BlockGraph::JUMP_TABLE_LABEL;
    } else {
      attrs |= BlockGraph::CASE_TABLE_LABEL;
    }

    // Add label.
    const char* name = image_file_.GetSymbolName(i);
    if (!AddLabelToBlock(offset, name, attrs, block))
      return false;
  }
  return true;
}

Block* CoffDecomposer::CreateBlock(BlockType type,
                                   FileOffsetAddress addr,
                                   BlockGraph::Size size,
                                   const base::StringPiece& name) {
  DCHECK(image_ != NULL);

  if (addr == FileOffsetAddress::kInvalidAddress) {
    // Unmapped block.
    Block* block = image_->graph()->AddBlock(type, size, name);
    if (block == NULL) {
      LOG(ERROR) << "Unable to add unmapped block \"" << name.as_string()
                 << "\" with size " << size << ".";
      return NULL;
    }
    return block;
  }

  // Otherwise, we have a normal mapped block.
  BlockGraphAddress block_addr(FileOffsetToBlockGraphAddress(addr));
  Block* block = image_->AddBlock(type, block_addr, size, name);
  if (block == NULL) {
    LOG(ERROR) << "Unable to add block \"" << name.as_string() << "\" at "
               << block_addr << " with size " << size << ".";
    return NULL;
  }

  // Mark the source range from whence this block originates.
  bool pushed = block->source_ranges().Push(
      Block::DataRange(0, size),
      Block::SourceRange(block_addr, size));
  DCHECK(pushed);

  const uint8* data = image_file_.GetImageData(addr, size);
  if (data != NULL)
    block->SetData(data, size);

  return block;
}

bool CoffDecomposer::CreateReference(FileOffsetAddress src_addr,
                                     ReferenceType ref_type,
                                     BlockGraph::Size ref_size,
                                     Block* target,
                                     BlockGraph::Offset offset) {
  DCHECK(image_ != NULL);

  // Get source block and offset.
  Block* source = NULL;
  BlockGraph::Offset src_offset = -1;
  if (!FileOffsetToBlockOffset(src_addr, &source, &src_offset))
    return false;
  DCHECK(source != NULL);
  DCHECK_GE(src_offset, 0);

  // Find an existing reference, or insert a new one.
  Reference ref(ref_type, ref_size, target, offset, offset);
  Block::ReferenceMap::const_iterator ref_it =
      source->references().find(src_offset);
  if (ref_it == source->references().end()) {
    // New reference.
    CHECK(source->SetReference(src_offset, ref));
  } else {
    // Collisions are only allowed if the references are identical.
    if (!(ref == ref_it->second)) {
      LOG(ERROR) << "Block \"" << source->name() << "\" has a conflicting "
                 << "reference at offset " << src_offset << ".";
      return false;
    }
  }

  return true;
}

bool CoffDecomposer::CreateFileOffsetReference(FileOffsetAddress src_addr,
                                               ReferenceType ref_type,
                                               BlockGraph::Size ref_size,
                                               FileOffsetAddress dst_addr) {
  DCHECK(image_ != NULL);

  // Get target section and offset.
  Block* target = NULL;
  BlockGraph::Offset offset = -1;
  if (!FileOffsetToBlockOffset(dst_addr, &target, &offset))
    return false;
  DCHECK(target != NULL);
  DCHECK_GE(offset, 0);

  // Add reference.
  if (!CreateReference(src_addr, ref_type, ref_size, target, offset))
    return false;

  return true;
}

bool CoffDecomposer::CreateSectionOffsetReference(FileOffsetAddress src_addr,
                                                  ReferenceType ref_type,
                                                  BlockGraph::Size ref_size,
                                                  size_t section_index,
                                                  size_t section_offset) {
  DCHECK(image_ != NULL);

  // Get target section and offset.
  Block* target = NULL;
  BlockGraph::Offset offset = -1;
  if (!SectionOffsetToBlockOffset(section_index, section_offset,
                                  &target, &offset)) {
    return false;
  }
  DCHECK(target != NULL);
  DCHECK_GE(offset, 0);

  // Add reference.
  if (!CreateReference(src_addr, ref_type, ref_size, target, offset))
    return false;

  return true;
}

bool CoffDecomposer::CreateSymbolOffsetReference(FileOffsetAddress src_addr,
                                                 ReferenceType ref_type,
                                                 BlockGraph::Size ref_size,
                                                 const IMAGE_SYMBOL* symbol,
                                                 size_t offset) {
  DCHECK(image_ != NULL);
  DCHECK(symbol != NULL);

  if (symbol->SectionNumber < 0) {
    LOG(ERROR) << "Symbol cannot be converted to a reference.";
    return false;
  }

  if (symbol->SectionNumber != 0) {
    // Section symbol.
    return CreateSectionOffsetReference(src_addr, ref_type, ref_size,
                                        symbol->SectionNumber - 1, offset);
  } else {
    // External symbol. As a convention, we use a reference to the symbol
    // table, since there is no corresponding block. The offset is ignored
    // (will be inferred from the symbol value and reference type).
    size_t symbol_index = symbol - image_file_.symbols();
    return CreateFileOffsetReference(
        src_addr, ref_type, ref_size,
        image_file_.symbols_address() + symbol_index * sizeof(*symbol));
  }
}

bool CoffDecomposer::FileOffsetToBlockOffset(FileOffsetAddress addr,
                                             Block** block,
                                             BlockGraph::Offset* offset) {
  DCHECK(image_ != NULL);
  DCHECK(block != NULL);
  DCHECK(offset != NULL);

  // Get block and offset.
  BlockGraphAddress actual_addr(FileOffsetToBlockGraphAddress(addr));
  Block* containing_block = image_->GetBlockByAddress(actual_addr);
  if (containing_block == NULL) {
    LOG(ERROR) << "File offset " << addr << " does not lie within a block.";
    return false;
  }
  BlockGraphAddress block_addr;
  CHECK(image_->GetAddressOf(containing_block, &block_addr));

  *block = containing_block;
  *offset = actual_addr - block_addr;
  return true;
}

bool CoffDecomposer::SectionOffsetToBlockOffset(size_t section_index,
                                                size_t section_offset,
                                                Block** block,
                                                BlockGraph::Offset* offset) {
  DCHECK(image_ != NULL);
  DCHECK_NE(BlockGraph::kInvalidSectionId, section_index);
  DCHECK_LT(section_index, image_file_.file_header()->NumberOfSections);
  DCHECK_LT(section_offset,
            image_file_.section_header(section_index)->SizeOfRawData);
  DCHECK(block != NULL);
  DCHECK(offset != NULL);

  // Get block and offset.
  SectionBlockMap::iterator it = section_block_map_.find(section_index);
  if (it == section_block_map_.end()) {
    LOG(ERROR) << "Section " << section_index << " is not mapped to a block.";
    return false;
  }
  DCHECK(it->second != NULL);
  DCHECK_LT(section_offset, it->second->size());

  *block = it->second;
  *offset = section_offset;
  return true;
}

CoffDecomposer::BlockGraphAddress CoffDecomposer::FileOffsetToBlockGraphAddress(
    FileOffsetAddress addr) {
  return BlockGraphAddress(addr.value());
}

}  // namespace pe
