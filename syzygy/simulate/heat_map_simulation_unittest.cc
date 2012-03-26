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

#include <set>
#include <vector>

#include "base/scoped_temp_dir.h"
#include "syzygy/common/syzygy_version.h"
#include "syzygy/core/random_number_generator.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pdb/omap.h"
#include "syzygy/pe/unittest_util.h"

namespace simulate {

namespace {

using base::Time;
using base::TimeDelta;

class HeatMapSimulationTest : public testing::PELibUnitTest {
 public:
  typedef HeatMapSimulation::TimeMemoryMap TimeMemoryMap;
  typedef HeatMapSimulation::TimeSlice TimeSlice;
  typedef std::vector<FilePath> TraceFileList;

  struct MockBlockInfo {
    time_t time;
    uint32 start;
    size_t size;

    MockBlockInfo(time_t time_, uint32 start_, DWORD size_)
        : time(time_),
          start(start_),
          size(size_) {
    }

    MockBlockInfo() {
    }

    bool operator<(const MockBlockInfo &x) const {
      return time < x.time;
    }
  };
  typedef std::vector<MockBlockInfo> MockBlockInfoList;

  HeatMapSimulationTest() : random_(55) {
  }

  void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    simulation_.reset(new HeatMapSimulation());

    blocks_[0] = MockBlockInfo(20, 0, 3);
    blocks_[1] = MockBlockInfo(20, 0, 3);
    blocks_[2] = MockBlockInfo(20, 2, 4);
    blocks_[3] = MockBlockInfo(20, 2, 1);
    blocks_[4] = MockBlockInfo(20, 10, 3);
    blocks_[5] = MockBlockInfo(20, 10, 3);
    blocks_[6] = MockBlockInfo(20, 10, 4);
    blocks_[7] = MockBlockInfo(20, 10, 1);
    blocks_[8] = MockBlockInfo(40, 2, 5);

    time = Time::FromTimeT(10);
  }

  // Simulates the current simulation with the function blocks given
  // in blocks_ with given parameters, and compares the result to certain
  // expected value.
  // @param expected_size The expected output size.
  // @param expected_times An expected_size sized array with the expected
  //     time of entry of each time slice.
  // @param expected_totals An expected_size sized array with the expected
  //     totals in each time slice.
  // @param expected_slices An expected_size sized array with the expected
  //     memory slices in each time slice.
  void CheckSimulationResult(
      uint32 expected_size,
      const uint32 expected_times[],
      const uint32 expected_totals[],
      const HeatMapSimulation::TimeSlice::SliceQtyMap expected_slices[]) {
    simulation_->OnProcessStarted(time, 1);

    for (uint32 i = 0; i < arraysize(blocks_); i++) {
      simulation_->OnFunctionEntry(Time::FromTimeT(blocks_[i].time),
                                   blocks_[i].start,
                                   blocks_[i].size);
    }

    EXPECT_EQ(simulation_->time_memory_map().size(), expected_size);

    for (uint32 i = 0; i < expected_size; i++) {
      TimeMemoryMap::const_iterator current_slice =
          simulation_->time_memory_map().find(expected_times[i]);

      ASSERT_NE(current_slice, simulation_->time_memory_map().end());
      EXPECT_EQ(current_slice->second.total(), expected_totals[i]);
      EXPECT_EQ(current_slice->second.slices(), expected_slices[i]);
    }
  }

  // Turn a MockBlockInfoList into a vector.
  // @param input The MockBlockInfoList to be transformed.
  // @param size The size of the lastest byte pointed by the MockBlockInfoList.
  // @returns A vector of size size where every element is equal to the number
  //     of different MockBlockInfos in input that cover to that position.
  std::vector<uint32> Vectorize(const MockBlockInfoList &input, size_t size) {
    std::vector<uint32> vector_input(size, 0);
    for (uint32 i = 0; i < input.size(); i++) {
      for (uint32 u = 0; u < input[i].size; u++)
        vector_input[input[i].start + u - input[0].start]++;
    }

    return vector_input;
  }

