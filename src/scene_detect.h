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
/// (docs/scene-detection.md), at once or by a dissolve or a fade. Flashes
/// and motion within a shot are not cuts.
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
	/// Dissolves and fades are found too, at their middle
	bool gradual = true;
	/// How different the frames either side of one must be, as threshold
	double gradual_threshold = 0.30;
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

/// The cells compared, [left, right) × [top, bottom): the picture, without
/// the black bars round it nor the band burnt-in subtitles are in, which
/// change when the shot does not
struct Area {
	int left = 0, right = thumb_w, top = 0, bottom = thumb_h;
	int Cells() const { return (right - left) * (bottom - top); }
	bool operator==(Area const&) const = default;
};

/// The picture inside the black bars of the frames sampled: the rows and
/// columns along the edges whose cells are near black in nine of ten of
/// them. The whole frame when that would leave less than half of it.
Area FindPicture(std::vector<Signature> const& samples);
/// The same, from @p samples frames spread through @p video
Area FindPicture(VideoProvider &video, int samples = 24);
/// @p picture without its bottom @p percent, where subtitles are burnt in
Area WithoutSubtitles(Area picture, int percent);

/// The share of the cells whose colour changed: 0 for the same picture, 1
/// when every part of it is another colour. Motion moves only the cells
/// along the edges of what moves; a dissolve moves every cell a little each
/// frame; another shot moves much of the picture at once, even when it is
/// of the same place in the same colours.
double Difference(Signature const& a, Signature const& b, Area const& area = {});

/// Dissolves and fades are looked for between frames this far apart
constexpr int gradual_span = 12;

/// The share of the cells of @p mid not in the middle half of the way from
/// @p a to @p b (or, where those are alike, not near them): low when @p mid
/// is the two mixed half and half or so, as a dissolve's middle is (a fade
/// is one with black)
double BlendError(Signature const& a, Signature const& mid, Signature const& b, Area const& area = {});

/// The least difference between @p a moved by whole cells, up to a third
/// of the picture each way, and @p b, over where they overlap: low when
/// the camera moved, which can look like a dissolve
double MovedDifference(Signature const& a, Signature const& b, Area const& area = {});

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
	/// The cells compared
	Area area;
	/// As FindCuts takes them
	std::vector<double> diffs, across;
	/// far[i]: frame i against frame i - gradual_span; blend[i]: the
	/// BlendError of the frame half way between; moved[i]: their
	/// MovedDifference, worked out only where the two may be a dissolve's
	/// ends (1 elsewhere)
	std::vector<double> far, blend, moved;
	bool empty() const { return diffs.empty(); }
};

/// Where a dissolve or fade is, found from the measures, and how different
/// the frames either side of it are
struct Transition {
	int frame;
	double strength;
};
/// The dissolves and fades, at the frame half way through each: frames
/// gradual_span apart that are very different, the one between them a mix
/// of the two (which it is not with a cut between them), and not the
/// camera moving.
std::vector<Transition> FindTransitions(Measures const& measures, Params const& params);

/// Read every frame of @p video in order, comparing @p area. Nothing if @p ps is cancelled.
Measures Measure(VideoProvider &video, agi::ProgressSink *ps, int flash_span, Area const& area = {});

/// A part shorter than this many frames is not worth a reader of its own
constexpr int min_part_frames = 240;

/// The same, the video cut into as many parts as there are @p videos (all
/// of them the same video, each with its own decoder), each read through
/// on a thread of its own. The parts overlap by a few frames, so that
/// what is found is just what reading it through in one piece finds. A
/// reader's error is thrown once all have stopped.
Measures Measure(std::vector<VideoProvider*> const& videos, agi::ProgressSink *ps, int flash_span, Area const& area = {});

/// Frame 0 first, then each cut and, if @p params says so, each dissolve
/// and fade; a cut within one (a short dissolve's frames each change enough
/// to look like one) is taken for part of it
std::vector<int> Cuts(Measures const& measures, Params const& params);

/// Measure then Cuts. Nothing if @p ps is cancelled.
std::vector<int> Detect(VideoProvider &video, agi::ProgressSink *ps, Params const& params);

/// Kept on disk for the next time. Saving throws as agi::io::Save does.
void SaveMeasures(agi::fs::path const& file, Measures const& measures);
/// Nothing when the file is missing, unreadable or of another form
Measures LoadMeasures(agi::fs::path const& file);

}
