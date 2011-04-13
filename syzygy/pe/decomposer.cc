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
//
#include "syzygy/pe/decomposer.h"

#include <cvconst.h>
#include <diacreate.h>
#include "base/file_path.h"
#include "base/path_service.h"
#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "base/win/scoped_bstr.h"
#include "base/win/scoped_comptr.h"
#include "sawbuck/sym_util/types.h"
#include "syzygy/pe/pe_file_parser.h"

using base::win::ScopedBstr;
using base::win::ScopedComPtr;

namespace {

bool IsSymTag(IDiaSymbol* symbol, DWORD expected_sym_tag) {
  DWORD sym_tag = SymTagNull;
  HRESULT hr = symbol->get_symTag(&sym_tag);
  if (FAILED(hr)) {
    NOTREACHED() << "Error getting sym tag";
    return false;
  }

  return sym_tag == expected_sym_tag;
}

bool CreateDiaSource(IDiaDataSource** created_source) {
  ScopedComPtr<IDiaDataSource> dia_source;
  if (SUCCEEDED(dia_source.CreateInstance(CLSID_DiaSource))) {
    *created_source = dia_source.Detach();
    return true;
  }

  if (SUCCEEDED(NoRegCoCreate(L"msdia90.dll",
                              CLSID_DiaSource,
                              IID_IDiaDataSource,
                              reinterpret_cast<void**>(&dia_source)))) {
    *created_source = dia_source.Detach();
    return true;
  }

  return false;
}

// Given a symbol @p symbol, this will inspect its type information
// to determine its length. If the symbol has no type information, sets @p size
// to zero. Returns true on success, false otherwise.
bool GetTypeSize(IDiaSymbol* symbol, size_t* size) {
  DCHECK(symbol != NULL);
  DCHECK(size != NULL);

  *size = 0;
  ScopedComPtr<IDiaSymbol> type;
  HRESULT hr = symbol->get_type(type.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get type symbol: " << hr;
    return false;
  }
  if (hr == S_FALSE)
    return true;

  ULONGLONG length = 0;
  if (FAILED(type->get_length(&length))) {
    LOG(ERROR) << "Failed to retrieve type length.";
    return false;
  }

  *size = static_cast<size_t>(length);
  return true;
}

}  // namespace

namespace pe {

using core::AbsoluteAddress;
using core::BlockGraph;
using core::RelativeAddress;

Decomposer::Decomposer(const PEFile& image_file,
                       const FilePath& file_path)
    : image_(NULL),
      image_file_(image_file),
      file_path_(file_path),
      current_block_(NULL) {
}

bool Decomposer::Decompose(DecomposedImage* decomposed_image) {
  // Start by instantiating and initializing our Debug Interface Access session.
  ScopedComPtr<IDiaDataSource> dia_source;
  if (!CreateDiaSource(dia_source.Receive())) {
    LOG(ERROR) << "Failed to create DIA source object.";
    return false;
  }

  HRESULT hr = dia_source->loadDataForExe(file_path_.value().c_str(),
                                          NULL,
                                          NULL);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to load DIA data for image file: " << hr;
    return false;
  }

  ScopedComPtr<IDiaSession> dia_session;
  hr = dia_source->openSession(dia_session.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to open DIA session: " << hr;
    return false;
  }

  hr = dia_session->put_loadAddress(
      image_file_.nt_headers()->OptionalHeader.ImageBase);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to set the DIA load address: " << hr;
    return false;
  }

  ScopedComPtr<IDiaSymbol> global;
  hr = dia_session->get_globalScope(global.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get the DIA global scope: " << hr;
    return false;
  }

  image_ = &decomposed_image->address_space;

  // Chunk out important PE image structures, like the headers and such.
  bool success = CreatePEImageBlocksAndReferences(&decomposed_image->header);

  // Chunk out blocks for each function and thunk in the image.
  if (success)
    success = CreateCodeBlocks(global);

  // Chunk out data blocks (currently one block per data segment).
  // This also builds a list of zero length data labels that did not map
  // to a pre-existing block.
  Labels data_labels;
  if (success)
    success = CreateDataBlocks(global, &data_labels);

  // This takes orphaned data labels and adds them to the appropriate blocks.
  if (success)
    success = CreateDataLabels(data_labels);

  data_labels.clear();

  // Create absolute references for each relocation entry.
  if (success)
    success = CreateRelocationReferences();

  // Disassemble code blocks and create PC-relative references
  if (success)
    success = CreateCodeReferences();

  // Turn the address->address format references we've created into
  // block->block references on the blocks in the image.
  if (success)
    success = FinalizeIntermediateReferences();

  if (success)
    success = LoadOmapInformation(dia_session,
                                  &decomposed_image->omap_to,
                                  &decomposed_image->omap_from);

  image_ = NULL;

  return success;
}