  // Takes a MockBlockInfoList where all the MockBlockInfos have the same time
  // value and returns another one that should generate the same output.
  // The algorithm consists of repeatly getting MockBlockInfos with start
  // address equal to the first element that isn't full yet, and size equal
  // to some random number from 1 to the distance between our element and
  // the next element that doesn't need more blocks to be full.
  // @param input A MockBlockInfoList where each element has the same size.
  // @returns Another MockBlockInfoList whose output is the same as the
  //     parameter.
  MockBlockInfoList RandomizeTimeBlocks(const MockBlockInfoList &input) {
    MockBlockInfoList random_input;

    if (input.size() == 0) {
      // This should never be reached
      ADD_FAILURE();
      return random_input;
    }

    // Get the time of the blocks, the address of the first block, and the
    // size of all them.
    time_t time = input[0].time;
    uint32 start = input[0].start;
    size_t size = input[0].start + input[0].size;

    for (uint32 i = 0; i < input.size(); i++) {
      if (input[i].time != time) {
        // This should never be reached
        ADD_FAILURE();
        return random_input;
      }
      start = std::min(start, input[i].start);
      size = std::max(size, input[i].start + input[i].size);
    }
    size -= start;

    std::vector<uint32> slices = Vectorize(input, size);

    uint32 slice = 0;
    while (slice < slices.size()) {
      if (slices[slice] == 0) {
        slice++;
        continue;
      }

      size_t max_size = slice;
      for (; max_size < slices.size(); max_size++) {
        if (slices[max_size] == 0)
          break;
      }

      uint32 block_size = 0;
      block_size = random_(max_size - slice) + 1;

      for (uint32 i = 0; i < block_size; i++) {
        if (slices[slice + i] > 0)
          slices[slice + i]--;
      }

      random_input.push_back(MockBlockInfo(time, slice + start, block_size));
    }

    return random_input;
  }

  // Takes a MockBlockInfoList and returns another at random that should
  // generate the same output.
  // @param input The MockBlockInfoList to be transformed.
  // @returns A random MockBlockInfoList that should generate the same output
  //     as input.
  MockBlockInfoList GenerateRandomInput() {
    MockBlockInfoList random_input;
    // std::sort(input.begin(), input.end());

    MockBlockInfoList time_input;
    time_t last_time = blocks_[0].time;

    for (uint32 i = 0; i <= arraysize(blocks_); i++) {
      if (i == arraysize(blocks_) || last_time != blocks_[i].time) {
        MockBlockInfoList random_time_input =
            RandomizeTimeBlocks(time_input);

        random_input.insert(random_input.end(),
                            random_time_input.begin(),
                            random_time_input.end());

        time_input.clear();
      }

      if (i != arraysize(blocks_)) {
        time_input.push_back(blocks_[i]);
        last_time = blocks_[i].time;
      }
    }

    std::random_shuffle(random_input.begin(), random_input.end(), random_);
    return random_input;
  }

  scoped_ptr<HeatMapSimulation> simulation_;

  Time time;
  ScopedTempDir temp_dir_;
  MockBlockInfo blocks_[9];
  core::RandomNumberGenerator random_;
};

}  // namespace

TEST_F(HeatMapSimulationTest, CorrectHeatMap) {
  static const uint32 expected_size = 2;
  static const uint32 expected_times[expected_size] = {10000000, 30000000};
  static const uint32 expected_totals[expected_size] = {8, 1};

  HeatMapSimulation::TimeSlice::SliceQtyMap expected_slices[expected_size];
  expected_slices[0][0] = 8;
  expected_slices[1][0] = 1;

  ASSERT_EQ(arraysize(expected_times), expected_size);
  ASSERT_EQ(arraysize(expected_totals), expected_size);
  ASSERT_EQ(arraysize(expected_slices), expected_size);

  CheckSimulationResult(
      expected_size, expected_times, expected_totals, expected_slices);

  EXPECT_EQ(simulation_->max_time_slice_usecs(), 30000000);
  EXPECT_EQ(simulation_->max_memory_slice_bytes(), 0);
}

TEST_F(HeatMapSimulationTest, SmallMemorySliceSize) {
  static const uint32 expected_size = 2;
  static const uint32 expected_times[expected_size] = {10000000, 30000000};
  static const uint32 expected_totals[expected_size] = {22, 5};

  HeatMapSimulation::TimeSlice::SliceQtyMap expected_slices[expected_size];
  expected_slices[0][0] = 2;
  expected_slices[0][1] = 2;
  expected_slices[0][2] = 4;
  expected_slices[0][3] = 1;
  expected_slices[0][4] = 1;
  expected_slices[0][5] = 1;
  expected_slices[0][10] = 4;
  expected_slices[0][11] = 3;
  expected_slices[0][12] = 3;
  expected_slices[0][13] = 1;
  expected_slices[1][2] = 1;
  expected_slices[1][3] = 1;
  expected_slices[1][4] = 1;
  expected_slices[1][5] = 1;
  expected_slices[1][6] = 1;

  ASSERT_EQ(arraysize(expected_times), expected_size);
  ASSERT_EQ(arraysize(expected_totals), expected_size);
  ASSERT_EQ(arraysize(expected_slices), expected_size);

  simulation_->set_memory_slice_bytes(1);

  CheckSimulationResult(
      expected_size, expected_times, expected_totals, expected_slices);

  EXPECT_EQ(simulation_->max_time_slice_usecs(), 30000000);
  EXPECT_EQ(simulation_->max_memory_slice_bytes(), 13);
}

