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

TEST(shape_drawing, reads_arbitrary_drawing_control_bounds_without_rewriting_paths) {
    for (auto shape : {DrawingShape::Rectangle, DrawingShape::Ellipse, DrawingShape::Triangle}) {
        auto bounds = ParseDrawingBounds(ShapeDrawingPath(shape, 120, 60));
        ASSERT_TRUE(bounds);
        EXPECT_DOUBLE_EQ(120, bounds->Width());
        EXPECT_DOUBLE_EQ(60, bounds->Height());
    }
    auto compound = ParseDrawingBounds("m -20 -10 l 100 0 40 50 m 0 0 b 10 -20 20 80 70 40");
    ASSERT_TRUE(compound);
    EXPECT_DOUBLE_EQ(-20, compound->left);
    EXPECT_DOUBLE_EQ(-20, compound->top);
    EXPECT_DOUBLE_EQ(120, compound->Width());
    EXPECT_DOUBLE_EQ(100, compound->Height());
    EXPECT_TRUE(ParseDrawingBounds("m0 0 s 10 0 20 20 30 0 p 40 20 c"));
    EXPECT_TRUE(ParseDrawingBounds("m 0 0 l 20 20 n 30 30 l 50 0"));
    for (auto invalid : {"", "text", "m 0 0", "m 0 0 l 10", "m 0 0 l 10 0",
        "m 0 0 b 10 10", "m 0 0 s 10 10", "m 0 0 p 10 10", "m 0 0 c",
        "l 0 0 10 10", "m 0 0 l 10 20 garbage", "m 0 0 l 1e999 20", "m 0 0 l 1000001 20"})
        EXPECT_FALSE(ParseDrawingBounds(invalid)) << invalid;
}

TEST(shape_drawing, scales_the_actual_control_points_without_changing_commands) {
    EXPECT_EQ("m 0 0 l 180 0 180 60 0 60",
        ScaleDrawingPath("m 0 0 l 120 0 120 60 0 60", 1.5, 1));
    EXPECT_EQ("m 0 0 l 60 0 60 30 0 30",
        ScaleDrawingPath("m 0 0 l 120 0 120 60 0 60", .5, .5));
    EXPECT_EQ("m -10 -5 l 6.25 0 6.25 3 -10 3",
        ScaleDrawingPath("m -20 -10 l 12.5 0 12.5 6 -20 6", .5, .5));

    auto compound = ScaleDrawingPath("m -20 -10 l 100 0 40 50 m 0 0 b 10 -20 20 80 70 40", 2, .5);
    auto bounds = ParseDrawingBounds(compound);
    ASSERT_TRUE(bounds);
    EXPECT_DOUBLE_EQ(240, bounds->Width());
    EXPECT_DOUBLE_EQ(50, bounds->Height());

    auto spline = ScaleDrawingPath("m 0 0 s 10 0 20 20 30 0 p 40 20 c", 2, 2);
    EXPECT_EQ("m 0 0 s 20 0 40 40 60 0 p 80 40 c", spline);
    auto multiple = ScaleDrawingPath("m 0 0 l 10 0 10 10 n 50 50 l 60 50", 2, 3);
    EXPECT_EQ("m 0 0 l 20 0 20 30 n 100 150 l 120 150", multiple);

    for (auto invalid : {"", "text", "m 0 0 l 10", "m 0 0 b 10 10", "m 0 0 s 10 0",
        "m 0 0 c", "m 0 0 l 10 20 garbage", "l 0 0 10 10"})
        EXPECT_TRUE(ScaleDrawingPath(invalid, 2, 2).empty()) << invalid;
    EXPECT_TRUE(ScaleDrawingPath("m 0 0 l 10 0 10 10", 0, 1).empty());
    EXPECT_TRUE(ScaleDrawingPath("m 0 0 l 10 0 10 10", 1, -1).empty());
    EXPECT_TRUE(ScaleDrawingPath("m 0 0 l 1e999 0", 1, 1).empty());
}