bool Decomposer::CreateCodeBlocks(IDiaSymbol* global) {
  HANDLE process = reinterpret_cast<HANDLE>(this);

  if (!CreateFunctionBlocks(global))
    return false;
  if (!CreateThunkBlocks(global))
    return false;

  size_t num_sections = image_file_.nt_headers()->FileHeader.NumberOfSections;
  for (size_t i = 0; i < num_sections; ++i)  {
    const IMAGE_SECTION_HEADER* header = image_file_.section_header(i);
    // Skip non-code sections.
    if ((header->Characteristics & IMAGE_SCN_CNT_CODE) != 0) {
      if (!CreateSectionGapBlocks(header, BlockGraph::CODE_BLOCK)) {
        LOG(ERROR) << "Failed to create gap blocks for section "
            << header->Name;
        return false;
      }
    }
  }

  return CreateGlobalLabels(global);
}

bool Decomposer::CreateFunctionBlocks(IDiaSymbol* global) {
  DCHECK(IsSymTag(global, SymTagExe));

  // Otherwise enumerate its offspring.
  ScopedComPtr<IDiaEnumSymbols> dia_enum_symbols;
  HRESULT hr = global->findChildren(SymTagFunction,
                                    NULL,
                                    nsNone,
                                    dia_enum_symbols.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get the DIA function enumerator: " << hr;
    return false;
  }

  while (true) {
    ScopedComPtr<IDiaSymbol> function;
    ULONG fetched = 0;
    hr = dia_enum_symbols->Next(1, function.Receive(), &fetched);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to enumerate functions.";
      return false;
    }
    if (hr != S_OK || fetched == 0)
      break;

    // Create the block representing the function.
    DCHECK(IsSymTag(function, SymTagFunction));
    if (!CreateFunctionBlock(function))
      return false;
  }

  return true;
}

bool Decomposer::CreateFunctionBlock(IDiaSymbol* function) {
  DCHECK(IsSymTag(function, SymTagFunction) || IsSymTag(function, SymTagThunk));

  DWORD location_type = LocIsNull;
  if (FAILED(function->get_locationType(&location_type))) {
    LOG(ERROR) << "Failed to retrieve function address type.";
    return false;
  }
  if (location_type != LocIsStatic) {
    DCHECK_EQ(static_cast<DWORD>(LocIsNull), location_type);
    return true;
  }

  DWORD rva = 0;
  ULONGLONG length = 0;
  ScopedBstr name;
  BOOL no_return = FALSE;
  if (FAILED(function->get_relativeVirtualAddress(&rva)) ||
      FAILED(function->get_length(&length)) ||
      FAILED(function->get_name(name.Receive())) ||
      FAILED(function->get_noReturn(&no_return))) {
    LOG(ERROR) << "Failed to retrieve function information";
    return false;
  }

  std::string block_name;
  if (!WideToUTF8(name, name.Length(), &block_name)) {
    LOG(ERROR) << "Failed to convert symbol name to UTF8";
    return false;
  }

  BlockGraph::Block* block =
      FindOrCreateBlock(BlockGraph::CODE_BLOCK,
                        RelativeAddress(rva),
                        static_cast<BlockGraph::Size>(length),
                        block_name.c_str());
  if (block == NULL)
    return false;

  DCHECK(block->data() != NULL);

  block->SetLabel(0, block_name.c_str());
  if (no_return == TRUE)
    block->set_attribute(BlockGraph::NON_RETURN_FUNCTION);

  return CreateLabelsForFunction(function, block);
}

bool Decomposer::CreateLabelsForFunction(IDiaSymbol* function,
                                         BlockGraph::Block* block) {
  // Enumerate the label offspring of function.
  ScopedComPtr<IDiaEnumSymbols> dia_enum_symbols;
  HRESULT hr = function->findChildren(SymTagLabel,
                                      NULL,
                                      nsNone,
                                      dia_enum_symbols.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get the DIA label enumerator: " << hr;
    return false;
  }

  while (true) {
    ScopedComPtr<IDiaSymbol> symbol;
    ULONG fetched = 0;
    hr = dia_enum_symbols->Next(1, symbol.Receive(), &fetched);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to enumerate the DIA symbol.";
      return false;
    }
    if (hr != S_OK || fetched == 0)
      break;

    DCHECK(IsSymTag(symbol, SymTagLabel));
    DWORD rva = 0;
    ScopedBstr name;
    if (FAILED(symbol->get_relativeVirtualAddress(&rva)) ||
        FAILED(symbol->get_name(name.Receive()))) {
      LOG(ERROR) << "Failed to retrieve function information";
      return false;
    }

    RelativeAddress addr;
    if (!image_->GetAddressOf(block, &addr)) {
      NOTREACHED() << "Block " << block->name() << " has no address";
      return false;
    }

    RelativeAddress label_rva(rva);
    if (label_rva < addr && label_rva >= addr + block->size()) {
      LOG(ERROR) << "Label outside function";
      return false;
    }

    std::string label_name;
    if (!WideToUTF8(name, name.Length(), &label_name)) {
      LOG(ERROR) << "Failed to convert label name to UTF8";
      return false;
    }

    block->SetLabel(label_rva - addr, label_name.c_str());
  }

  return true;
}

