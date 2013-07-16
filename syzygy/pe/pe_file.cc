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
#include "syzygy/pe/pe_file.h"

#include "base/file_util.h"
#include "base/logging.h"

namespace pe {

using core::AbsoluteAddress;
using core::FileOffsetAddress;
using core::RelativeAddress;

PEFile::PEFile()
    : dos_header_(NULL),
      nt_headers_(NULL) {
}

PEFile::~PEFile() {
}

bool PEFile::Init(const base::FilePath& path) {
  PECoffFile::Init(path);

  FILE* file = file_util::OpenFile(path, "rb");
  if (file == NULL) {
    LOG(ERROR) << "Failed to open file " << path.value() << ".";
    return false;
  }

  bool success = ReadHeaders(file);
  if (success)
    success = ReadSections(file);

  file_util::CloseFile(file);

  return success;
}

void PEFile::GetSignature(Signature* signature) const {
  DCHECK(signature != NULL);
  DCHECK(nt_headers_ != NULL);

  // TODO(chrisha): Make GetSignature return a bool, and update all calling
  //     sites.
  base::FilePath abs_path(base::MakeAbsoluteFilePath(path_));
  CHECK(!abs_path.empty());

  signature->path = abs_path.value();
  signature->base_address =
      AbsoluteAddress(nt_headers_->OptionalHeader.ImageBase);
  signature->module_size = nt_headers_->OptionalHeader.SizeOfImage;
  signature->module_time_date_stamp = nt_headers_->FileHeader.TimeDateStamp;
  signature->module_checksum = nt_headers_->OptionalHeader.CheckSum;
}

bool PEFile::Contains(AbsoluteAddress abs, size_t len) const {
  RelativeAddress rel;
  return Translate(abs, &rel) && Contains(rel, len);
}

size_t PEFile::GetSectionIndex(AbsoluteAddress abs, size_t len) const {
  RelativeAddress rel;
  if (!Translate(abs, &rel))
    return kInvalidSection;
  return GetSectionIndex(rel, len);
}

const IMAGE_SECTION_HEADER* PEFile::GetSectionHeader(
    AbsoluteAddress abs, size_t len) const {
  RelativeAddress rel;
  if (!Translate(abs, &rel))
    return NULL;
  return GetSectionHeader(rel, len);
}

size_t PEFile::GetSectionIndex(const char* name) const {
  size_t section_count = file_header_->NumberOfSections;
  for (size_t i = 0; i < section_count; ++i) {
    const IMAGE_SECTION_HEADER* header = section_headers_ + i;
    if (strncmp(reinterpret_cast<const char*>(header->Name), name,
                IMAGE_SIZEOF_SHORT_NAME) == 0)
      return i;
  }
  return kInvalidSection;
}

const IMAGE_SECTION_HEADER* PEFile::GetSectionHeader(const char* name) const {
  size_t id = GetSectionIndex(name);
  if (id == kInvalidSection)
    return NULL;
  return section_headers_ + id;
}

bool PEFile::ReadHeaders(FILE* file) {
  // Read the DOS header.
  IMAGE_DOS_HEADER dos_header = {};
  if (!ReadAt(file, 0, &dos_header, sizeof(dos_header))) {
    LOG(ERROR) << "Unable to read DOS header.";
    return false;
  }

  // And the NT headers.
  IMAGE_NT_HEADERS nt_headers = {};
  size_t pos = dos_header.e_lfanew;
  if (!ReadAt(file, pos, &nt_headers, sizeof(nt_headers))) {
    LOG(ERROR) << "Unable to read NT headers.";
    return false;
  }

  FileOffsetAddress file_header_start(
      pos + offsetof(IMAGE_NT_HEADERS, FileHeader));
  if (!ReadCommonHeaders(file, file_header_start)) {
    return false;
  }

  ImageAddressSpace::RangeMap::iterator it = image_data_.begin();
  DCHECK(it != image_data_.end());
  SectionBuffer& header = it->second.buffer;

  // TODO(siggi): Validate these pointers!
  dos_header_ = reinterpret_cast<IMAGE_DOS_HEADER*>(&header.at(0));
  nt_headers_ =
      reinterpret_cast<IMAGE_NT_HEADERS*>(&header.at(dos_header_->e_lfanew));

  return true;
}

bool PEFile::Translate(RelativeAddress rel, AbsoluteAddress* abs) const {
  DCHECK(abs != NULL);
  if (rel.value() >= nt_headers_->OptionalHeader.SizeOfImage)
    return false;
  abs->set_value(rel.value() + nt_headers_->OptionalHeader.ImageBase);
  return true;
}

bool PEFile::Translate(AbsoluteAddress abs, RelativeAddress* rel) const {
  DCHECK(rel != NULL);
  uint32 rel_addr = AbsToRelDisplacement(abs.value());
  if (rel_addr >= nt_headers_->OptionalHeader.SizeOfImage)
    return false;
  rel->set_value(rel_addr);
  return true;
}

bool PEFile::Translate(FileOffsetAddress offs, RelativeAddress* rel) const {
  DCHECK(rel != NULL);

  // The first "previous section" is the headers.
  RelativeAddress previous_section_start(0);
  FileOffsetAddress previous_section_file_start(0);
  const IMAGE_SECTION_HEADER* previous_section = NULL;
  for (size_t i = 0; i < nt_headers_->FileHeader.NumberOfSections; ++i) {
    if (offs.value() < section_headers_[i].PointerToRawData)
      break;

    previous_section_start.set_value(section_headers_[i].VirtualAddress);
    previous_section_file_start.set_value(section_headers_[i].PointerToRawData);
    previous_section = section_headers_ + i;
  }

  size_t section_offset = offs - previous_section_file_start;
  if (previous_section != NULL &&
      section_offset >= previous_section->SizeOfRawData) {
    return false;
  }

  *rel = previous_section_start + section_offset;

  return true;
}

bool PEFile::Translate(RelativeAddress rel, FileOffsetAddress* offs) const {
  DCHECK(offs != NULL);

  // In the headers?
  if (rel.value() < section_header(0)->VirtualAddress) {
    offs->set_value(rel.value());
    return true;
  }

  // Find the section in which this address lies.
  const IMAGE_SECTION_HEADER* section = GetSectionHeader(rel, 1);
  if (section == NULL)
    return false;

  // Calculate the offset of this address and ensure it can be expressed as
  // a file offset (lies in the explicit data part of the section, not the
  // implicit virtual data at the end).
  size_t section_offset = rel.value() - section->VirtualAddress;
  if (section_offset >= section->SizeOfRawData)
    return false;

  offs->set_value(section->PointerToRawData + section_offset);

  return true;
}

const uint8* PEFile::GetImageData(AbsoluteAddress abs, size_t len) const {
  RelativeAddress rel;
  if (Translate(abs, &rel))
    return GetImageData(rel, len);

  return NULL;
}

uint8* PEFile::GetImageData(AbsoluteAddress abs, size_t len) {
  return const_cast<uint8*>(
      static_cast<const PEFile*>(this)->GetImageData(abs, len));
}

bool PEFile::ReadImage(AbsoluteAddress abs, void* data, size_t len) const {
  RelativeAddress rel;
  if (!Translate(abs, &rel))
    return false;

  return ReadImage(rel, data, len);
}

bool PEFile::ReadImageString(AbsoluteAddress abs, std::string* str) const {
  RelativeAddress rel;
  if (!Translate(abs, &rel))
    return false;

  return ReadImageString(rel, str);
}

bool PEFile::DecodeRelocs(RelocSet* relocs) const {
  DCHECK(nt_headers_ != NULL);
  DCHECK(relocs != NULL);

  // Walk the relocs.
  IMAGE_DATA_DIRECTORY dir =
      nt_headers_->OptionalHeader.DataDirectory[
          IMAGE_DIRECTORY_ENTRY_BASERELOC];
  RelativeAddress offs(dir.VirtualAddress);
  RelativeAddress end(offs + dir.Size);

  const IMAGE_BASE_RELOCATION* hdr = NULL;
  for (; offs < end; offs += hdr->SizeOfBlock) {
    // Read the next header.
    if (!GetImageData(offs, sizeof(hdr), &hdr)) {
      LOG(ERROR) << "Failed to read relocation block header.";
      return false;
    }

    // Read the entries.
    size_t num_relocs = (hdr->SizeOfBlock - sizeof(*hdr)) / sizeof(WORD);
    const WORD* reloc_block = NULL;
    if (!GetImageData(offs + sizeof(*hdr), sizeof(*reloc_block) * num_relocs,
                      &reloc_block)) {
      LOG(ERROR) << "Failed to read relocation entries.";
      return false;
    }

    // Walk the entries.
    for (size_t i = 0; i < num_relocs; ++i) {
      uint8 type = reloc_block[i] >> 12;
      uint16 offs = reloc_block[i] & 0xFFF;
      DCHECK(type == IMAGE_REL_BASED_HIGHLOW ||
             type == IMAGE_REL_BASED_ABSOLUTE);

      if (type == IMAGE_REL_BASED_HIGHLOW) {
        // Record the entry.
        relocs->insert(RelativeAddress(hdr->VirtualAddress) + offs);
      }
    }
  }

  DCHECK(offs == end);
  return true;
}

bool PEFile::ReadRelocs(const RelocSet& relocs, RelocMap* reloc_values) const {
  RelocSet::const_iterator it(relocs.begin());
  for (; it != relocs.end(); ++it) {
    const AbsoluteAddress* abs = NULL;
    if (!GetImageData(*it, sizeof(*abs), &abs)) {
      LOG(ERROR) << "Failed to read reloc at " << it->value() << ".";
      return false;
    }

    reloc_values->insert(std::make_pair(*it, *abs));
  }

  return true;
}

bool PEFile::DecodeExports(ExportInfoVector* exports) const {
  DCHECK(exports != NULL);

  IMAGE_DATA_DIRECTORY dir = nt_headers_->OptionalHeader.
      DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  RelativeAddress addr(dir.VirtualAddress);
  RelativeAddress end(addr + dir.Size);

  if (addr.value() == 0)
    return true;

  const IMAGE_EXPORT_DIRECTORY* export_dir = NULL;
  if (!GetImageData(addr, sizeof(export_dir), &export_dir)) {
    LOG(ERROR) << "Unable to read export directory.";
    return false;
  }

  const RelativeAddress* functions = NULL;
  if (!GetImageData(RelativeAddress(export_dir->AddressOfFunctions),
                    sizeof(*functions) * export_dir->NumberOfFunctions,
                    &functions)) {
    LOG(ERROR) << "Unable to read export functions.";
    return false;
  }

  const RelativeAddress* names = NULL;
  if (!GetImageData(RelativeAddress(export_dir->AddressOfNames),
                    sizeof(*functions) * export_dir->NumberOfNames,
                    &names)) {
    LOG(ERROR) << "Unable to read export names.";
    return false;
  }

  const WORD* name_ordinals = NULL;
  if (!GetImageData(RelativeAddress(export_dir->AddressOfNameOrdinals),
                    sizeof(*functions) * export_dir->NumberOfNames,
                    &name_ordinals)) {
    LOG(ERROR) << "Unable to read name ordinals.";
    return false;
  }

  for (size_t index = 0; index < export_dir->NumberOfFunctions; ++index) {
    // Is it a blank entry?
    if (functions[index] != RelativeAddress(0)) {
      ExportInfo info;
      info.ordinal = index + 1;

      RelativeAddress function = functions[index];
      // Is it a forward?
      if (function >= addr && function < end) {
        if (!ReadImageString(function, &info.forward)) {
          LOG(ERROR) << "Unable to read export forward string.";
          return false;
        }
      } else {
        info.function = function;
      }

      // Does it have a name?
      for (size_t i = 0; i < export_dir->NumberOfNames; ++i) {
        if (name_ordinals[i] == index) {
          if (!ReadImageString(names[i], &info.name)) {
            LOG(ERROR) << "Unable to read export name.";
            return false;
          }
          break;
        }
      }

      exports->push_back(info);
    }
  }

  return true;
}

bool PEFile::DecodeImports(ImportDllVector* imports) const {
  DCHECK(imports != NULL);

  // Walk the import thunks.
  IMAGE_DATA_DIRECTORY dir = nt_headers_->OptionalHeader.
      DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  RelativeAddress offs(dir.VirtualAddress);
  RelativeAddress end(offs + dir.Size);

  const IMAGE_IMPORT_DESCRIPTOR* import_desc = NULL;
  for (; offs < end; offs += sizeof(*import_desc)) {
    if (!GetImageData(offs, sizeof(*import_desc), &import_desc)) {
      LOG(ERROR) << "Unable to read import descriptor.";
      return false;
    }

    if (import_desc->Characteristics == 0 && import_desc->FirstThunk == 0) {
      // This is the last chunk, bail the loop.
      break;
    }

    std::string dll_name;
    if (!ReadImageString(RelativeAddress(import_desc->Name), &dll_name)) {
      LOG(ERROR) << "Unable to read import descriptor name.";
      return false;
    }

    // Iterate the Import Name Table and the Import Address Table
    // concurrently. They will yield, respectively, the name of the
    // function and the address of the entry.
    RelativeAddress int_offs(import_desc->OriginalFirstThunk);
    RelativeAddress iat_offs(import_desc->FirstThunk);

    imports->push_back(ImportDll());
    ImportDll& dll = imports->back();
    dll.name = dll_name;
    dll.desc = *import_desc;

    while (true) {
      IMAGE_THUNK_DATA int_thunk = {};
      IMAGE_THUNK_DATA iat_thunk = {};

      if (!ReadImage(int_offs, &int_thunk, sizeof(int_thunk)) ||
          !ReadImage(iat_offs, &iat_thunk, sizeof(iat_thunk))) {
        LOG(ERROR) << "Unable to read import name or address table thunk.";
        return false;
      }

      // Are we at the end of the table?
      if (int_thunk.u1.Function == 0) {
        DCHECK_EQ(0U, iat_thunk.u1.Function);
        break;
      }

      uint16 hint = 0;
      uint16 ordinal = 0;
      std::string function_name;
      if (int_thunk.u1.AddressOfData & IMAGE_ORDINAL_FLAG32) {
        // It's an ordinal.
        ordinal = IMAGE_ORDINAL32(int_thunk.u1.Ordinal);
      } else {
        // Read the hint word, followed by the function name.
        RelativeAddress import_name(int_thunk.u1.AddressOfData);
        if (!ReadImage(import_name, &hint, sizeof(hint)) ||
            !ReadImageString(import_name + sizeof(hint), &function_name)) {
          LOG(ERROR) << "Unable to read import function hint or name.";
          return false;
        }
      }

      dll.functions.push_back(ImportInfo());
      ImportInfo& info = dll.functions.back();
      info.function = function_name;
      info.ordinal = ordinal;
      info.hint = hint;

      int_offs += sizeof(int_thunk);
      iat_offs += sizeof(iat_thunk);
    }
  }

  return true;
}

bool PEFile::Signature::IsConsistent(const Signature& signature) const {
  return IsConsistentExceptForChecksum(signature) &&
      module_checksum == signature.module_checksum;
}

bool PEFile::Signature::IsConsistentExceptForChecksum(
    const Signature& signature) const {
  return base_address == signature.base_address &&
      module_size == signature.module_size &&
      module_time_date_stamp == signature.module_time_date_stamp;
}

bool PEFile::Signature::Save(core::OutArchive* out_archive) const {
  return out_archive->Save(path) &&
      out_archive->Save(base_address) &&
      out_archive->Save(module_size) &&
      out_archive->Save(module_time_date_stamp) &&
      out_archive->Save(module_checksum);
}

bool PEFile::Signature::Load(core::InArchive* in_archive) {
  return in_archive->Load(&path) &&
      in_archive->Load(&base_address) &&
      in_archive->Load(&module_size) &&
      in_archive->Load(&module_time_date_stamp) &&
      in_archive->Load(&module_checksum);
}

}  // namespace pe
