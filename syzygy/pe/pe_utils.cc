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

#include "base/string_util.h"
#include "syzygy/block_graph/typed_block.h"
#include "syzygy/pe/dos_stub.h"

namespace pe {

using block_graph::BlockGraph;
using block_graph::ConstTypedBlock;
using block_graph::TypedBlock;
using core::RelativeAddress;

namespace {

// A simple struct that can be used to let us access strings using TypedBlock.
struct StringStruct {
  const char string[1];
};

typedef TypedBlock<IMAGE_DOS_HEADER> DosHeader;
typedef TypedBlock<IMAGE_IMPORT_DESCRIPTOR> ImageImportDescriptor;
typedef TypedBlock<IMAGE_NT_HEADERS> NtHeaders;
typedef TypedBlock<StringStruct> String;

template <typename BlockPtr>
BlockPtr UncheckedGetNtHeadersBlockFromDosHeaderBlock(
    BlockPtr dos_header_block) {
  BlockGraph::Reference ref;
  if (!dos_header_block->GetReference(offsetof(IMAGE_DOS_HEADER, e_lfanew),
                                      &ref)) {
    // No NT headers reference.
    return NULL;
  }

  if (ref.offset() != 0 ||
      ref.type() != BlockGraph::RELATIVE_REF ||
      ref.size() != sizeof(RelativeAddress)) {
    // The reference is of incorrect type.
    return NULL;
  }

  return ref.referenced();
}

template <typename BlockPtr>
BlockPtr CheckedGetNtHeadersBlockFromDosHeaderBlock(
    BlockPtr dos_header_block) {
  DCHECK(IsValidDosHeaderBlock(dos_header_block));

  BlockPtr nt_headers_block =
      UncheckedGetNtHeadersBlockFromDosHeaderBlock(dos_header_block);
  if (nt_headers_block == NULL ||
      !IsValidNtHeadersBlock(nt_headers_block)) {
    return NULL;
  }

  return nt_headers_block;
}

}  // namespace

const char kCodeSectionName[] = ".text";
const char kReadOnlyDataSectionName[] = ".rdata";
const char kReadWriteDataSectionName[] = ".data";
const char kRelocSectionName[] = ".reloc";
const char kResourceSectionName[] = ".rsrc";
const char kTlsSectionName[] = ".tls";

const DWORD kCodeCharacteristics =
    IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
const DWORD kReadOnlyDataCharacteristics =
    IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
const DWORD kReadWriteDataCharacteristics =
    IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
const DWORD kRelocCharacteristics =
    IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_DISCARDABLE |
    IMAGE_SCN_MEM_READ;

bool IsValidDosHeaderBlock(const BlockGraph::Block* dos_header_block) {
  ConstTypedBlock<IMAGE_DOS_HEADER> dos_header;

  if (!dos_header.Init(0, dos_header_block)) {
    // Too small or no data.
    return false;
  }

  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    // Wrong signature.
    return false;
  }

  // The "DOS file size" is encoded in a rather wonky manner.
  // - e_cp is the number of "pages" in the file, but
  // - e_cblp is the number of bytes used on the last page.
  size_t dos_file_size = 512 * dos_header->e_cp;
  if (dos_header->e_cblp != 0) {
    // Let's not go below zero size.
    if (dos_file_size < 512)
      return false;

    dos_file_size -= 512;
    dos_file_size += dos_header->e_cblp;
  }
  // The VC linker yields a DOS header with a size that's larger than
  // the DOS header and the NT headers combined. I wonder if anyone cares
  // about these sizes anymore.
  if (dos_file_size < dos_header_block->size())
    return false;

  // Check the paragraph size of the header.
  if (dos_header->e_cparhdr * 16 < sizeof(IMAGE_DOS_HEADER))
    return false;

  // Retrieve the NT headers.
  const BlockGraph::Block* nt_headers =
      UncheckedGetNtHeadersBlockFromDosHeaderBlock(dos_header_block);
  if (nt_headers == NULL) {
    // No DOS header reference.
    return false;
  }

  return true;
}

bool IsValidNtHeadersBlock(const BlockGraph::Block* nt_headers_block) {
  // Check the signatures.
  ConstTypedBlock<IMAGE_NT_HEADERS> nt_headers;

  if (!nt_headers.Init(0, nt_headers_block)) {
    // Short or no data.
    return false;
  }

  if (nt_headers->Signature!= IMAGE_NT_SIGNATURE) {
    // Wrong signature.
    return false;
  }
  if (nt_headers->FileHeader.SizeOfOptionalHeader !=
      sizeof(IMAGE_OPTIONAL_HEADER)) {
    // Wrong optional header size.
    return false;
  }
  if (nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) {
    // Wrong magic for optional header.
    return false;
  }

  // Calculate the minimum size for the NT headers and the section header.
  size_t header_size = sizeof(IMAGE_NT_HEADERS) +
      sizeof(IMAGE_SECTION_HEADER) * nt_headers->FileHeader.NumberOfSections;

  if (nt_headers_block->size() < header_size ||
      nt_headers_block->data_size() < header_size) {
    // The block's size isn't large enough for the section headers.
    return false;
  }

  return true;
}

