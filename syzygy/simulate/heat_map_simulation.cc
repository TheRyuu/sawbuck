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

#include "syzygy/simulate/heat_map_simulation.h"

#include "syzygy/core/json_file_writer.h"

namespace simulate {

HeatMapSimulation::HeatMapSimulation()
    : time_slice_usecs_(kDefaultTimeSliceSize),
      memory_slice_bytes_(kDefaultMemorySliceSize) {
}

bool HeatMapSimulation::SerializeToJSON(FILE* output, bool pretty_print) {
  DCHECK(output != NULL);

  core::JSONFileWriter json_file(output, pretty_print);

  if (!json_file.OpenDict() ||
      !json_file.OutputKey("time_slice_usecs") ||
      !json_file.OutputInteger(time_slice_usecs_) ||
      !json_file.OutputKey("memory_slice_bytes") ||
      !json_file.OutputInteger(memory_slice_bytes_) ||
      !json_file.OutputKey("time_slice_list") ||
      !json_file.OpenList()) {
    return false;
  }

  TimeMemoryMap::const_iterator time_memory_iter = time_memory_map_.begin();
  for (; time_memory_iter != time_memory_map_.end(); time_memory_iter++) {
    time_t time = time_memory_iter->first;
    uint32 total = time_memory_iter->second.total();
    const TimeSlice& time_slice = time_memory_iter->second;

    if (!json_file.OpenDict() ||
        !json_file.OutputKey("timestamp") ||
        !json_file.OutputInteger(time) ||
        !json_file.OutputKey("total_memory_slices") ||
        !json_file.OutputInteger(total) ||
        !json_file.OutputKey("memory_slice_list") ||
        !json_file.OpenList()) {
      return false;
    }

    TimeSlice::SliceQtyMap::const_iterator slices_iter =
        time_slice.slices().begin();

    for (; slices_iter != time_slice.slices().end(); slices_iter++) {

      if (!json_file.OpenDict() ||
          !json_file.OutputKey("memory_slice") ||
          !json_file.OutputInteger(slices_iter->first) ||
          !json_file.OutputKey("quantity") ||
          !json_file.OutputInteger(slices_iter->second) ||
          !json_file.CloseDict()) {
        return false;
      }
    }

    if (!json_file.CloseList() ||
        !json_file.CloseDict()) {
      return false;
    }
  }

  if (!json_file.CloseList() ||
      !json_file.CloseDict())
    return false;

  DCHECK(json_file.Finished());
  return true;
}

void HeatMapSimulation::OnProcessStarted(base::Time time,
                                         size_t /*default_page_size*/) {
  // Set the entry time of this process.
  process_start_time_ = time;
}

void HeatMapSimulation::OnFunctionEntry(base::Time time,
                                        uint32 block_start,
                                        size_t size) {
  // Get the time when this function was called since the process start.
  time_t relative_time = (time - process_start_time_).InMicroseconds();

  // Since we will insert to a map many TimeSlices with the same entry time,
  // we can pass RegisterFunction a reference to the TimeSlice in the map.
  // This way, RegisterFunction doesn't have to search for that position
  // every time it gets called and the time complexity gets reduced
  // in a logarithmic scale.
  TimeSliceId time_slice = relative_time / time_slice_usecs_;
  TimeSlice& slice = time_memory_map_[time_slice];

  DCHECK(memory_slice_bytes_ != 0);
  const uint32 kStartIndex = block_start / memory_slice_bytes_;
  const uint32 kEndIndex = (block_start + size + memory_slice_bytes_ - 1)
      / memory_slice_bytes_;

  // Loop through all the memory blocks of the current function and
  // add them to the given time_slice.
  for (uint32 i = kStartIndex; i < kEndIndex; i++) {
    slice.AddSlice(i);
  }
}

} // namespace simulate
