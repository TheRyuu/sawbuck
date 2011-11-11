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

#include "syzygy/instrument/instrumenter.h"

#include <string>

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "syzygy/common/align.h"
#include "syzygy/common/defs.h"
#include "syzygy/common/syzygy_version.h"
#include "syzygy/core/serialization.h"
#include "syzygy/core/typed_block.h"
#include "syzygy/pe/pe_file_writer.h"
#include "syzygy/pe/decomposer.h"
#include "syzygy/pe/metadata.h"

namespace instrument {

using core::AbsoluteAddress;
using core::RelativeAddress;
using pe::Decomposer;
using pe::PEFileWriter;

namespace {

const char* const kEntryHookTable[] = {
    "_indirect_penter",
    "_indirect_penter_dllmain",
};

const size_t kEntryHookTableSize = ARRAYSIZE(kEntryHookTable);
const size_t kIndirectPenterOffset = 0;
const size_t kIndirectPenterDllMainOffset = sizeof(IMAGE_THUNK_DATA);

size_t WordAlign(size_t value) {
  return common::AlignUp(value, sizeof(WORD));
}

}  // namespace

const char* const Instrumenter::kCallTraceClientDllEtw = "call_trace.dll";
const char* const Instrumenter::kCallTraceClientDllRpc =
    "call_trace_client.dll";

Instrumenter::Instrumenter()
    : client_dll_(kCallTraceClientDllEtw),
      image_import_by_name_block_(NULL),
      hint_name_array_block_(NULL),
      import_address_table_block_(NULL),
      dll_name_block_(NULL),
      image_import_descriptor_array_block_(NULL),
      thunk_suffix_("_thunk") {
}

void Instrumenter::set_client_dll(const char* const client_dll) {
  DCHECK(client_dll != NULL);
  DCHECK(client_dll[0] != '\0');
  client_dll_ = client_dll;
}

bool Instrumenter::Instrument(const FilePath& input_dll_path,
                              const FilePath& input_pdb_path,
                              const FilePath& output_dll_path,
                              const FilePath& output_pdb_path) {
  DCHECK(!input_dll_path.empty());
  DCHECK(!output_dll_path.empty());

  // Read and decompose the input image for starters.
  LOG(INFO) << "Parsing input image PE headers.";
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

  // Construct and initialize our instrumenter.
  if (!Initialize(image_layout, &block_graph)) {
    LOG(ERROR) << "Unable to initialize instrumenter.";
    return false;
  }

  // Copy the sections, except for .rsrc and .relocs.
  LOG(INFO) << "Copying sections.";
  if (!CopySections()) {
    LOG(ERROR) << "Unable to copy sections.";
    return false;
  }

  // Instrument the binary. This creates .import and .thunks sections.
  LOG(INFO) << "Adding call trace import descriptor.";

  // Retrieve the import directory entry block.
  BlockGraph::Reference import_entry;
  size_t import_entry_size = 0;
  if (!builder().GetDataDirectoryEntry(IMAGE_DIRECTORY_ENTRY_IMPORT,
                                       &import_entry,
                                       &import_entry_size) ||
      import_entry.offset() != 0) {
    LOG(ERROR) << "Missing or invalid import table.";
    return false;
  }

  if (!AddCallTraceImportDescriptor(import_entry.referenced())) {
    LOG(ERROR) << "Unable to add call trace import.";
    return false;
  }

  LOG(INFO) << "Instrumenting code blocks.";
  if (!InstrumentCodeBlocks(&block_graph)) {
    LOG(ERROR) << "Unable to instrument code blocks.";
    return false;
  }

  // Update the debug directory to point to the new PDB file.
  if (!UpdateDebugInformation(output_pdb_path)) {
    LOG(ERROR) << "Unable to update debug information.";
    return false;
  }

  // Write metadata section.
  if (!WriteMetadataSection(input_dll))
    return false;

  // Copies the resource section, if there is one.
  if (!CopyResourceSection())
    return false;

  // Update the data directory import entry to refer to our newly created
  // section.
  if (!builder().SetDataDirectoryEntry(IMAGE_DIRECTORY_ENTRY_IMPORT,
                                       image_import_descriptor_array_block_)) {
    LOG(ERROR) << "Unable to set data directory entry.";
    return false;
  }

  // Finalize the headers and write the image.
  LOG(INFO) << "Finalizing headers.";
  if (!FinalizeImageHeaders()) {
    LOG(ERROR) << "Unable to finalize image headers.";
    return false;
  }

  LOG(INFO) << "Writing the image.";
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

bool Instrumenter::CopySections() {
  // Copy the sections from the decomposed image to the new one, save for the
  // .relocs section. If there is a .rsrc section, does not copy it but stores
  // its index in resource_section_id_.
  for (size_t i = 0; i < original_sections().size() - 1; ++i) {
    const ImageLayout::SectionInfo& section = original_sections()[i];

    // Skip the resource section.
    if (i == resource_section_id())
      continue;

    LOG(INFO) << "Copying section " << i << " (" << section.name << ").";
    if (!CopySection(section)) {
      LOG(ERROR) << "Unable to copy section.";
      return false;
    }
  }

  return true;
}

bool Instrumenter::AddCallTraceImportDescriptor(
    const BlockGraph::Block* original_image_import_descriptor_array) {
  DCHECK(original_image_import_descriptor_array != NULL);

  RelativeAddress start = builder().next_section_address();
  RelativeAddress insert_at = start;

  // Create the image import by name block.
  if (!CreateImageImportByNameBlock(&insert_at)) {
    LOG(ERROR) << "Unable to create image import by name block";
    return false;
  }

  // Create the hint name array and import address table blocks.
  if (!CreateImportAddressTableBlocks(&insert_at)) {
    LOG(ERROR) << "Unable to create import address table block";
    return false;
  }

  // Create the DLL name block with room for a null character.
  if (!CreateDllNameBlock(&insert_at)) {
    LOG(ERROR) << "Unable to create dll name block";
    return false;
  }

  // Align the import descriptor array block to a DWORD boundary.
  insert_at.set_value(common::AlignUp(insert_at.value(), sizeof(DWORD)));

  // Create the image import descript array block.
  if (!CreateImageImportDescriptorArrayBlock(
      original_image_import_descriptor_array, &insert_at)) {
    LOG(ERROR) << "Unable to create image import descriptor array block";
    return false;
  }

  // Wrap the above blocks in a new section.
  uint32 import_dir_size = insert_at - start;
  uint32 flags = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE |
                 IMAGE_SCN_CNT_INITIALIZED_DATA;
  RelativeAddress real_start = builder().AddSection(
      ".import", import_dir_size, import_dir_size, flags);

  DCHECK_EQ(start, real_start);

  return true;
}

bool Instrumenter::InstrumentCodeBlocks(BlockGraph* block_graph) {
  DCHECK(block_graph != NULL);

  RelativeAddress start = builder().next_section_address();
  RelativeAddress insert_at = start;

  // The block map needs to be copied because it will change while we create
  // new thunks. However, pointers to the original blocks are needed, so copy
  // the block pointers into a vector. Also, we only need to instrument code
  // blocks, so filter non code blocks out here.
  std::vector<BlockGraph::Block*> block_list;
  BlockGraph::BlockMap::iterator block_it(
      block_graph->blocks_mutable().begin());
  for (; block_it != block_graph->blocks_mutable().end(); ++block_it) {
    if (block_it->second.type() == BlockGraph::CODE_BLOCK) {
      block_list.push_back(&block_it->second);
    }
  }

  // Iterate through all the code blocks in the decomposed image's block graph.
  for (uint32 i = 0; i < block_list.size(); ++i) {
    BlockGraph::Block* block = block_list[i];
    if (!CreateThunks(block, &insert_at)) {
      LOG(ERROR) << "Unable to create thunks for block";
      return false;
    }
  }

  // Reset the entry-point thunks (as needed).
  if (!FixEntryPointThunk()) {
    LOG(ERROR) << "Failed to patch DLL entry point thunk";
    return false;
  }

  // Reset the TLS static initializers (as needed).
  if (!FixTlsInitializerThunks()) {
    LOG(ERROR) << "Faied to patch DLL TLS Initializer thunks.";
    return false;
  }

  // Wrap the thunks in a new section.
  uint32 thunks_size = insert_at - start;
  builder().AddSection(".thunks",
                       thunks_size,
                       thunks_size,
                       IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_READ |
                       IMAGE_SCN_MEM_EXECUTE);

  return true;
}

bool Instrumenter::CreateImageImportByNameBlock(
    RelativeAddress* insert_at) {
  DCHECK(image_import_by_name_block_ == NULL);

  // Figure out how large the block needs to be to hold all the names of the
  // hooks we export.  The IMAGE_IMPORT_BY_NAME struct has a WORD ordinal and
  // a variable sized field for the null-terminated function name. Each entry
  // should be WORD aligned, and will be referenced from the import table.
  size_t total_size = 0;
  for (int i = 0; i < kEntryHookTableSize; ++i) {
    total_size += sizeof(WORD) + WordAlign(strlen(kEntryHookTable[i]) + 1);
  }

  // Allocate the block.
  BlockGraph::Block* block =
      builder().image_layout().blocks.AddBlock(BlockGraph::DATA_BLOCK,
                                               *insert_at,
                                               total_size,
                                               "image_import_by_name");
  if (block == NULL) {
    LOG(ERROR) << "Unable to allocate image import by name block";
    return false;
  }

  uint8* raw_data = block->AllocateData(total_size);
  if (raw_data == NULL) {
    LOG(ERROR) << "Unable to allocate image import by name block data";
    return false;
  }

  *insert_at += block->size();

  // Populate the block with IMAGE_IMPORT_BY_NAME records.
  size_t offset = 0;
  for (int i = 0; i < kEntryHookTableSize; ++i) {
    size_t size = strlen(kEntryHookTable[i]) + 1;
    IMAGE_IMPORT_BY_NAME* image_import_by_name =
        reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(raw_data + offset);
    image_import_by_name->Hint = 0;
    base::strlcpy(reinterpret_cast<char*>(&image_import_by_name->Name[0]),
                  kEntryHookTable[i],
                  size);

    offset += sizeof(WORD) + WordAlign(size);
  }

  image_import_by_name_block_ = block;
  return true;
}

bool Instrumenter::CreateImportAddressTableBlocks(RelativeAddress* insert_at) {
  DCHECK(insert_at != NULL);
  DCHECK(image_import_by_name_block_ != NULL);
  DCHECK(hint_name_array_block_ == NULL);
  DCHECK(import_address_table_block_ == NULL);

  if (!CreateImportAddressTableBlock("hint_name_array", insert_at,
                                     &hint_name_array_block_)) {
     return false;
  }

  if (!CreateImportAddressTableBlock("import_address_table", insert_at,
                                     &import_address_table_block_)) {
     return false;
  }

  return true;
}

bool Instrumenter::CreateImportAddressTableBlock(const char* name,
                                                 RelativeAddress* insert_at,
                                                 BlockGraph::Block** block) {
  DCHECK(insert_at != NULL);
  DCHECK(block != NULL);
  DCHECK(*block == NULL);
  DCHECK(name != NULL);
  DCHECK(image_import_by_name_block_ != NULL);

  // The hint name array and import address table are identical null-terminated
  // arrays of IMAGE_THUNK_DATA. Each IMAGE_THUNK_DATA entry points to an
  // IMAGE_IMPORT_BY_NAME entry in the image import by name array.

  const size_t kImageThunkDataSize =
      sizeof(IMAGE_THUNK_DATA) * (kEntryHookTableSize + 1);

  // Allocate the new block.
  BlockGraph::Block* new_block =
      builder().image_layout().blocks.AddBlock(BlockGraph::DATA_BLOCK,
                                               *insert_at,
                                               kImageThunkDataSize,
                                               name);
  if (new_block == NULL) {
    LOG(ERROR) << "Unable to allocate " << name << " block.";
    return false;
  }

  // Allocate the the memory for the new block. It will already be zero-
  // initialized, which takes care of null-terminating the table.
  uint8* raw_data = new_block->AllocateData(new_block->size());
  if (raw_data == NULL) {
    LOG(ERROR) << "Unable to allocate " << name << " block data.";
    return false;
  }

  // Create references to each of the defined hooks.
  size_t offset = 0;
  for (int hook_index = 0; hook_index < kEntryHookTableSize; ++hook_index) {
    // Create a reference to the hook's offset.
    BlockGraph::Reference hook_ref(BlockGraph::RELATIVE_REF,
                                   sizeof(RelativeAddress),
                                   image_import_by_name_block_,
                                   offset);
    new_block->SetReference(hook_index * sizeof(IMAGE_THUNK_DATA), hook_ref);
    offset += (sizeof(WORD) +
               WordAlign(strlen(kEntryHookTable[hook_index]) + 1));
  }

  // Advance the block insertion address.
  *insert_at += new_block->size();

  // Update the instrumenter's reference to this block.
  *block = new_block;

  return true;
}

bool Instrumenter::CreateDllNameBlock(RelativeAddress* insert_at) {
  DCHECK(dll_name_block_ == NULL);

  // Create the DLL name block with room for a null character.
  BlockGraph::Block* block =
      builder().image_layout().blocks.AddBlock(BlockGraph::DATA_BLOCK,
                                               *insert_at,
                                               client_dll_.length() + 1,
                                               "client_dll_name");
  if (block == NULL) {
    LOG(ERROR) << "Unable to allocate client dll name block.";
    return false;
  }
  *insert_at += block->size();

  uint8* raw_data = block->AllocateData(block->size());
  if (raw_data == NULL) {
    LOG(ERROR) << "Unable to allocate client dll name data.";
    return false;
  }

  base::strlcpy(
      reinterpret_cast<char*>(raw_data), client_dll_.c_str(), block->size());

  dll_name_block_ = block;
  return true;
}

bool Instrumenter::CreateImageImportDescriptorArrayBlock(
    const BlockGraph::Block* original_image_import_descriptor_array,
    RelativeAddress* insert_at) {
  DCHECK(original_image_import_descriptor_array != NULL);
  DCHECK(hint_name_array_block_ != NULL);
  DCHECK(import_address_table_block_ != NULL);
  DCHECK(dll_name_block_ != NULL);
  DCHECK(image_import_descriptor_array_block_ == NULL);

  // The image import descriptor array is an array of IMAGE_IMPORT_DESCRIPTOR
  // structs where the last struct is zeroed-out (i.e. the array length is one
  // more than the actual number of imports). The OriginalFirstThunk member
  // points to a hint name array, the Name member points to the DLL name, and
  // the FirstThunk member points to an import address table.
  // Note: The PE Parser truncates the original_image_import_descriptor_array
  //       size to the first DWORD of the sentinel (i.e., loses about 16 bytes)
  //       So we need to make sure we re-expand/align the array.
  size_t original_block_size = common::AlignUp(
     original_image_import_descriptor_array->size(),
     sizeof(IMAGE_IMPORT_DESCRIPTOR));
  size_t block_size = original_block_size + sizeof(IMAGE_IMPORT_DESCRIPTOR);
  BlockGraph::Block* block =
      builder().image_layout().blocks.AddBlock(BlockGraph::DATA_BLOCK,
                                               *insert_at,
                                               block_size,
                                               "image_import_descriptor");
  if (block == NULL) {
    LOG(ERROR) << "Unable to allocate image import descriptor array block";
    return false;
  }
  *insert_at += block->size();

  uint8* data = block->AllocateData(block_size);
  if (data == NULL) {
    LOG(ERROR) << "Unable to allocate image import descriptor array block "
                  "data";
    return false;
  }

  // Copy IMAGE_IMPORT_DESCRIPTOR data from the old one to the new one.
  // TODO(ericdingle): This doesn't copy the references from the old block to
  // the new block (i.e. it is dependent on the fact that the original import
  // table is written into the exact same address space in the new image).
  size_t original_data_size =
      original_block_size - sizeof(IMAGE_IMPORT_DESCRIPTOR);
  memcpy(data, original_image_import_descriptor_array->data(),
         original_data_size);
  // And zero out the rest.
  memset(data + original_data_size, 0, block_size - original_data_size);

  // For the new IMAGE_IMPORT_DESCRIPTOR, add references to the hint name
  // array, the import address table and the dll name.
  size_t offset = original_data_size;
  block->SetReference(
      offset + offsetof(IMAGE_IMPORT_DESCRIPTOR, OriginalFirstThunk),
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            sizeof(RelativeAddress),
                            hint_name_array_block_,
                            0));
  block->SetReference(
      offset + offsetof(IMAGE_IMPORT_DESCRIPTOR, FirstThunk),
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            sizeof(RelativeAddress),
                            import_address_table_block_,
                            0));
  block->SetReference(
      offset + offsetof(IMAGE_IMPORT_DESCRIPTOR, Name),
      BlockGraph::Reference(BlockGraph::RELATIVE_REF,
                            sizeof(RelativeAddress),
                            dll_name_block_,
                            0));