bool Decomposer::CreateThunkBlocks(IDiaSymbol* globals) {
  ScopedComPtr<IDiaEnumSymbols> enum_compilands;
  HRESULT hr = globals->findChildren(SymTagCompiland,
                                     NULL,
                                     nsNone,
                                     enum_compilands.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to retrieve compiland enumerator: " << hr;
    return false;
  }

  while (true) {
    ScopedComPtr<IDiaSymbol> compiland;
    ULONG fetched = 0;
    hr = enum_compilands->Next(1, compiland.Receive(), &fetched);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to enumerate compiland enumerator.";
      return false;
    }
    if (hr != S_OK || fetched == 0)
      break;

    ScopedComPtr<IDiaEnumSymbols> enum_thunks;
    hr = compiland->findChildren(SymTagThunk,
                                 NULL,
                                 nsNone,
                                 enum_thunks.Receive());
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to retrieve thunk enumerator: " << hr;
      return false;
    }

    while (true) {
      ScopedComPtr<IDiaSymbol> thunk;
      hr = enum_thunks->Next(1, thunk.Receive(), &fetched);
      if (FAILED(hr)) {
        LOG(ERROR) << "Failed to enumerate thunk enumerator: " << hr;
        return false;
      }
      if (hr != S_OK || fetched == 0)
        break;


      DCHECK(IsSymTag(thunk, SymTagThunk));

      if (!CreateFunctionBlock(thunk))
        return false;
    }
  }

  return true;
}

bool Decomposer::CreateGlobalLabels(IDiaSymbol* globals) {
  ScopedComPtr<IDiaEnumSymbols> enum_compilands;
  HRESULT hr = globals->findChildren(SymTagCompiland,
                                     NULL,
                                     nsNone,
                                     enum_compilands.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to retrieve compiland enumerator: " << hr;
    return false;
  }

  while (true) {
    ScopedComPtr<IDiaSymbol> compiland;
    ULONG fetched = 0;
    hr = enum_compilands->Next(1, compiland.Receive(), &fetched);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to enumerate compiland enumerator.";
      return false;
    }
    if (hr != S_OK || fetched == 0)
      break;

    ScopedComPtr<IDiaEnumSymbols> enum_labels;
    hr = compiland->findChildren(SymTagLabel,
                                 NULL,
                                 nsNone,
                                 enum_labels.Receive());
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to retrieve label enumerator: " << hr;
      return false;
    }

    while (true) {
      ScopedComPtr<IDiaSymbol> label;
      hr = enum_labels->Next(1, label.Receive(), &fetched);
      if (FAILED(hr)) {
        LOG(ERROR) << "Failed to enumerate label enumerator.";
        return false;
      }
      if (hr != S_OK || fetched == 0)
        break;

      DCHECK(IsSymTag(label, SymTagLabel));

      DWORD addr = 0;
      ScopedBstr name;
      if (FAILED(label->get_relativeVirtualAddress(&addr)) ||
          FAILED(label->get_name(name.Receive()))) {
        LOG(ERROR) << "Failed to retrieve label address or name";
        return false;
      }

      RelativeAddress label_addr(addr);
      BlockGraph::Block* block = image_->GetBlockByAddress(label_addr);
      if (block == NULL) {
        LOG(ERROR) << "No block for label " << name << " at " << addr;
        return false;
      }

      std::string label_name;
      if (!WideToUTF8(name, name.Length(), &label_name)) {
        LOG(ERROR) << "Failed to convert label name to UTF8";
        return false;
      }

      RelativeAddress block_addr;
      if (!image_->GetAddressOf(block, &block_addr)) {
        NOTREACHED() << "Block " << block->name() << " has no address";
        return false;
      }

      block->SetLabel(label_addr - block_addr, label_name.c_str());
    }
  }

  return true;
}

