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
//
// This file allows dumping the content of the symbol record stream of a PDB.

#ifndef SYZYGY_EXPERIMENTAL_PDB_DUMPER_PDB_SYMBOL_RECORD_DUMPER_H_
#define SYZYGY_EXPERIMENTAL_PDB_DUMPER_PDB_SYMBOL_RECORD_DUMPER_H_

#include <vector>

#include "base/basictypes.h"
#include "syzygy/pdb/pdb_data_types.h"

namespace pdb {

// Forward declarations.
class PdbStream;

// Dumps @p symbol_record_vector from @p stream to out with a specified level of
// indentation.
void DumpSymbolRecord(FILE* out,
                      PdbStream* stream,
                      const SymbolRecordVector& sym_record_vector,
                      uint8 indent_level);

}  // namespace pdb

#endif  // SYZYGY_EXPERIMENTAL_PDB_DUMPER_PDB_SYMBOL_RECORD_STREAM_DUMPER_H_