  image_import_descriptor_array_block_ = block;
  return true;
}

bool Instrumenter::CreateThunks(BlockGraph::Block* block,
                                RelativeAddress* insert_at) {
  // Typedef for the thunk block map. The key is the offset within the callee
  // block and the value is the thunk block that forwards to the callee at that
  // offset.
  typedef std::map<BlockGraph::Offset, BlockGraph::Block*> ThunkBlockMap;
  ThunkBlockMap thunk_block_map;

  // Iterate through all the block's referrers, creating thunks as we go.
  BlockGraph::Block::ReferrerSet referrers = block->referrers();
  BlockGraph::Block::ReferrerSet::const_iterator referrer_it(referrers.begin());
  for (; referrer_it != referrers.end(); ++referrer_it) {
    const BlockGraph::Block::Referrer& referrer = *referrer_it;

    // Skip self-references.
    if (referrer.first == block)
      continue;

    // Get the reference.
    BlockGraph::Reference ref;
    if (!referrer.first->GetReference(referrer.second, &ref)) {
      LOG(ERROR) << "Unable to get reference from referrer";
      return false;
    }

    // Look for the reference in the thunk block map, and only create a new one
    // if it does not already exist.
    BlockGraph::Block* thunk_block = NULL;
    ThunkBlockMap::const_iterator thunk_it = thunk_block_map.find(ref.offset());
    if (thunk_it == thunk_block_map.end()) {
      if (!CreateOneThunk(block, ref, insert_at, &thunk_block)) {
        LOG(ERROR) << "Unable to create thunk block";
        return false;
      }
      thunk_block_map[ref.offset()] = thunk_block;
    } else {
      thunk_block = thunk_it->second;
    }
    DCHECK(thunk_block != NULL);

    // Update the referrer to point to the thunk.
    BlockGraph::Reference new_ref(ref.type(),
                                  ref.size(),
                                  thunk_block,
                                  0);
    referrer.first->SetReference(referrer.second, new_ref);
  }

  return true;
}