const BlockGraph::Block* GetNtHeadersBlockFromDosHeaderBlock(
    const BlockGraph::Block* dos_header_block) {
  return CheckedGetNtHeadersBlockFromDosHeaderBlock(dos_header_block);
}

BlockGraph::Block* GetNtHeadersBlockFromDosHeaderBlock(
    BlockGraph::Block* dos_header_block) {
  return CheckedGetNtHeadersBlockFromDosHeaderBlock(dos_header_block);
}

bool UpdateDosHeader(BlockGraph::Block* dos_header_block) {
  DCHECK(dos_header_block != NULL);

  // The DOS header has to be a multiple of 16 bytes for historic reasons.
  size_t dos_header_size = common::AlignUp(
      sizeof(IMAGE_DOS_HEADER) + pe::kDosStubSize, 16);

  // If the new header block is shorter than it was, go ahead and
  // trim the source ranges to match the new, shorter size.
  if (dos_header_block->size() > dos_header_size) {
    BlockGraph::Block::DataRange range(
        dos_header_size, dos_header_block->size() - dos_header_size);
    dos_header_block->source_ranges().RemoveMappedRange(range);
  }

  dos_header_block->ResizeData(dos_header_size);
  dos_header_block->set_size(dos_header_size);
  DCHECK_EQ(dos_header_size, dos_header_block->size());
  DCHECK_EQ(dos_header_size, dos_header_block->data_size());

  TypedBlock<IMAGE_DOS_HEADER> dos_header;
  if (!dos_header.InitWithSize(0, dos_header_size, dos_header_block)) {
    LOG(ERROR) << "Unable to cast IMAGE_DOS_HEADER.";
    return false;
  }

  // Wipe the DOS header and fill in the stub.
  memset(dos_header.Get(), 0, sizeof(IMAGE_DOS_HEADER));
  memcpy(dos_header.Get() + 1, pe::kDosStub, pe::kDosStubSize);

  dos_header->e_magic = IMAGE_DOS_SIGNATURE;
  // Calculate the number of bytes used on the last DOS executable "page".
  dos_header->e_cblp = dos_header_size % 512;
  // Calculate the number of pages used by the DOS executable.
  dos_header->e_cp = dos_header_size / 512;
  // Count the last page if we didn't have an even multiple
  if (dos_header->e_cblp != 0)
    dos_header->e_cp++;

  // Header length in "paragraphs".
  dos_header->e_cparhdr = sizeof(*dos_header) / 16;

  // Set this to max allowed, just because.
  dos_header->e_maxalloc = 0xFFFF;

  // Location of relocs - our header has zero relocs, but we set this anyway.
  dos_header->e_lfarlc = sizeof(*dos_header);

  DCHECK(IsValidDosHeaderBlock(dos_header_block));

  return true;
}