bool Decomposer::CreateSectionGapBlocks(const IMAGE_SECTION_HEADER* header,
                                        BlockGraph::BlockType block_type) {
  RelativeAddress section_begin(header->VirtualAddress);
  RelativeAddress section_end(section_begin + header->Misc.VirtualSize);
  RelativeAddress image_end(
      image_file_.nt_headers()->OptionalHeader.SizeOfImage);

  // Search for the first and last blocks interesting from the start and end
  // of the section to the end of the image.
  BlockGraph::AddressSpace::RangeMap::const_iterator it(
      image_->address_space_impl().FindFirstIntersection(
          BlockGraph::AddressSpace::Range(section_begin,
                                          image_end - section_begin)));
  BlockGraph::AddressSpace::RangeMap::const_iterator end(
      image_->address_space_impl().FindFirstIntersection(
          BlockGraph::AddressSpace::Range(section_end,
                                          image_end - section_end)));

  if (it == end) {
    // No block for the section, map the whole thing.
    BlockGraph::Block* section = FindOrCreateBlock(
        block_type, section_begin, section_end - section_begin,
        StringPrintf("Section %s", header->Name).c_str());
    DCHECK(section != NULL);
    if (section->data() == NULL) {
      // The section is only partially defined.
      const uint8* data = image_file_.GetImageData(section_begin,
                                                   header->SizeOfRawData);
      DCHECK(data != NULL);
      section->set_data(data);
      section->set_data_size(header->SizeOfRawData);
    }

    return true;
  }

  // Do we need to fill in the start?
  if (it->first.start() < section_begin) {
    if (!FindOrCreateBlock(block_type,
                           section_begin,
                           it->first.start() - section_begin,
                           "Start Block")) {
      LOG(ERROR) << "Failed to create block for start of code section "
          << header->Name;
      return false;
    }
  }

  // Now iterate the blocks and fill in gaps.
  for (; it != end; ++it) {
    const BlockGraph::Block* block = it->second;
    DCHECK(block != NULL);
    RelativeAddress block_end = it->first.start() + block->size();
    if (block_end >= section_end)
      break;

    // Walk to the next address in turn.
    BlockGraph::AddressSpace::RangeMap::const_iterator next = it;
    ++next;
    if (next == end) {
      // We're at the end of the list.
      DCHECK(block_end < section_end);
      BlockGraph::Block* added = FindOrCreateBlock(block_type,
                                                   block_end,
                                                   section_end - block_end,
                                                   "Tail Block");
      DCHECK(added != NULL);
      break;
    }

    if (block_end < next->first.start()) {
      BlockGraph::Block* added = FindOrCreateBlock(
          block_type, block_end, next->first.start() - block_end,
          StringPrintf("Gap%08X", block_end).c_str());
      DCHECK(added != NULL);
    }
  }

  return true;
}

bool Decomposer::CreateDataBlocks(IDiaSymbol* global, Labels* labels) {
  ScopedComPtr<IDiaEnumSymbols> enum_data;
  HRESULT hr = global->findChildren(SymTagData,
                                    NULL,
                                    nsNone,
                                    enum_data.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get the DIA data enumerator: " << hr;
    return false;
  }

  while (true) {
    ScopedComPtr<IDiaSymbol> data;
    ULONG fetched = 0;
    hr = enum_data->Next(1, data.Receive(), &fetched);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to enumerate data: " << hr;
      return false;
    }
    if (hr != S_OK || fetched == 0)
      break;

    DCHECK(IsSymTag(data, SymTagData));

    // Create the block representing the datum.
    if (!CreateDataBlock(data, labels))
      return false;
  }

  size_t num_sections = image_file_.nt_headers()->FileHeader.NumberOfSections;
  // Iterate through all the image sections.
  for (size_t i = 0; i < num_sections; ++i) {
    const IMAGE_SECTION_HEADER* header = image_file_.section_header(i);
    DCHECK(header != NULL);

    // And create a block for any gaps in data sections.
    const DWORD kDataCharacteristics =
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA;
    if (header->Characteristics & kDataCharacteristics) {
      if (!CreateSectionGapBlocks(header, BlockGraph::DATA_BLOCK)) {
        LOG(ERROR) << "Unable to create gap blocks for data segment "
              << header->Name;
        return false;
      }
    }
  }

  return true;
}

bool Decomposer::CreateDataLabels(const Labels& labels) {
  for (size_t i = 0; i < labels.size(); ++i) {
    BlockGraph::Block* block = image_->GetBlockByAddress(labels[i].first);
    if (block != NULL) {
      if (block->type() == BlockGraph::CODE_BLOCK) {
        LOG(ERROR) << "Encountered a zero-length data label in a code block.";
        return false;
      }
      BlockGraph::Offset offset = labels[i].first - block->addr();
      block->SetLabel(offset, labels[i].second.c_str());
    }
  }
  return true;
}