bool Instrumenter::CreateOneThunk(BlockGraph::Block* block,
                                  const BlockGraph::Reference& ref,
                                  RelativeAddress* insert_at,
                                  BlockGraph::Block** thunk_block) {
  DCHECK(import_address_table_block_ != NULL);
  DCHECK(block != NULL);
  DCHECK(insert_at != NULL);
  DCHECK(thunk_block != NULL);

  // We push the absolute address of the function to be called on the
  // stack, and then we invoke the _indirect_penter function.
  // 6844332211    push  offset (11223344)
  // FF2588776655  jmp   dword ptr [(55667788)]
  static const Thunk kThunk = {
    0x68,
    NULL,
    0x25FF,
    NULL
  };

  // Create the new thunk block, and set its data.
  std::string name = std::string(block->name()) + thunk_suffix_;
  BlockGraph::Block* new_block =
      builder().image_layout().blocks.AddBlock(BlockGraph::CODE_BLOCK,
                                               *insert_at,
                                               sizeof(Thunk),
                                               name.c_str());
  if (new_block == NULL) {
    LOG(ERROR) << "Unable to allocate thunk block.";
    return false;
  }
  *insert_at += new_block->size();
  new_block->SetData(reinterpret_cast<const uint8*>(&kThunk),
                     new_block->size());

  // Set an absolute reference to the original block at the given offset.
  new_block->SetReference(
      offsetof(Thunk, func_addr),
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF,
                            sizeof(AbsoluteAddress),
                            block,
                            ref.offset()));

  // Set an absolute reference to the correct instrumentation hook in the call
  // trace client dll import table. This corresponds to the hook_index'th
  // IMAGE_THUNK_DATA entry in the import_address_table_block_.
  new_block->SetReference(
      offsetof(Thunk, hook_addr),
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF,
                            sizeof(RelativeAddress),
                            import_address_table_block_,
                            kIndirectPenterOffset));

  *thunk_block = new_block;
  return true;
}

