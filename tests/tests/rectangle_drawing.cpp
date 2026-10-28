// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include "../../src/rectangle_drawing.h"
#include "../../src/shape_drawing.h"

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

TEST(shape_drawing, creates_ass_paths_and_rejects_degenerate_shapes) {
    auto rectangle = ShapeDrawingPath(DrawingShape::Rectangle, 120, 60);
    ASSERT_TRUE(ParseRectangleDrawing(rectangle));
    EXPECT_EQ("m 60 0 l 120 60 0 60", ShapeDrawingPath(DrawingShape::Triangle, 120, 60));
    auto ellipse = ShapeDrawingPath(DrawingShape::Ellipse, 120, 60);
    EXPECT_EQ(0, ellipse.find("m 60 0 b "));
    std::istringstream coordinates(ellipse.substr(ellipse.find(" b ") + 3));
    double number;
    int count = 0;
    while (coordinates >> number) { EXPECT_TRUE(std::isfinite(number)); ++count; }
    EXPECT_EQ(24, count); // Four cubic arcs, each with three coordinate pairs.
    EXPECT_FALSE(ParseRectangleDrawing(ellipse));
    for (auto shape : {DrawingShape::Rectangle, DrawingShape::Ellipse, DrawingShape::Triangle}) {
        EXPECT_TRUE(ShapeDrawingPath(shape, 0, 60).empty());
        EXPECT_TRUE(ShapeDrawingPath(shape, 10, -1).empty());
    }
    EXPECT_EQ("m 0 0 l 120 0 60 30 120 60 0 60",
        PolygonDrawingPath({{0, 0}, {120, 0}, {60, 30}, {120, 60}, {0, 60}}));
    EXPECT_TRUE(PolygonDrawingPath({{0, 0}, {10, 10}}).empty());
    EXPECT_TRUE(PolygonDrawingPath({{0, 0}, {10, 10}, {20, 20}}).empty());
    EXPECT_TRUE(PolygonDrawingPath({{0, 0}, {0, 0}, {0, 0}}).empty());
}

TEST(shape_drawing, shift_preserves_proportions_and_video_bounds_in_all_directions) {
    for (auto shape : {DrawingShape::Rectangle, DrawingShape::Ellipse, DrawingShape::Triangle}) {
        for (auto end : {DrawingPoint{630, 350}, {10, 350}, {630, 10}, {10, 10}}) {
            DrawingPoint start{320, 180};
            auto p = ConstrainDrawingPoint(start, end, {640, 360}, shape);
            EXPECT_GE(p.x, 0); EXPECT_LE(p.x, 640);
            EXPECT_GE(p.y, 0); EXPECT_LE(p.y, 360);
            double ratio = shape == DrawingShape::Triangle ? std::sqrt(3.0) / 2 : 1;
            EXPECT_NEAR(std::abs(p.y - start.y), std::abs(p.x - start.x) * ratio, 1e-8);
        }
    }
    auto diagonal = ConstrainDrawingPoint({600, 100}, {640, 160}, {640, 360}, DrawingShape::Polygon);
    EXPECT_NEAR(640, diagonal.x, 1e-8);
    EXPECT_NEAR(diagonal.x - 600, diagonal.y - 100, 1e-8);
    auto horizontal = ConstrainDrawingPoint({100, 100}, {200, 105}, {640, 360}, DrawingShape::Polygon);
    EXPECT_NEAR(100, horizontal.y, 1e-8);
}