bool Decomposer::CreateDataBlock(IDiaSymbol* data, Labels* labels) {
  DCHECK(IsSymTag(data, SymTagData));

  DWORD location_type = LocIsNull;
  if (FAILED(data->get_locationType(&location_type))) {
    LOG(ERROR) << "Failed to retrieve data address type.";
    return false;
  }
  if (location_type != LocIsStatic) {
    DCHECK(location_type == LocIsNull || location_type == LocIsConstant);
    return true;
  }

  DWORD rva = 0;
  ScopedBstr name;
  if (FAILED(data->get_relativeVirtualAddress(&rva)) ||
      FAILED(data->get_name(name.Receive()))) {
    LOG(ERROR) << "Failed to retrieve data information";
    return false;
  }

  std::string data_name;
  if (!WideToUTF8(name, name.Length(), &data_name)) {
    LOG(ERROR) << "Failed to convert label name to UTF8";
    return false;
  }

  size_t length;
  if (!GetTypeSize(data, &length))
    return false;

  // We treat zero length data blocks as labels, and store them in an
  // intermediate data-structure. This is necessary because the block they
  // will eventually refer to may not yet exist.
  RelativeAddress addr(rva);
  if (length == 0) {
    labels->push_back(std::make_pair(addr, data_name));
    return true;
  }

  BlockGraph::Block* block = image_->GetBlockByAddress(addr);
  BlockGraph::Block* block_end = image_->GetBlockByAddress(addr + length - 1);
  if (block != block_end) {
    // TODO(chrisha): This was occurring for the
    //     IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG block that is created by the PE
    //     Parser. As it turns out, DIA reports the correct length for this
    //     block which for some reason the headers indicate as 8 bytes short.
    //     This has been addressed in PEFileParser::ParseImageHeader.
    //     If this occurs again, something else must be going on!
    LOG(ERROR) << "A DIA data symbol runs off the end of an existing block.";
    return false;
  }

  if (block == NULL) {
    block = CreateBlock(BlockGraph::DATA_BLOCK, addr,
        static_cast<BlockGraph::Size>(length), data_name.c_str());
  } else {
    switch (block->type()) {
      case BlockGraph::CODE_BLOCK:
        // TODO(chrisha): Mark part of the function as inline data.
        break;

      // Add a label to the block.
      case BlockGraph::DATA_BLOCK:
      case BlockGraph::READONLY_BLOCK: {
        BlockGraph::Offset offset = addr - block->addr();
        if (!block->HasLabel(offset))
          block->SetLabel(offset, data_name.c_str());
        break;
      }

      default:
        NOTREACHED() << "Invalid block type: " << block->type();
        break;
    }
  }

  if (block == NULL)
    return false;

  return true;
}

bool Decomposer::AddReference(RelativeAddress src_addr,
                              BlockGraph::ReferenceType type,
                              BlockGraph::Size size,
                              RelativeAddress dst_addr,
                              const char* name) {
  IntermediateReference ref = { type,
                                size,
                                dst_addr,
                                name == NULL ? "" : name };
  bool added = references_.insert(std::make_pair(src_addr, ref)).second;
  return added;
}

void Decomposer::AddReferenceCallback(RelativeAddress src_addr,
                                      BlockGraph::ReferenceType type,
                                      BlockGraph::Size size,
                                      RelativeAddress dst_addr,
                                      const char* name) {
  AddReference(src_addr, type, size, dst_addr, name);
}

bool Decomposer::CreateRelocationReferences() {
  PEFile::RelocSet reloc_set;
  if (!image_file_.DecodeRelocs(&reloc_set)) {
    LOG(ERROR) << "Unable to decode image relocs";
    return false;
  }

  PEFile::RelocMap reloc_map;
  if (!image_file_.ReadRelocs(reloc_set, &reloc_map)) {
    LOG(ERROR) << "Unable to read image relocs";
    return false;
  }

  PEFile::RelocMap::const_iterator it(reloc_map.begin());
  PEFile::RelocMap::const_iterator end(reloc_map.end());
  for (; it != end; ++it) {
    RelativeAddress src(it->first);
    RelativeAddress dst;
    if (!image_file_.Translate(it->second, &dst)) {
      LOG(ERROR) << "Unable to translate relocation destination";
      return false;
    }

    AddReference(src, BlockGraph::ABSOLUTE_REF, sizeof(dst), dst, "");
  }

  return true;
}

bool Decomposer::CreateCodeReferences() {
  // Queue all blocks for disassembly.
  BlockGraph::BlockMap::iterator it(image_->graph()->blocks_mutable().begin());
  BlockGraph::BlockMap::iterator end(image_->graph()->blocks_mutable().end());
  for (; it != end; ++it) {
    BlockGraph::Block* block = &it->second;
    if (block->type() == BlockGraph::CODE_BLOCK)
      to_disassemble_.insert(block);
  }

  scoped_ptr<Disassembler::InstructionCallback> on_instruction(
      NewCallback(this, &Decomposer::OnInstruction));

  // First we iterate all intermediate references and label all un-labeled
  // locations in functions we find referred.
  IntermediateReferenceMap::const_iterator ref_it(references_.begin());
  for (; ref_it != references_.end(); ++ref_it) {
    const RelativeAddress& src = ref_it->first;
    const IntermediateReference& ref = ref_it->second;
    BlockGraph::Block* src_block = image_->GetContainingBlock(src, 1);
    BlockGraph::Block* dst_block =
        image_->GetContainingBlock(ref.destination, 1);
    DCHECK(src_block != NULL);
    DCHECK(dst_block != NULL);

    if (src_block != dst_block && dst_block->type() == BlockGraph::CODE_BLOCK) {
      BlockGraph::Offset offset = ref.destination - dst_block->addr();
      if (!dst_block->HasLabel(offset)) {
        // If it had no label here, we add one.
        std::string label(base::StringPrintf("From 0x%08X",
                                             ref_it->first.value()));
        dst_block->SetLabel(offset, label.c_str());
      }
    }
  }

  // Disassemble all blocks, note that this process is potentially iterative,
  // as if disassembly turns up a PC-relative reference to another function
  // (block) at a location that didn't already have a label, it'll label that
  // location and re-queue the destination function for disassembly.
  DCHECK(to_merge_.empty());
  while (!to_disassemble_.empty()) {
    while (!to_disassemble_.empty()) {
      BlockSet::iterator it = to_disassemble_.begin();
      BlockGraph::Block* block = *it;
      to_disassemble_.erase(it);

      if (!CreateCodeReferencesForBlock(block, on_instruction.get())) {
        return false;
      }
    }

    DCHECK(to_disassemble_.empty());

    // Merge any ranges scheduled for merging, then re-schedule the
    // merged blocks for disassembly. Doing this outside the above loop
    // avoids orphaning scheduled blocks as we merge them together,
    // and is slightly more efficient as we may merge larger clusters
    // of blocks and avoid some disassembly/merging iterations.
    if (!to_merge_.empty()) {
      RangeSet::const_iterator it = to_merge_.begin();
      BlockGraph::AddressSpace::Range range(*it);
      to_merge_.erase(it);

      BlockGraph::Block* merged = image_->MergeIntersectingBlocks(range);
      DCHECK(merged != NULL);
      to_disassemble_.insert(merged);
    }
  }

  return true;
}

