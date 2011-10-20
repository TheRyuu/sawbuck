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
#ifndef SYZYGY_PE_IMAGE_LAYOUT_H_
#define SYZYGY_PE_IMAGE_LAYOUT_H_

#include <string>
#include <vector>

#include <windows.h>
#include <winnt.h>

#include "base/basictypes.h"
#include "syzygy/core/address_space.h"
#include "syzygy/core/block_graph.h"

namespace pe {

// Fwd.
class PEFileBuilder;

struct ImageLayout {
  // Information necessary to create PE image headers.
  struct HeaderInfo {
    // These fields correspond to the similarly named fields in the
    // IMAGE_FILE_HEADER and IMAGE_OPTIONAL_HEADER members of the
    // IMAGE_NT_HEADERS structure.
    // These fields are exclusive of any field that can be computed from
    // the image itself.
    int16 characteristics;
    uint8 major_linker_version;
    uint8 minor_linker_version;
    size_t image_base;
    size_t section_alignment;
    size_t file_alignment;
    int16 major_operating_system_version;
    int16 minor_operating_system_version;
    int16 major_image_version;
    int16 minor_image_version;
    int16 major_subsystem_version;
    int16 minor_subsystem_version;
    size_t win32_version_value;
    size_t size_of_headers;
    int16 subsystem;
    int16 dll_characteristics;
    size_t size_of_stack_reserve;
    size_t size_of_stack_commit;
    size_t size_of_heap_reserve;
    size_t size_of_heap_commit;
    size_t loader_flags;
  };

  // Per-segment information.
  struct SegmentInfo {
    // Name of the segment, note that this will be truncated to a max of
    // 8 characters on output.
    std::string name;
    // The segment's starting RVA, must be a multiple of the image's
    // SectionAlignment value.
    core::RelativeAddress addr;
    // The virtual size of the segment, must be greater than zero. Any
    // part of the segment that extends beyond data_size is implicitly
    // zero initialized.
    size_t size;
    // The initialized data size of the segment, must be a multple of the
    // image's FileAlignment value.
    size_t data_size;
    // The segment characteristics, a bitmask of IMAGE_SCN_* values.
    uint32 characteristics;
  };

  explicit ImageLayout(core::BlockGraph* block_graph);

  // TODO(siggi): Remove this constructor once PEFileBuilder is
  //    yielding an ImageLayout as output.
  explicit ImageLayout(PEFileBuilder* builder);

  // Information to populate the PE header.
  HeaderInfo header_info;

  // The segments in the image.
  std::vector<SegmentInfo> segments;

  // The blocks that should be written to the image.
  core::BlockGraph::AddressSpace blocks;
};

// Copies relevant fields from NT headers to header info.
void CopyNtHeaderToImageLayout(const IMAGE_NT_HEADERS* nt_headers,
                               ImageLayout::HeaderInfo* header_info);

// Copies section headers to segment info.
void CopySectionHeadersToImageLayout(
    size_t num_sections,
    const IMAGE_SECTION_HEADER* section_headers,
    std::vector<ImageLayout::SegmentInfo>* segments);

}  // namespace pe

#endif  // SYZYGY_PE_IMAGE_LAYOUT_H_
