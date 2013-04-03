// Copyright 2013 Google Inc. All Rights Reserved.
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

#include "syzygy/trace/common/clock.h"

#include "gtest/gtest.h"

namespace trace {
namespace common {

TEST(TimerToFileTimeTest, FailsForInvalidTimerInfo) {
  FILETIME ft1 = {};
  TimerInfo ti = {};
  FILETIME ft2 = {};
  EXPECT_FALSE(TimerToFileTime(ft1, ti, 0, 0, &ft2));
}

TEST(TimerToFileTimeTest, FailsForLargeNegativeInterval) {
  FILETIME ft1 = {};
  TimerInfo ti = {};
  FILETIME ft2 = {};

  // This should fail as -100 s is not representable starting at a filetime of
  // 0.
  ti.frequency = 1;
  ti.resolution = 1;
  EXPECT_FALSE(TimerToFileTime(ft1, ti, 100, 0, &ft2));
}

TEST(TimerToFileTimeTest, Identity) {
  FILETIME ft1 = { 0xBAAD, 0xCAFE };
  TimerInfo ti = {};
  FILETIME ft2 = {};

  ti.frequency = 1;
  ti.resolution = 1;
  EXPECT_TRUE(TimerToFileTime(ft1, ti, 0, 0, &ft2));
  EXPECT_EQ(ft1.dwLowDateTime, ft2.dwLowDateTime);
  EXPECT_EQ(ft1.dwHighDateTime, ft2.dwHighDateTime);
}

TEST(TimerToFileTimeTest, PositiveInterval) {
  FILETIME ft1 = {};
  TimerInfo ti = {};
  FILETIME ft2 = {};

  ft1.dwLowDateTime = 0x10000;

  // This corresponds to 100ns ticks, which is the same precision as the
  // underlying filetime.
  ti.frequency = 10e6;
  ti.resolution = 1;

  // We expect the filetime to have increased by 100 intervals.
  EXPECT_TRUE(TimerToFileTime(ft1, ti, 200, 300, &ft2));
  EXPECT_EQ(0u, ft2.dwHighDateTime);
  EXPECT_EQ(ft1.dwLowDateTime + 100, ft2.dwLowDateTime);
}

TEST(TimerToFileTimeTest, NegativeInterval) {
  FILETIME ft1 = {};
  TimerInfo ti = {};
  FILETIME ft2 = {};

  ft1.dwLowDateTime = 0x10000;

  // This corresponds to 100ns ticks, which is the same precision as the
  // underlying filetime.
  ti.frequency = 10e6;
  ti.resolution = 1;

  // We expect the filetime to have decreased by 100 intervals.
  EXPECT_TRUE(TimerToFileTime(ft1, ti, 300, 200, &ft2));
  EXPECT_EQ(0u, ft2.dwHighDateTime);
  EXPECT_EQ(ft1.dwLowDateTime - 100, ft2.dwLowDateTime);
}

TEST(ClockInfoTest, GetClockInfo) {
  ClockInfo ci = {};
  GetClockInfo(&ci);

  EXPECT_EQ(1000u, ci.ticks_info.frequency);
  EXPECT_LT(0u, ci.ticks_info.resolution);

  // We have no precise expectations about TSC info, except that both entries
  // are zero or they are both non-zero.
  if (ci.tsc_info.resolution == 0) {
    EXPECT_EQ(0u, ci.tsc_info.frequency);
  } else {
    EXPECT_EQ(1u, ci.tsc_info.resolution);
    EXPECT_LT(0u, ci.tsc_info.frequency);
  }
}

TEST(TicksToFileTimeTest, WorksAsExpected) {
  ClockInfo ci = {};
  FILETIME ft = {};

  EXPECT_FALSE(TicksToFileTime(ci, 100, &ft));

  // 100 ms is 1e6 100ns intervals.
  ci.ticks_info.frequency = 1000;
  ci.ticks_info.resolution = 1;
  EXPECT_TRUE(TicksToFileTime(ci, 100, &ft));
  EXPECT_EQ(1e6, ft.dwLowDateTime);
  EXPECT_EQ(0, ft.dwHighDateTime);
}

TEST(TscToFileTimeTest, WorksAsExpected) {
  ClockInfo ci = {};
  FILETIME ft = {};

  EXPECT_FALSE(TscToFileTime(ci, 100, &ft));

  // 100 ms is 1e6 100ns intervals.
  ci.tsc_info.frequency = 1000;
  ci.tsc_info.resolution = 1;
  EXPECT_TRUE(TscToFileTime(ci, 100, &ft));
  EXPECT_EQ(1e6, ft.dwLowDateTime);
  EXPECT_EQ(0, ft.dwHighDateTime);
}

}  // namespace common
}  // namespace trace