bool Decomposer::CreateCodeReferencesForBlock(
    BlockGraph::Block* block,
    Disassembler::InstructionCallback *on_instruction) {
  DCHECK(current_block_ == NULL);
  current_block_ = block;

  RelativeAddress block_addr;
  if (!image_->GetAddressOf(block, &block_addr)) {
    LOG(ERROR) << "Block " << block->name() << " has no address";
    return false;
  }

  AbsoluteAddress abs_block_addr;
  if (!image_file_.Translate(block_addr, &abs_block_addr)) {
    LOG(ERROR) << "Unable to get absolute address for " << block_addr;
    return false;
  }

  Disassembler disasm(block->data(),
                      block->data_size(),
                      abs_block_addr,
                      on_instruction);

  BlockGraph::Block::LabelMap::const_iterator it(block->labels().begin());
  for (; it != block->labels().end(); ++it) {
    BlockGraph::Offset label = it->first;

    DCHECK(label >= 0 && static_cast<size_t>(label) <= block->size());
    // Some labels are addressed at the end of the function, but we don't
    // want to disassemble from there.
    if (static_cast<size_t>(label) != block->size())
      disasm.Unvisited(abs_block_addr + it->first);
  }

  Disassembler::WalkResult result = disasm.Walk();

  DCHECK_EQ(block, current_block_);
  current_block_ = NULL;

  // TODO(siggi): Tally functions and instruction bytes.
  return (result == Disassembler::kWalkSuccess ||
      result == Disassembler::kWalkIncomplete);
}

void Decomposer::ScheduleForMerging(BlockGraph::Block* block1,
                                    BlockGraph::Block* block2) {
  RelativeAddress start(std::min(block1->addr(), block2->addr()));
  RelativeAddress end(std::max(block1->addr() + block1->size(),
                               block2->addr() + block2->size()));

  to_merge_.insert(BlockGraph::AddressSpace::Range(start, end - start));
}

BlockGraph::Block* Decomposer::CreateBlock(BlockGraph::BlockType type,
                                           RelativeAddress address,
                                           BlockGraph::Size size,
                                           const char* name) {
  BlockGraph::Block* block = image_->AddBlock(type, address, size, name);
  if (block == NULL) {
    LOG(ERROR) << "Unable to add block at " << address.value()
        << "(" << size << ")";
    return NULL;
  }

  const uint8* data = image_file_.GetImageData(address, size);
  if (data != NULL) {
    // TODO(siggi): test that the block is wholly out of the image data.
    block->set_data(data);
    block->set_data_size(size);
  }

  return block;
}

BlockGraph::Block* Decomposer::FindOrCreateBlock(BlockGraph::BlockType type,
                                                 RelativeAddress addr,
                                                 BlockGraph::Size size,
                                                 const char* name) {
  BlockGraph::Block* block = image_->GetBlockByAddress(addr);
  if (block != NULL) {
    RelativeAddress block_addr;
    if (!image_->GetAddressOf(block, &block_addr)) {
      LOG(ERROR) << "No address for block " << block->name();
      return NULL;
    }

    if (block_addr != addr || block->size() != size) {
      LOG(ERROR) << "Block collision for function at "
          << addr.value() << "(" << size << ") with " << block->name();
      return NULL;
    }

    return block;
  }
  DCHECK(block == NULL);

  return CreateBlock(type, addr, size, name);
}

