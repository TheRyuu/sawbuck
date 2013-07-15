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
//
// Implements utility functions useful to grinders that process basic-block
// frequency data.

#include "syzygy/grinder/basic_block_util.h"

#include <algorithm>
#include <functional>

#include "syzygy/common/indexed_frequency_data.h"
#include "syzygy/pdb/pdb_reader.h"
#include "syzygy/pdb/pdb_util.h"
#include "syzygy/pe/find.h"

namespace grinder {
namespace basic_block_util {

bool ModuleIdentityComparator::operator()(
    const ModuleInformation& lhs, const ModuleInformation& rhs) {
  if (lhs.module_size < rhs.module_size)
    return true;
  if (lhs.module_size > rhs.module_size)
    return false;

  if (lhs.time_date_stamp < rhs.time_date_stamp)
    return true;
  if (lhs.time_date_stamp > rhs.time_date_stamp)
    return false;

  return lhs.image_file_name < rhs.image_file_name;
}

void InitModuleInfo(const pe::PEFile::Signature& signature,
                    ModuleInformation* module_info) {
  DCHECK(module_info != NULL);
  module_info->base_address = signature.base_address.value();
  module_info->image_checksum = signature.module_checksum;
  module_info->image_file_name = signature.path;
  module_info->module_size = signature.module_size;
  module_info->time_date_stamp = signature.module_time_date_stamp;
}

bool FindEntryCountMap(const pe::PEFile::Signature& signature,
                       const ModuleEntryCountMap& module_entry_count_map,
                       const EntryCountMap** entry_count_map) {
  DCHECK(entry_count_map != NULL);
  *entry_count_map = NULL;

  // Find exactly one consistent entry count vector in the map.
  const EntryCountMap* tmp_entry_count_map = NULL;
  ModuleEntryCountMap::const_iterator it = module_entry_count_map.begin();
  for (; it != module_entry_count_map.end(); ++it) {
    const pe::PEFile::Signature candidate(it->first);
    if (candidate.IsConsistent(signature)) {
      if (tmp_entry_count_map != NULL) {
        LOG(ERROR) << "Found multiple module instances in the entry count map.";
        return false;
      }
      tmp_entry_count_map = &it->second;
    }
  }

  // Handle the case where there is no consistent module found.
  if (tmp_entry_count_map == NULL) {
    LOG(ERROR) << "Did not find module in the entry count map.";
    return false;
  }

  // Return the entry counts that were found.
  *entry_count_map = tmp_entry_count_map;
  return true;
}

bool LoadBasicBlockRanges(const base::FilePath& pdb_path,
                          RelativeAddressRangeVector* bb_ranges) {
  DCHECK(!pdb_path.empty());
  DCHECK(bb_ranges != NULL);

  // Read the PDB file.
  pdb::PdbReader pdb_reader;
  pdb::PdbFile pdb_file;
  if (!pdb_reader.Read(pdb_path, &pdb_file)) {
    LOG(ERROR) << "Failed to read PDB: " << pdb_path.value();
    return false;
  }

  // Get the name-stream map from the PDB.
  pdb::PdbInfoHeader70 pdb_header = {};
  pdb::NameStreamMap name_stream_map;
  if (!pdb::ReadHeaderInfoStream(pdb_file, &pdb_header, &name_stream_map)) {
    LOG(ERROR) << "Failed to read PDB header info stream: " << pdb_path.value();
    return false;
  }

  // Get the basic block addresses from the PDB file.
  pdb::NameStreamMap::const_iterator name_it = name_stream_map.find(
      common::kBasicBlockRangesStreamName);
  if (name_it == name_stream_map.end()) {
    LOG(ERROR) << "PDB does not contain basic block ranges stream: "
               << pdb_path.value();
    return false;
  }
  scoped_refptr<pdb::PdbStream> bb_ranges_stream;
  bb_ranges_stream = pdb_file.GetStream(name_it->second);
  if (bb_ranges_stream.get() == NULL) {
    LOG(ERROR) << "PDB basic block ranges stream has invalid index: "
               << name_it->second;
    return false;
  }

  // Read the basic block range stream.
  if (!bb_ranges_stream->Seek(0) ||
      !bb_ranges_stream->Read(bb_ranges)) {
    LOG(ERROR) << "Failed to read basic block range stream from PDB: "
               << pdb_path.value();
    return false;
  }

  return true;
}

bool LoadPdbInfo(PdbInfoMap* pdb_info_cache,
                 const ModuleInformation& module_info,
                 PdbInfo** pdb_info) {
  DCHECK(pdb_info_cache != NULL);
  DCHECK(pdb_info != NULL);

  *pdb_info = NULL;

  // Look for a cached entry first. If the cached entry is found but has no
  // pdb_path then it is a cached failure.
  PdbInfoMap::iterator it = pdb_info_cache->find(module_info);
  if (it != pdb_info_cache->end()) {
    *pdb_info = &(it->second);
    bool is_valid = !it->second.pdb_path.empty();
    return is_valid;
  }

  // Insert a new (empty) PdbInfo for module_info and keep a reference to it.
  // If any of the operations below fail, the pdb_path in the PdbInfo structure
  // will not have been populated.
  PdbInfo& pdb_info_ref = (*pdb_info_cache)[module_info];

  // Find the PDB file for the module.
  base::FilePath pdb_path;
  base::FilePath module_path(module_info.image_file_name);
  if (!pe::FindPdbForModule(module_path, &pdb_path) || pdb_path.empty()) {
    LOG(ERROR) << "Failed to find PDB for module: " << module_path.value();
    return false;
  }

  // Load the line information from the PDB.
  if (!pdb_info_ref.line_info.Init(pdb_path)) {
    LOG(ERROR) << "Failed to extract line information from PDB file: "
               << pdb_path.value();
    return false;
  }

  // This logs verbosely for us.
  if (!LoadBasicBlockRanges(pdb_path, &pdb_info_ref.bb_ranges)) {
    return false;
  }

  // Populate the pdb_path field of pdb_info_ref, which marks the cached
  // entry as valid.
  pdb_info_ref.pdb_path = pdb_path;

  // Return a pointer to the pdb_info entry.
  *pdb_info = &pdb_info_ref;

  return true;
}

bool IsValidFrequencySize(size_t size) {
  return size == 1 || size == 2 || size == 4;
}

// TODO(sebmarchand): Move this to indexed_frequency_util.
uint32 GetFrequency(const TraceIndexedFrequencyData* data, size_t bb_id) {
  DCHECK(data != NULL);
  DCHECK_EQ(1U, data->num_columns);
  DCHECK(IsValidFrequencySize(data->frequency_size));
  DCHECK_LT(bb_id, data->num_entries);

  DCHECK(data->data_type == common::IndexedFrequencyData::BASIC_BLOCK_ENTRY ||
         data->data_type == common::IndexedFrequencyData::COVERAGE);

  switch (data->frequency_size) {
    case 1:
      return data->frequency_data[bb_id];
    case 2:
      return reinterpret_cast<const uint16*>(data->frequency_data)[bb_id];
    case 4:
      return reinterpret_cast<const uint32*>(data->frequency_data)[bb_id];
  }

  NOTREACHED();
  return 0;
}

}  // namespace basic_block_util
}  // namespace grinder