bool Instrumenter::FixEntryPointThunk() {
  core::TypedBlock<IMAGE_NT_HEADERS> nt_headers;
  if (!nt_headers.Init(0, builder().nt_headers_block())) {
    LOG(ERROR) << "Failed to retrieve NT Headers.";
    return false;
  }

  // If the module is NOT a DLL then there's nothing to do.
  if ((nt_headers->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0) {
    return true;
  }

  core::TypedBlock<Thunk> thunk;
  if (!nt_headers.Dereference(nt_headers->OptionalHeader.AddressOfEntryPoint,
                              &thunk)) {
    LOG(ERROR) << "Failed to resolve entry point thunk.";
    return false;
  }

  DCHECK(thunk.offset() == 0);

  return RedirectThunk(thunk.block());
}

bool Instrumenter::FixTlsInitializerThunks() {
  core::TypedBlock<IMAGE_NT_HEADERS> nt_headers;
  if (!nt_headers.Init(0, builder().nt_headers_block())) {
    LOG(ERROR) << "Failed to retrieve NT Headers.";
    return false;
  }

  // If the module is NOT a DLL then there's nothing to do.
  if ((nt_headers->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0) {
    return true;
  }

  // If the module has no TLS directory then there are no TLS initializers
  // and hence nothing to do.
  const IMAGE_DATA_DIRECTORY& data_dir =
      nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
  if (data_dir.Size == 0 || data_dir.VirtualAddress == 0) {
    return true;
  }

  // Find the TLS directory.
  core::TypedBlock<IMAGE_TLS_DIRECTORY> tls_dir;
  if (!nt_headers.Dereference(data_dir.VirtualAddress, &tls_dir)) {
    LOG(ERROR) << "Failed to locate TLS directory.";
    return false;
  }

  // Get the TLS initializer callbacks.
  core::TypedBlock<DWORD> callbacks;
  if (!tls_dir.Dereference(tls_dir->AddressOfCallBacks, &callbacks)) {
    LOG(ERROR) << "Failed to locate TLS directory.";
    return false;
  }

  // Redirect each of the thunks referenced by the callbacks block.
  typedef BlockGraph::Block::ReferenceMap ReferenceMap;
  const ReferenceMap& ref_map = callbacks.block()->references();
  ReferenceMap::const_iterator iter = ref_map.begin();
  for (; iter != ref_map.end(); ++iter) {
    DCHECK(iter->second.offset() == 0);
    core::TypedBlock<Thunk> thunk;
    if (!thunk.Init(0, iter->second.referenced())) {
      LOG(ERROR) << "Failed to locate TLS initializer thunk.";
      return false;
    }

    if (!RedirectThunk(thunk.block())) {
      LOG(ERROR) << "Failed to redirect tls initializer thunk: "
                 << thunk.block()->name() << ".";
      return false;
    }
  }

  return true;
}

bool Instrumenter::RedirectThunk(BlockGraph::Block* thunk_block) {
  DCHECK(thunk_block != NULL);
  DCHECK(thunk_block->size() == sizeof(Thunk));
  DCHECK(std::equal(thunk_suffix_.rbegin(),
                    thunk_suffix_.rend(),
                    std::string(thunk_block->name()).rbegin()));
#ifndef NDEBUG
  BlockGraph::Reference ref;
  DCHECK(thunk_block->GetReference(offsetof(Thunk, hook_addr), &ref));
  DCHECK(ref.referenced() == import_address_table_block_);
  DCHECK(ref.offset() == kIndirectPenterOffset);
#endif

  bool inserted = thunk_block->SetReference(
      offsetof(Thunk, hook_addr),
      BlockGraph::Reference(BlockGraph::ABSOLUTE_REF,
                            sizeof(RelativeAddress),
                            import_address_table_block_,
                            kIndirectPenterDllMainOffset));

 DCHECK(inserted == false);

 return true;
}

}  // namespace instrument
