// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <libaegisub/fs.h>

#include <array>
#include <cstdint>
#include <vector>

namespace agi { class ProgressSink; }
class VideoProvider;
struct VideoFrame;

/// Finds the frames where the picture cuts to another shot
/// (docs/scene-detection.md). Gradual transitions, flashes and motion within
/// a shot are not cuts.
namespace scene_detect {

struct Params {
	/// How different two frames must be, 0–1, to be a cut: the medium
	/// sensitivity's
	double threshold = 0.15;
	/// ... and how many times the difference around it
	double ratio = 3.0;
	/// Frames each side the difference around a frame is taken over
	int window = 8;
	/// Cuts closer together than this many frames: only the strongest is kept
	int min_scene = 6;
	/// A picture coming back within this many frames was a flash, not a cut
	int flash_span = 3;
};

/// The sensitivity option's three steps: low, medium, high
Params ForSensitivity(int sensitivity);

constexpr int thumb_w = 64, thumb_h = 36;

/// A cell whose colour moved this far, out of 255, in any channel changed
constexpr int changed_level = 20;

/// What a frame is compared by: the colour of each cell of a small picture
struct Signature {
	/// B, G, R of each cell, row by row
	std::array<uint8_t, thumb_w * thumb_h * 3> colour{};
};

/// A BGRA frame's signature, whatever its pitch and however it is stored
Signature Sign(VideoFrame const& frame);

/// The share of the cells whose colour changed: 0 for the same picture, 1
/// when every part of it is another colour. Motion moves only the cells
/// along the edges of what moves; a dissolve moves every cell a little each
/// frame; another shot moves much of the picture at once, even when it is
/// of the same place in the same colours.
double Difference(Signature const& a, Signature const& b);

/// The cuts among frames, frame 0 not included.
/// @param diffs  diffs[i]: frame i against frame i - 1 (diffs[0] unused)
/// @param across across[i]: the least difference between frame i - 1 and
///               any of the flash_span frames after i, and between frame i
///               and any of the frames before i - 1; low when frame i or
///               i - 1 is a flash, the picture being the same either side
std::vector<int> FindCuts(std::vector<double> const& diffs, std::vector<double> const& across, Params const& params);

/// Frames are read this wide where the video can hand them out smaller,
/// picked out rather than averaged: the pixels the thumbnail's cells are
/// made from, as they are in the full frame
constexpr int analysis_width = thumb_w * 6;

/// What reading a video through finds, before any frame is called a cut.
/// Only the flash span enters into it, so a change of sensitivity is
/// answered from it without reading the video again.
struct Measures {
	int flash_span = 0;
	/// As FindCuts takes them
	std::vector<double> diffs, across;
	bool empty() const { return diffs.empty(); }
};

/// Read every frame of @p video in order. Nothing if @p ps is cancelled.
Measures Measure(VideoProvider &video, agi::ProgressSink *ps, int flash_span);

/// A part shorter than this many frames is not worth a reader of its own
constexpr int min_part_frames = 240;

/// The same, the video cut into as many parts as there are @p videos (all
/// of them the same video, each with its own decoder), each read through
/// on a thread of its own. The parts overlap by a few frames, so that
/// what is found is just what reading it through in one piece finds. A
/// reader's error is thrown once all have stopped.
Measures Measure(std::vector<VideoProvider*> const& videos, agi::ProgressSink *ps, int flash_span);

/// Frame 0 first, then each cut
std::vector<int> Cuts(Measures const& measures, Params const& params);

/// Measure then Cuts. Nothing if @p ps is cancelled.
std::vector<int> Detect(VideoProvider &video, agi::ProgressSink *ps, Params const& params);

/// Kept on disk for the next time. Saving throws as agi::io::Save does.
void SaveMeasures(agi::fs::path const& file, Measures const& measures);
/// Nothing when the file is missing, unreadable or of another form
Measures LoadMeasures(agi::fs::path const& file);

}