void Decomposer::OnInstruction(const Disassembler& walker,
                               const _DInst& instruction,
                               Disassembler::CallbackDirective* directive) {
  int fc = META_GET_FC(instruction.meta);
  // For all branches, calls and conditional branches to PC-relative
  // addresses, record a PC-relative reference.
  if ((fc == FC_BRANCH || fc == FC_CALL || fc == FC_COND_BRANCH) &&
      instruction.ops[0].type == O_PC) {
    DCHECK_EQ(O_PC, instruction.ops[0].type);
    DCHECK_EQ(O_NONE, instruction.ops[1].type);
    DCHECK_EQ(O_NONE, instruction.ops[2].type);
    DCHECK_EQ(O_NONE, instruction.ops[3].type);
    DCHECK(instruction.ops[0].size == 8 ||
        instruction.ops[0].size == 16 ||
        instruction.ops[0].size == 32);
    // Distorm gives us size in bits, we want bytes.
    BlockGraph::Size size = instruction.ops[0].size / 8;

    // Get the reference's address. Note we assume it's in the instruction's
    // tail end - I don't know of a case where a PC-relative offset in a branch
    // or call is not the very last thing in an x86 instruction.
    AbsoluteAddress abs_src(
        static_cast<size_t>(instruction.addr) + instruction.size - size);
    AbsoluteAddress abs_dst(
        static_cast<size_t>(instruction.addr) + instruction.size +
            static_cast<size_t>(instruction.imm.addr));

    RelativeAddress src, dst;
    if (!image_file_.Translate(abs_src, &src) ||
        !image_file_.Translate(abs_dst, &dst)) {
      LOG(ERROR) << "Unable to translate absolute to relative addresses";
      *directive = Disassembler::kDirectiveTerminateWalk;
      return;
    }

    // Get the block associated with the destination address. It must exist
    // and be a code block.
    BlockGraph::Block* block = image_->GetContainingBlock(dst, 1);
    DCHECK(block != NULL && block->type() == BlockGraph::CODE_BLOCK);

    // If this is a call and the destination is a non-returning function,
    // then indicate that we should terminate this disassembly path.
    if (fc == FC_CALL &&
        (block->attributes() & BlockGraph::NON_RETURN_FUNCTION)) {
      // TODO(chrisha): For now, we enforce that the call be to the beginning
      //    of the function. This may not be necessary, but better safe than
      //    sorry for now.
      if (block->addr() != dst) {
        LOG(ERROR) << "Calling inside the body of non-returning function "
            << block->name();
        *directive = Disassembler::kDirectiveTerminateWalk;
        return;
      }
      *directive = Disassembler::kDirectiveTerminatePath;
    }

    // Add the reference. If it's new, make sure to try and add a label
    // and reschedule the block for disassembly again.
    bool added = AddReference(src, BlockGraph::PC_RELATIVE_REF, size, dst, "");
    if (added) {
      // No special action if the reference is to the current block,
      // we're already covered by the disassembly process.
      if (block != current_block_) {
        // See whether the block had a label at the offset.
        BlockGraph::Offset offset = dst - block->addr();
        if (!block->HasLabel(offset)) {
          // If it had no label here, we add one.
          std::string label(base::StringPrintf("From 0x%08X", src.value()));

          block->SetLabel(offset, label.c_str());

          // And then potentially re-schedule the block for disassembly,
          // as we may have turned up another entry to a block we already
          // disassembled.
          to_disassemble_.insert(block);
        }

        // For short references across blocks, we want to make sure we merge
        // the two blocks. AFAICT, this only occurs in hand-coded assembly in
        // the CRT, and the "functions" involved are not independent.
        if (size != sizeof(RelativeAddress))
          ScheduleForMerging(current_block_, block);
      }
    }
  }

  // We want to find function blocks where control flow runs off the end
  // of the function into the immediately adjoining block, and schedule
  // the two for merging. AFAICT, this again only occurs in hand-crafted
  // assembly in the CRT.
  if (fc != FC_RET && fc != FC_BRANCH && fc != FC_INT) {
    AbsoluteAddress instruction_start_abs(
        static_cast<size_t>(instruction.addr));
    RelativeAddress instruction_start;
    if (!image_file_.Translate(instruction_start_abs, &instruction_start)) {
      LOG(ERROR) << "Unable to translate absolute to relative addresses";
      *directive = Disassembler::kDirectiveTerminateWalk;
      return;
    }

    RelativeAddress instruction_end(instruction_start + instruction.size);
    RelativeAddress block_end(current_block_->addr() + current_block_->size());
    if  (instruction_end == block_end) {
      // Find the following block.
      BlockGraph::Block* next_block =
          image_->GetFirstItersectingBlock(block_end, 1);
      DCHECK(next_block != NULL);

      // And schedule the two for merging.
      ScheduleForMerging(current_block_, next_block);
    }
  }

  if (fc == FC_CALL) {
    // TODO(chrisha): For call instructions, see whether they call a
    //     non-returning function. Instruct the disassembler not to continue
    //     disassembly past the instruction in that case.
    //     The case where the address is PC-relative is handled in the above
    //     code. However, the called function could also be at an
    //     indirect absolute address when invoking imported symbols. We do not
    //     currently have meta-data regarding these symbols, so do not know if
    //     they are non-returning.
  }
}

