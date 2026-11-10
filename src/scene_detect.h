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
	/// How different two frames must be, 0–1, to be a cut
	double threshold = 0.25;
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

constexpr int thumb_w = 64, thumb_h = 36, hist_bins = 16;

/// What a frame is compared by: a small grey picture and its colours
struct Signature {
	std::array<uint8_t, thumb_w * thumb_h> luma{};
	/// Per channel (B, G, R) a histogram summing to 1
	std::array<float, 3 * hist_bins> hist{};
};

/// A BGRA frame's signature, whatever its pitch and however it is stored
Signature Sign(VideoFrame const& frame);

/// 0 for the same picture, 1 for wholly unlike ones
double Difference(Signature const& a, Signature const& b);

/// The cuts among frames, frame 0 not included.
/// @param diffs  diffs[i]: frame i against frame i - 1 (diffs[0] unused)
/// @param across across[i]: the least difference between frame i - 1 and
///               any of the flash_span frames after i, and between frame i
///               and any of the frames before i - 1; low when frame i or
///               i - 1 is a flash, the picture being the same either side
std::vector<int> FindCuts(std::vector<double> const& diffs, std::vector<double> const& across, Params const& params);

/// Frames are read this wide where the video can hand them out smaller:
/// five source pixels to each of the thumbnail's, averaged
constexpr int analysis_width = thumb_w * 5;

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

/// Frame 0 first, then each cut
std::vector<int> Cuts(Measures const& measures, Params const& params);

/// Measure then Cuts. Nothing if @p ps is cancelled.
std::vector<int> Detect(VideoProvider &video, agi::ProgressSink *ps, Params const& params);

/// Kept on disk for the next time. Saving throws as agi::io::Save does.
void SaveMeasures(agi::fs::path const& file, Measures const& measures);
/// Nothing when the file is missing, unreadable or of another form
Measures LoadMeasures(agi::fs::path const& file);

}
