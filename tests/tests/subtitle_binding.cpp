// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include "../../src/subtitle_binding.h"

TEST(subtitle_binding, changes_one_group_once_and_preserves_starts) {
    std::vector<BoundSubtitleTiming> rows{{"a", 1000, 100, 1600, true}, {"a", 2000, 500, 2500}, {"b", 1000, 0, 1000}};
    EXPECT_TRUE(SynchronizeBoundSubtitleDurations(rows));
    EXPECT_EQ(1600, rows[0].end);
    EXPECT_EQ(3000, rows[1].end);
    EXPECT_EQ(500, rows[1].start);
    EXPECT_EQ(1000, rows[2].end);
}

TEST(subtitle_binding, multi_selection_does_not_double_apply) {
    std::vector<BoundSubtitleTiming> rows{{"a", 1000, 0, 1500}, {"a", 2000, 100, 2600}, {"a", 500, 500, 1000}};
    EXPECT_TRUE(SynchronizeBoundSubtitleDurations(rows));
    EXPECT_EQ(1500, rows[0].end);
    EXPECT_EQ(2600, rows[1].end);
    EXPECT_EQ(1500, rows[2].end);
}

TEST(subtitle_binding, preferred_driver_and_common_shortening_limit) {
    std::vector<BoundSubtitleTiming> rows{{"a", 1000, 0, 1800}, {"a", 2000, 0, 2500, true}, {"a", 100, 100, 200}};
    SynchronizeBoundSubtitleDurations(rows);
    EXPECT_EQ(1500, rows[0].end);
    EXPECT_EQ(700, rows[2].end);
    rows = {{"a", 1000, 0, 100, true}, {"a", 200, 500, 700}};
    SynchronizeBoundSubtitleDurations(rows);
    EXPECT_EQ(800, rows[0].end);
    EXPECT_EQ(500, rows[1].end);
}

TEST(subtitle_binding, pure_shift_does_not_shift_other_members_and_limits_ass_time) {
    std::vector<BoundSubtitleTiming> rows{{"a", 1000, 500, 1500, true}, {"a", 2000, 0, 2000}};
    EXPECT_FALSE(SynchronizeBoundSubtitleDurations(rows));
    EXPECT_EQ(2000, rows[1].end);
    rows = {{"a", 1000, 0, 3000, true}, {"a", 1000, 35997990, 35998990}};
    SynchronizeBoundSubtitleDurations(rows);
    EXPECT_EQ(2000, rows[0].end);
    EXPECT_EQ(35999990, rows[1].end);
}