bool Decomposer::CreatePEImageBlocksAndReferences(
    PEFileParser::PEHeader* header) {
  scoped_ptr<PEFileParser::AddReferenceCallback> add_reference(
      NewCallback(this, &Decomposer::AddReferenceCallback));
  PEFileParser parser(image_file_, image_, add_reference.get());

  if (!parser.ParseImageHeader(header)) {
    LOG(ERROR) << "Unable to parse image header";
    return false;
  }

  if (!parser.ParseExportDirectory(
      header->data_directory[IMAGE_DIRECTORY_ENTRY_EXPORT])) {
    LOG(ERROR) << "Unable to parse export directory";
    return false;
  }

  if (!parser.ParseLoadConfig(
      header->data_directory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG])) {
    LOG(ERROR) << "Unable to parse load config";
    return false;
  }

  if (!parser.ParseTlsDirectory(
      header->data_directory[IMAGE_DIRECTORY_ENTRY_TLS])) {
    LOG(ERROR) << "Unable to parse tls directory";
    return false;
  }

  if (!parser.ParseDebugDirectory(
      header->data_directory[IMAGE_DIRECTORY_ENTRY_DEBUG])) {
    LOG(ERROR) << "Unable to parse debug directory";
    return false;
  }

  if (!parser.ParseResourceDirectory(
      header->data_directory[IMAGE_DIRECTORY_ENTRY_RESOURCE])) {
    LOG(ERROR) << "Unable to parse resource directory";
    return false;
  }

  return true;
}

bool Decomposer::FinalizeIntermediateReferences() {
  IntermediateReferenceMap::const_iterator it(references_.begin());
  IntermediateReferenceMap::const_iterator end(references_.end());

  for (; it != end; ++it) {
    RelativeAddress src_addr(it->first);
    BlockGraph::Block* src = image_->GetBlockByAddress(src_addr);
    RelativeAddress dst_addr(it->second.destination);
    BlockGraph::Block* dst = image_->GetBlockByAddress(dst_addr);

    if (src == NULL || dst == NULL) {
      LOG(ERROR) << "Reference source or destination address is out of "
          << "range, src: " << src << ", dst: " << dst;
      return false;
    }

    RelativeAddress src_start;
    RelativeAddress dst_start;
    if (!image_->GetAddressOf(src, &src_start) ||
        !image_->GetAddressOf(dst, &dst_start)) {
      LOG(ERROR) << "No address for src or dst block";
      return false;
    }

    BlockGraph::Reference ref(it->second.type,
                              it->second.size,
                              dst,
                              dst_addr - dst_start);
    src->SetReference(src_addr - src_start, ref);
  }

  return true;
}

bool Decomposer::LoadOmapInformation(IDiaSession* dia_session,
                                     std::vector<OMAP>* omap_to,
                                     std::vector<OMAP>* omap_from) {
  DCHECK(dia_session != NULL);
  DCHECK(omap_to != NULL);
  DCHECK(omap_from != NULL);

  omap_to->clear();
  omap_from->clear();

  ScopedComPtr<IDiaEnumDebugStreams> debug_streams;
  if (FAILED(dia_session->getEnumDebugStreams(debug_streams.Receive()))) {
    LOG(ERROR) << "Unable to get debug streams.";
    return false;
  }

  while (true) {
    ScopedComPtr<IDiaEnumDebugStreamData> debug_stream;
    ULONG count = 0;
    HRESULT hr = debug_streams->Next(1, debug_stream.Receive(), &count);
    if (FAILED(hr) || (hr != S_FALSE && count != 1)) {
      LOG(ERROR) << "Unable to load debug stream: " << hr;
      return false;
    } else if (hr == S_FALSE) {
      // No more records.
      break;
    }

    ScopedBstr name;
    if (FAILED(debug_stream->get_name(name.Receive())) || name == NULL) {
      LOG(ERROR) << "Unable to get debug stream name.";
      return false;
    }

    if (wcscmp(name, L"OMAPTO") == 0 &&
        !LoadOmapStream(debug_stream, omap_to)) {
      LOG(ERROR) << "Unable to load omap to stream.";
      return false;
    } else if (wcscmp(name, L"OMAPFROM") == 0 &&
        !LoadOmapStream(debug_stream, omap_from)) {
      LOG(ERROR) << "Unable to load omap from stream.";
      return false;
    }
  }

  return true;
}

bool Decomposer::LoadOmapStream(IDiaEnumDebugStreamData* omap_stream,
                                std::vector<OMAP>* omap_list) {
  DCHECK(omap_stream != NULL);
  DCHECK(omap_list != NULL);

  LONG count = 0;
  if (FAILED(omap_stream->get_Count(&count))) {
    LOG(ERROR) << "Failed to get stream count.";
    return false;
  }

  omap_list->resize(count);

  DWORD bytes_read = 0;
  ULONG count_read = 0;
  if (FAILED(omap_stream->Next(count, count * sizeof(OMAP), &bytes_read,
                               reinterpret_cast<BYTE*>(&omap_list->at(0)),
                               &count_read))) {
    LOG(ERROR) << "Unable to read omap stream.";
    return false;
  }
  DCHECK_EQ(count * sizeof(OMAP), bytes_read);
  DCHECK_EQ(count, static_cast<LONG>(count_read));

  return true;
}

}  // namespace pe
