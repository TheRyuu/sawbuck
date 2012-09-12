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
// Declares a PDB mutator for adding basic-block addresses and sizes to a
// named PDB stream.

#ifndef SYZYGY_INSTRUMENT_MUTATORS_ADD_BB_RANGES_STREAM_H_
#define SYZYGY_INSTRUMENT_MUTATORS_ADD_BB_RANGES_STREAM_H_

#include "syzygy/core/address.h"
#include "syzygy/core/address_space.h"
#include "syzygy/pdb/mutators/add_named_stream_mutator.h"

namespace instrument {
namespace mutators {

class AddBasicBlockRangesStreamPdbMutator
    : public pdb::mutators::AddNamedStreamMutatorImpl<
          AddBasicBlockRangesStreamPdbMutator> {
 public:
  typedef core::AddressRange<core::RelativeAddress, size_t>
      RelativeAddressRange;
  typedef std::vector<RelativeAddressRange> RelativeAddressRangeVector;

  // Constructor.
  // @param bb_ranges a reference to the vector that contains the
  //     relative addresses associated with the basic blocks in an image. This
  //     need not be populated at the time of construction, so long as it exists
  //     before MutatePdb is called.
  AddBasicBlockRangesStreamPdbMutator(
      const RelativeAddressRangeVector& bb_ranges)
          : bb_ranges_(bb_ranges) {
  }

 protected:
  friend pdb::mutators::AddNamedStreamMutatorImpl<
      AddBasicBlockRangesStreamPdbMutator>;
  friend pdb::mutators::NamedPdbMutatorImpl<
      AddBasicBlockRangesStreamPdbMutator>;

  // Implementation of AddNamedStreamMutatorImpl.
  bool AddNamedStreams(const pdb::PdbFile& pdb_file);

  // Implementation of NamedPdbMutatorImpl.
  static const char kMutatorName[];

  const RelativeAddressRangeVector& bb_ranges_;
};

}  // namespace mutators
}  // namespace instrument

#endif  // SYZYGY_INSTRUMENT_MUTATORS_ADD_BB_RANGES_STREAM_H_