TEST_F(HeatMapSimulationTest, BigTimeSliceSize) {
  static const uint32 expected_size = 1;
  static const uint32 expected_times[expected_size] = {0};
  static const uint32 expected_totals[expected_size] = {9};

  HeatMapSimulation::TimeSlice::SliceQtyMap expected_slices[expected_size];
  expected_slices[0][0] = 9;

  ASSERT_EQ(arraysize(expected_times), expected_size);
  ASSERT_EQ(arraysize(expected_totals), expected_size);
  ASSERT_EQ(arraysize(expected_slices), expected_size);

  simulation_->set_time_slice_usecs(40000000);

  CheckSimulationResult(
      expected_size, expected_times, expected_totals, expected_slices);

  EXPECT_EQ(simulation_->max_time_slice_usecs(), 0);
  EXPECT_EQ(simulation_->max_memory_slice_bytes(), 0);
}

TEST_F(HeatMapSimulationTest, BigTimeSliceSizeSmallMemorySliceSize) {
  static const uint32 expected_size = 1;
  static const uint32 expected_times[expected_size] = {0};
  static const uint32 expected_totals[expected_size] = {27};

  HeatMapSimulation::TimeSlice::SliceQtyMap expected_slices[expected_size];
  expected_slices[0][0] = 2;
  expected_slices[0][1] = 2;
  expected_slices[0][2] = 5;
  expected_slices[0][3] = 2;
  expected_slices[0][4] = 2;
  expected_slices[0][5] = 2;
  expected_slices[0][6] = 1;
  expected_slices[0][10] = 4;
  expected_slices[0][11] = 3;
  expected_slices[0][12] = 3;
  expected_slices[0][13] = 1;

  ASSERT_EQ(arraysize(expected_times), expected_size);
  ASSERT_EQ(arraysize(expected_totals), expected_size);
  ASSERT_EQ(arraysize(expected_slices), expected_size);

  simulation_->set_memory_slice_bytes(1);
  simulation_->set_time_slice_usecs(40000000);

  CheckSimulationResult(
      expected_size, expected_times, expected_totals, expected_slices);

  EXPECT_EQ(simulation_->max_time_slice_usecs(), 0);
  EXPECT_EQ(simulation_->max_memory_slice_bytes(), 13);
}

TEST_F(HeatMapSimulationTest, RandomInput) {
  // Using a blocks_ and its respective output,
  // generate several other random inputs that should result in the
  // same output and test HeatMapSimulation with them.
  static const uint32 expected_size = 2;
  static const uint32 expected_times[expected_size] = {10000000, 30000000};
  static const uint32 expected_totals[expected_size] = {22, 5};

  HeatMapSimulation::TimeSlice::SliceQtyMap expected_slices[expected_size];
  expected_slices[0][0] = 2;
  expected_slices[0][1] = 2;
  expected_slices[0][2] = 4;
  expected_slices[0][3] = 1;
  expected_slices[0][4] = 1;
  expected_slices[0][5] = 1;
  expected_slices[0][10] = 4;
  expected_slices[0][11] = 3;
  expected_slices[0][12] = 3;
  expected_slices[0][13] = 1;
  expected_slices[1][2] = 1;
  expected_slices[1][3] = 1;
  expected_slices[1][4] = 1;
  expected_slices[1][5] = 1;
  expected_slices[1][6] = 1;

  ASSERT_EQ(arraysize(expected_times), expected_size);
  ASSERT_EQ(arraysize(expected_totals), expected_size);
  ASSERT_EQ(arraysize(expected_slices), expected_size);

  for (uint32 i = 0; i < 100; i++) {
    // Generate a random input that should have the same output than blocks_.
    MockBlockInfoList random_input = GenerateRandomInput();

    std::stringstream s;
    s << "Failed with input: ";
    for (uint32 i = 0; i < random_input.size(); i++) {
      s << '(' << random_input[i].time << ", " << random_input[i].start;
      s << ", " << random_input[i].size << "), ";
    }

    // Test simulation_ with this input.
    simulation_.reset(new HeatMapSimulation());
    ASSERT_TRUE(simulation_ != NULL);

    simulation_->OnProcessStarted(Time::FromTimeT(1), 0);
    simulation_->set_memory_slice_bytes(1);
    simulation_->set_time_slice_usecs(1);

    CheckSimulationResult(
        expected_size, expected_times, expected_totals, expected_slices);

    EXPECT_EQ(simulation_->max_time_slice_usecs(), 30000000);
    EXPECT_EQ(simulation_->max_memory_slice_bytes(), 13);

    ASSERT_FALSE(testing::Test::HasNonfatalFailure()) << s.str();
  }
}

}  // namespace simulate
