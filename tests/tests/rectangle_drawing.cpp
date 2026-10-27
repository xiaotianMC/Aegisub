// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include "../../src/rectangle_drawing.h"

TEST(rectangle_drawing, reads_created_rectangle_and_fractional_dimensions) {
    auto box = ParseRectangleDrawing("m 0 0 l 600 0 600 60 0 60");
    ASSERT_TRUE(box);
    EXPECT_EQ(600, box->first);
    EXPECT_EQ(60, box->second);
    box = ParseRectangleDrawing(" m 0 0 l 12.5 0 12.5 3.25 0 3.25  ");
    ASSERT_TRUE(box);
    EXPECT_EQ(12.5, box->first);
    EXPECT_EQ(3.25, box->second);
}

TEST(rectangle_drawing, rejects_other_shapes_and_unsafe_sizes) {
    for (auto path : {"", "hello", "m 0 0 l 600 0 600 60 0 61",
        "m 0 0 l 0 0 0 60 0 60", "m 0 0 l -2 0 -2 60 0 60",
        "m 1 0 l 600 0 600 60 0 60", "m 0 0 l 600 0 600 60 0 60 b 1 2 3 4 5 6",
        "m 0 0 l 1e999 0 1e999 60 0 60", "m 0 0 l 1000001 0 1000001 2 0 2"})
        EXPECT_FALSE(ParseRectangleDrawing(path)) << path;
}
