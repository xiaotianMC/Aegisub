// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The animation a line's override tags give it (docs/style-panel.md, the
// animation page): \move, \fad or \fade, \t and \org, read into values and
// written back. Times are in milliseconds from the line's start, as the
// tags hold them; the page shows them as times of the video.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace animation {
/// \move(x1,y1,x2,y2[,t1,t2]): from the first point to the second, between
/// t1 and t2; without them over the whole line
struct Move {
	double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	int t1 = 0, t2 = 0;
	bool operator==(Move const&) const = default;
};

/// \fade(a1,a2,a3,t1,t2,t3,t4): alpha a1 until t1, to a2 by t2, a2 until
/// t3, to a3 by t4. \fad(in,out) is the one fading in from the line's
/// start and out to its end: a1 = a3 = 255, a2 = 0, t1 = 0, t4 = the end.
struct Fade {
	int a1 = 255, a2 = 0, a3 = 255;
	int t1 = 0, t2 = 0, t3 = 0, t4 = 0;
	bool operator==(Fade const&) const = default;
	/// Whether \fad says it for a line @p duration long
	bool Simple(int duration) const;
};

/// One tag a \t changes to, e.g. name "\fs", value "80"
struct Target {
	std::string name, value;
	bool operator==(Target const&) const = default;
};

/// \t([t1,t2,][accel,]tags): the tags reached between t1 and t2, over the
/// whole line without them; accel above 1 starts slowly, below fast
struct Transform {
	int t1 = 0, t2 = 0;
	double accel = 1;
	std::vector<Target> targets;
	bool operator==(Transform const&) const = default;
};

/// \org(x,y): the point the line turns about
struct Origin {
	double x = 0, y = 0;
	bool operator==(Origin const&) const = default;
};

struct Animation {
	std::optional<Move> move;
	std::optional<Fade> fade;
	std::optional<Origin> origin;
	/// The \t of the block at the start of the line: those further on
	/// change only the text after them, and are left as they are
	std::vector<Transform> transforms;
	/// Where the line stands, from \pos, if it has one
	std::optional<std::pair<double, double>> position;
};

/// What @p text, a line @p duration ms long, does. Times not given are
/// filled in: a \move or \t over the whole line runs from 0 to @p duration.
Animation Read(std::string const& text, int duration);

/// @p text with its \move set to @p move, or none; \pos goes, as a line
/// can't have both. A move over the whole line is written without times.
std::string WriteMove(std::string const& text, std::optional<Move> const& move, int duration);
/// @p text with its \fad and \fade set to @p fade, or none: \fad when it
/// says it, \fade otherwise
std::string WriteFade(std::string const& text, std::optional<Fade> const& fade, int duration);
/// @p text with its \org set to @p origin, or none
std::string WriteOrigin(std::string const& text, std::optional<Origin> const& origin);
/// @p text with the \t of its first block set to @p transforms, after the
/// block's other tags; a \t over the whole line is written without times
std::string WriteTransforms(std::string const& text, std::vector<Transform> const& transforms, int duration);
/// @p text with \pos(@p x,@p y) and no \move
std::string WritePosition(std::string const& text, double x, double y);

/// The tags in @p tags ("\fs80\1c&H0000FF&") one by one
std::vector<Target> SplitTargets(std::string const& tags);
/// The tags of @p targets written one after another
std::string JoinTargets(std::vector<Target> const& targets);
/// One \t, as it is written
std::string TransformTag(Transform const& transform, int duration);

/// A number as tags have it: no more decimals than needed
std::string Number(double value);
}
