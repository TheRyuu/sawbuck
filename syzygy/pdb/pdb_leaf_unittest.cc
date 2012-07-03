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

#include "syzygy/pdb/pdb_leaf.h"

#include "base/file_util.h"
#include "gtest/gtest.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pdb/cvinfo_ext.h"
#include "syzygy/pdb/pdb_byte_stream.h"
#include "syzygy/pdb/pdb_data.h"
#include "syzygy/pdb/pdb_type_info_stream.h"
#include "syzygy/pdb/unittest_util.h"
#include "syzygy/pe/unittest_util.h"

namespace pdb {

namespace cci = Microsoft_Cci_Pdb;

const uint16 array_of_leaf_types[] = {
    cci::LF_VTSHAPE,
    cci::LF_COBOL1,
    cci::LF_LABEL,
    cci::LF_NULL,
    cci::LF_NOTTRAN,
    cci::LF_ENDPRECOMP,
    cci::LF_TYPESERVER_ST,
    cci::LF_LIST,
    cci::LF_REFSYM,
    cci::LF_ENUMERATE_ST,
    cci::LF_TI16_MAX,
    cci::LF_MODIFIER,
    cci::LF_POINTER,
    cci::LF_ARRAY_ST,
    cci::LF_CLASS_ST,
    cci::LF_STRUCTURE_ST,
    cci::LF_UNION_ST,
    cci::LF_ENUM_ST,
    cci::LF_PROCEDURE,
    cci::LF_MFUNCTION,
    cci::LF_COBOL0,
    cci::LF_BARRAY,
    cci::LF_DIMARRAY_ST,
    cci::LF_VFTPATH,
    cci::LF_PRECOMP_ST,
    cci::LF_OEM,
    cci::LF_ALIAS_ST,
    cci::LF_OEM2,
    cci::LF_SKIP,
    cci::LF_ARGLIST,
    cci::LF_DEFARG_ST,
    cci::LF_FIELDLIST,
    cci::LF_DERIVED,
    cci::LF_BITFIELD,
    cci::LF_METHODLIST,
    cci::LF_DIMCONU,
    cci::LF_DIMCONLU,
    cci::LF_DIMVARU,
    cci::LF_DIMVARLU,
    cci::LF_BCLASS,
    cci::LF_VBCLASS,
    cci::LF_IVBCLASS,
    cci::LF_FRIENDFCN_ST,
    cci::LF_INDEX,
    cci::LF_MEMBER_ST,
    cci::LF_STMEMBER_ST,
    cci::LF_METHOD_ST,
    cci::LF_NESTTYPE_ST,
    cci::LF_VFUNCTAB,
    cci::LF_FRIENDCLS,
    cci::LF_ONEMETHOD_ST,
    cci::LF_VFUNCOFF,
    cci::LF_NESTTYPEEX_ST,
    cci::LF_MEMBERMODIFY_ST,
    cci::LF_MANAGED_ST,
    cci::LF_ST_MAX,
    cci::LF_TYPESERVER,
    cci::LF_ENUMERATE,
    cci::LF_ARRAY,
    cci::LF_CLASS,
    cci::LF_STRUCTURE,
    cci::LF_UNION,
    cci::LF_ENUM,
    cci::LF_DIMARRAY,
    cci::LF_PRECOMP,
    cci::LF_ALIAS,
    cci::LF_DEFARG,
    cci::LF_FRIENDFCN,
    cci::LF_MEMBER,
    cci::LF_STMEMBER,
    cci::LF_METHOD,
    cci::LF_NESTTYPE,
    cci::LF_ONEMETHOD,
    cci::LF_NESTTYPEEX,
    cci::LF_MEMBERMODIFY,
    cci::LF_MANAGED,
    cci::LF_TYPESERVER2,
    cci::LF_NUMERIC,
    cci::LF_CHAR,
    cci::LF_SHORT,
    cci::LF_USHORT,
    cci::LF_LONG,
    cci::LF_ULONG,
    cci::LF_REAL32,
    cci::LF_REAL64,
    cci::LF_REAL80,
    cci::LF_REAL128,
    cci::LF_QUADWORD,
    cci::LF_UQUADWORD,
    cci::LF_COMPLEX32,
    cci::LF_COMPLEX64,
    cci::LF_COMPLEX80,
    cci::LF_COMPLEX128,
    cci::LF_VARSTRING,
    cci::LF_OCTWORD,
    cci::LF_UOCTWORD,
    cci::LF_DECIMAL,
    cci::LF_DATE,
    cci::LF_UTF8STRING
};

TEST(PdbLeafTest, DumpInvalidLeafTypes) {
  FilePath stdout_path;
  ASSERT_TRUE(file_util::CreateTemporaryFile(&stdout_path));
  FILE* out_file = file_util::OpenFile(stdout_path, "w");

  // First we have to create a type info stream.

  scoped_refptr<PdbByteStream> type_info_stream(new PdbByteStream());
  scoped_refptr<WritablePdbStream> writable_stream =
      type_info_stream->GetWritablePdbStream();
  ASSERT_TRUE(writable_stream != NULL);

  TypeInfoHeader header = {};
  header.len = sizeof(header);
  // The minimal data size for a non-empty type info block is 4 bytes, 2 for
  // the record length and 2 for the type Id.
  header.type_info_data_size = 4;
  ASSERT_TRUE(writable_stream->Write(header));
  const uint16 type_info_record_length = 2;
  ASSERT_TRUE(writable_stream->Write(type_info_record_length));

  // Iterate over each leaf type and update the type info stream each time.
  for (uint16 i = 0; i < sizeof(array_of_leaf_types); ++i) {
    // First we need to modify the header to make it match the current kind of
    // leaf.
    uint16 current_type = array_of_leaf_types[i];
    uint32 min_type = current_type;
    uint32 max_type = min_type + 1;
    writable_stream->set_pos(offsetof(TypeInfoHeader, type_min));
    writable_stream->Write(min_type);
    writable_stream->Write(max_type);

    // Then we have to modify the data section of this stream by setting the
    // type of the record.
    const size_t field_type_offset = sizeof(TypeInfoHeader)
        + sizeof(type_info_record_length);
    writable_stream->set_pos(field_type_offset);
    writable_stream->Write(current_type);

    // Now this fake stream should be readable. An error will be logged if we
    // try to dump a kind of leaf for which the implementation have been done
    // because there's nothing in the data section.
    TypeInfoHeader header_temp;
    TypeInfoRecordMap types_map;
    EXPECT_TRUE(ReadTypeInfoStream(type_info_stream.get(),
                                   &header_temp,
                                   &types_map));
    DumpTypeInfoStream(out_file, type_info_stream.get(), header, types_map);
  }

  ::file_util::CloseFile(out_file);
}

}  // namespace pdb