namespace {

enum EntryPointTypeEnum { kExeEntryPoint, kDllEntryPoint };

bool GetImageEntryPoint(BlockGraph::Block* dos_header_block,
                        EntryPointTypeEnum desired_entry_point_type,
                        EntryPoint* entry_point) {
  DCHECK(dos_header_block != NULL);
  DCHECK(entry_point != NULL);

  *entry_point = EntryPoint(static_cast<BlockGraph::Block*>(NULL), 0);

  BlockGraph::Block* nt_headers_block =
      pe::GetNtHeadersBlockFromDosHeaderBlock(dos_header_block);

  TypedBlock<IMAGE_NT_HEADERS> nt_headers;
  if (nt_headers_block == NULL || !nt_headers.Init(0, nt_headers_block)) {
    LOG(ERROR) << "Unable to retrieve NT Headers.";
    return false;
  }

  EntryPointTypeEnum entry_point_type = kExeEntryPoint;
  if ((nt_headers->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0)
    entry_point_type = kDllEntryPoint;

  if (entry_point_type != desired_entry_point_type)
    return true;

  BlockGraph::Reference entry_point_ref;
  bool found = nt_headers.block()->GetReference(
      offsetof(IMAGE_NT_HEADERS, OptionalHeader.AddressOfEntryPoint),
      &entry_point_ref);

  if (!found && entry_point_type == kExeEntryPoint) {
    LOG(ERROR) << "Malformed PE Headers: No entry point found for executable.";
    return false;
  }

  if (found) {
    *entry_point = EntryPoint(entry_point_ref.referenced(),
                              entry_point_ref.offset());
  }

  return true;
}

}  // namespace

bool GetExeEntryPoint(BlockGraph::Block* dos_header_block,
                      EntryPoint* entry_point) {
  return GetImageEntryPoint(dos_header_block, kExeEntryPoint, entry_point);
}

bool GetDllEntryPoint(BlockGraph::Block* dos_header_block,
                      EntryPoint* entry_point) {
  return GetImageEntryPoint(dos_header_block, kDllEntryPoint, entry_point);
}

bool GetTlsInitializers(BlockGraph::Block* dos_header_block,
                        EntryPointSet* entry_points) {
  DCHECK(dos_header_block != NULL);
  DCHECK(entry_points != NULL);

  BlockGraph::Block* nt_headers_block =
      pe::GetNtHeadersBlockFromDosHeaderBlock(dos_header_block);

  TypedBlock<IMAGE_NT_HEADERS> nt_headers;
  if (nt_headers_block == NULL || !nt_headers.Init(0, nt_headers_block)) {
    LOG(ERROR) << "Unable to retrieve NT Headers.";
    return false;
  }

  // If the module has no TLS directory then there are no TLS initializers
  // and hence nothing to do.
  const IMAGE_DATA_DIRECTORY& data_dir =
      nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
  if (data_dir.Size == 0 || !nt_headers.HasReference(data_dir.VirtualAddress)) {
    return true;
  }

  // Find the TLS directory.
  TypedBlock<IMAGE_TLS_DIRECTORY> tls_dir;
  if (!nt_headers.Dereference(data_dir.VirtualAddress, &tls_dir)) {
    LOG(ERROR) << "Failed to cast TLS directory.";
    return false;
  }

  // Get the TLS initializer callbacks. We manually lookup the reference
  // because it is an indirect reference, which can't be dereferenced by
  // TypedBlock.
  typedef BlockGraph::Block::ReferenceMap ReferenceMap;
  ReferenceMap::const_iterator callback_ref =
      tls_dir.block()->references().find(
          tls_dir.OffsetOf(tls_dir->AddressOfCallBacks));
  if (callback_ref == tls_dir.block()->references().end()) {
    LOG(ERROR) << "Failed to locate TLS initializers.";
    return false;
  }

  // Note each of the TLS entry points.
  const BlockGraph::Block* callbacks_block = callback_ref->second.referenced();
  const ReferenceMap& ref_map = callbacks_block->references();
  ReferenceMap::const_iterator iter = ref_map.begin();
  for (; iter != ref_map.end(); ++iter) {
    const BlockGraph::Reference& ref = iter->second;
    DCHECK(ref.size() == sizeof(core::AbsoluteAddress));
    entry_points->insert(
        std::make_pair(ref.referenced(), ref.offset()));
  }

  return true;
}

bool HasImportEntry(block_graph::BlockGraph::Block* header_block,
                    const base::StringPiece& dll_name,
                    bool* has_import_entry) {
  DCHECK(header_block != NULL);
  DCHECK(dll_name != NULL);
  DCHECK(!dll_name.empty());
  DCHECK(has_import_entry != NULL);

  *has_import_entry = false;

  DosHeader dos_header;
  NtHeaders nt_headers;
  if (!dos_header.Init(0, header_block) ||
      !dos_header.Dereference(dos_header->e_lfanew, &nt_headers)) {
    LOG(ERROR) << "Unable to cast image headers.";
    return false;
  }

  BlockGraph::Block* image_import_descriptor_block;
  IMAGE_DATA_DIRECTORY* import_directory =
      nt_headers->OptionalHeader.DataDirectory + IMAGE_DIRECTORY_ENTRY_IMPORT;
  DCHECK(nt_headers.HasReference(import_directory->VirtualAddress));

  ImageImportDescriptor image_import_descriptor;
  if (!nt_headers.Dereference(import_directory->VirtualAddress,
                              &image_import_descriptor)) {
    // This could happen if the image import descriptor array is empty, and
    // terminated by a *partial* null entry. However, we've not yet seen that.
    LOG(ERROR) << "Failed to dereference Image Import Descriptor Array.";
    return false;
  }

  image_import_descriptor_block = image_import_descriptor.block();

  ImageImportDescriptor iida;
  if (!iida.Init(0, image_import_descriptor_block)) {
    LOG(ERROR) << "Unable to cast Image Import Descriptor.";
    return false;
  }

  // The array is NULL terminated with a potentially incomplete descriptor so
  // we can't use ElementCount - 1.
  DCHECK_GT(image_import_descriptor_block->size(), 0U);
  size_t descriptor_count =
      (common::AlignUp(image_import_descriptor_block->size(),
                       sizeof(IMAGE_IMPORT_DESCRIPTOR)) /
       sizeof(IMAGE_IMPORT_DESCRIPTOR)) - 1;

  for (size_t iida_index = 0; iida_index < descriptor_count; ++iida_index) {
    String ref_dll_name;
    if (!iida.Dereference(iida[iida_index].Name, &ref_dll_name)) {
      LOG(ERROR) << "Unable to dereference DLL name.";
      return false;
    }

    size_t max_len = ref_dll_name.ElementCount();
    if (base::strncasecmp(ref_dll_name->string, dll_name.data(),
                          max_len) == 0) {
      *has_import_entry = true;
      break;
    }
  }

  return true;
}

}  // namespace pe
