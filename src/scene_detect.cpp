// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "scene_detect.h"

#include "include/aegisub/video_provider.h"
#include "video_frame.h"

#include <libaegisub/background_runner.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <thread>

namespace scene_detect {

Params ForSensitivity(int sensitivity) {
	Params p;
	p.threshold = sensitivity <= 0 ? 0.30 : sensitivity == 1 ? 0.15 : 0.08;
	p.gradual_threshold = sensitivity <= 0 ? 0.45 : sensitivity == 1 ? 0.30 : 0.20;
	return p;
}

Signature Sign(VideoFrame const& frame) {
	Signature s;
	int const w = frame.width, h = frame.height;
	if (w <= 0 || h <= 0) return s;
	constexpr int cells = thumb_w * thumb_h;
	std::array<uint32_t, 3 * cells> sum{};
	std::array<uint32_t, cells> count{};
	// A few dozen pixels to each cell of the thumbnail are plenty: every
	// fifth pixel each way of a 1080p frame
	int const step = std::max(1, std::min(w / (thumb_w * 6), h / (thumb_h * 6)));
	std::vector<int> cell_x;
	for (int x = 0; x < w; x += step) cell_x.push_back(x * thumb_w / w);
	for (int y = 0; y < h; y += step) {
		int const row = frame.flipped ? h - 1 - y : y;
		unsigned char const *p = frame.data.data() + static_cast<size_t>(row) * frame.pitch;
		int const cell_row = y * thumb_h / h * thumb_w;
		for (size_t i = 0; i < cell_x.size(); ++i) {
			unsigned char const *px = p + i * step * 4;
			int const cell = cell_row + cell_x[i];
			sum[3 * cell] += px[0];
			sum[3 * cell + 1] += px[1];
			sum[3 * cell + 2] += px[2];
			++count[cell];
		}
	}
	for (int i = 0; i < cells; ++i) {
		// A frame narrower or shorter than the thumbnail leaves cells
		// empty: they take the cell before's colour
		if (!count[i]) {
			if (i) for (int c = 0; c < 3; ++c) s.colour[3 * i + c] = s.colour[3 * (i - 1) + c];
			continue;
		}
		for (int c = 0; c < 3; ++c)
			s.colour[3 * i + c] = static_cast<uint8_t>(sum[3 * i + c] / count[i]);
	}
	return s;
}

namespace {
/// How far cell @p ia of @p a is from cell @p ib of @p b, in its channel that moved most
inline int Change(Signature const& a, int ia, Signature const& b, int ib) {
	auto const *p = &a.colour[3 * ia], *q = &b.colour[3 * ib];
	return std::max({std::abs(int(p[0]) - int(q[0])), std::abs(int(p[1]) - int(q[1])), std::abs(int(p[2]) - int(q[2]))});
}

/// A cell is near black in all its channels
inline bool Black(Signature const& s, int i) {
	auto const *p = &s.colour[3 * i];
	return std::max({p[0], p[1], p[2]}) <= 28;
}

/// Where a dissolve's ends may be: worth working out whether the camera moved
constexpr double least_far = 0.15;
/// A frame between is a mix of the two when this share of what changed or less is not
constexpr double blend_share = 0.3;
/// The camera moving explains the change when moving the picture leaves
/// less than this share of it
constexpr double moved_share = 0.5;
}

Area FindPicture(std::vector<Signature> const& samples) {
	Area whole;
	if (samples.empty()) return whole;
	auto black_row = [&](int y) {
		int black = 0;
		for (auto const& s : samples)
			for (int x = 0; x < thumb_w; ++x) black += Black(s, y * thumb_w + x);
		return black * 10 >= 9 * thumb_w * static_cast<int>(samples.size());
	};
	auto black_column = [&](int x) {
		int black = 0;
		for (auto const& s : samples)
			for (int y = 0; y < thumb_h; ++y) black += Black(s, y * thumb_w + x);
		return black * 10 >= 9 * thumb_h * static_cast<int>(samples.size());
	};
	Area a;
	while (a.top < thumb_h && black_row(a.top)) ++a.top;
	while (a.bottom > a.top && black_row(a.bottom - 1)) --a.bottom;
	while (a.left < thumb_w && black_column(a.left)) ++a.left;
	while (a.right > a.left && black_column(a.right - 1)) --a.right;
	// Dark all through, rather than bars round a picture
	if ((a.bottom - a.top) * 2 < thumb_h || (a.right - a.left) * 2 < thumb_w) return whole;
	return a;
}

Area FindPicture(VideoProvider &video, int samples) {
	int const n = video.GetFrameCount();
	std::vector<Signature> signs;
	VideoFrame frame;
	int const count = std::min(samples, n);
	for (int k = 0; k < count; ++k) {
		video.GetFrame(static_cast<int>((int64_t(n) * (2 * k + 1)) / (2 * count)), frame);
		signs.push_back(Sign(frame));
	}
	return FindPicture(signs);
}

Area WithoutSubtitles(Area picture, int percent) {
	int const cut = static_cast<int>(std::lround((picture.bottom - picture.top) * std::clamp(percent, 0, 90) / 100.0));
	picture.bottom = std::max(picture.top + 1, picture.bottom - cut);
	return picture;
}

double Difference(Signature const& a, Signature const& b, Area const& area) {
	int changed = 0;
	for (int y = area.top; y < area.bottom; ++y)
		for (int x = area.left; x < area.right; ++x) {
			int const i = y * thumb_w + x;
			changed += Change(a, i, b, i) >= changed_level;
		}
	return area.Cells() ? double(changed) / area.Cells() : 0.;
}

double BlendError(Signature const& a, Signature const& mid, Signature const& b, Area const& area) {
	int off = 0;
	for (int y = area.top; y < area.bottom; ++y)
		for (int x = area.left; x < area.right; ++x) {
			int const i = 3 * (y * thumb_w + x);
			// Twice the distance from half way, and how far apart the ends are
			int from_middle = 0, apart = 0;
			for (int c = 0; c < 3; ++c) {
				from_middle = std::max(from_middle, std::abs(2 * int(mid.colour[i + c]) - int(a.colour[i + c]) - int(b.colour[i + c])));
				apart = std::max(apart, std::abs(int(b.colour[i + c]) - int(a.colour[i + c])));
			}
			// In the middle half of the way from one end to the other, or
			// near both where they are alike. Near one end is not a mix: the
			// mixing has only begun, or the frame is of one shot or the other.
			off += from_middle > std::max(changed_level, apart / 2);
		}
	return area.Cells() ? double(off) / area.Cells() : 0.;
}

double MovedDifference(Signature const& a, Signature const& b, Area const& area) {
	int const w = area.right - area.left, h = area.bottom - area.top;
	double best = 1;
	for (int dy = -h / 3; dy <= h / 3; ++dy)
		for (int dx = -w / 3; dx <= w / 3; ++dx) {
			// Cell (x, y) of b against cell (x - dx, y - dy) of a
			int const x0 = std::max(area.left, area.left + dx), x1 = std::min(area.right, area.right + dx);
			int const y0 = std::max(area.top, area.top + dy), y1 = std::min(area.bottom, area.bottom + dy);
			int const overlap = (x1 - x0) * (y1 - y0);
			int const limit = static_cast<int>(best * overlap);
			int changed = 0;
			for (int y = y0; y < y1 && changed <= limit; ++y)
				for (int x = x0; x < x1; ++x)
					changed += Change(a, (y - dy) * thumb_w + (x - dx), b, y * thumb_w + x) >= changed_level;
			best = std::min(best, double(changed) / overlap);
		}
	return best;
}

std::vector<int> FindCuts(std::vector<double> const& diffs, std::vector<double> const& across, Params const& params) {
	struct Cut { int frame; double strength; };
	std::vector<Cut> cuts;
	int const n = static_cast<int>(diffs.size());
	std::vector<double> around;
	for (int i = 1; i < n; ++i) {
		double const d = diffs[i];
		if (d < params.threshold) continue;
		// Much more different than the frames around it: not motion
		around.clear();
		for (int j = std::max(1, i - params.window); j <= std::min(n - 1, i + params.window); ++j)
			if (j != i) around.push_back(diffs[j]);
		if (!around.empty()) {
			auto mid = around.begin() + around.size() / 2;
			std::nth_element(around.begin(), mid, around.end());
			if (d < params.ratio * *mid) continue;
		}
		// The picture is back within a few frames: a flash
		if (i < static_cast<int>(across.size()) && across[i] < params.threshold / 2) continue;
		if (i < params.min_scene) continue;
		if (!cuts.empty() && i - cuts.back().frame < params.min_scene) {
			if (d > cuts.back().strength) cuts.back() = {i, d};
			continue;
		}
		cuts.push_back({i, d});
	}
	std::vector<int> frames;
	for (auto const& c : cuts) frames.push_back(c.frame);
	return frames;
}

std::vector<Transition> FindTransitions(Measures const& m, Params const& params) {
	std::vector<Transition> found;
	int const n = static_cast<int>(std::min({m.diffs.size(), m.far_difference.size(), m.blend.size(), m.moved.size()}));
	auto mixed = [&](int i) {
		double const far_difference = m.far_difference[i];
		// A cut between them leaves the frame half way of one shot or the
		// other, not mixed: no need to look for one
		return far_difference >= params.gradual_threshold && m.blend[i] <= blend_share * far_difference && m.moved[i] >= moved_share * far_difference;
	};
	for (int i = gradual_span; i < n; ++i) {
		if (!mixed(i)) continue;
		// The windows over one dissolve: from this one on while they are
		// mixed, past a few that are not: near its ends a window can take in
		// so little of the dissolve that its frames differ by little, and a
		// window with too little of one shot is not mixed either
		int last = i;
		double strength = m.far_difference[i];
		for (int j = i + 1; j < n && j <= last + gradual_span / 2; ++j) {
			if (!mixed(j)) continue;
			last = j;
			strength = std::max(strength, m.far_difference[j]);
		}
		// Each window's middle is half its span back from its end
		int const frame = (i + last + 1) / 2 - gradual_span / 2;
		i = last;
		if (frame < params.min_scene) continue;
		if (!found.empty() && frame - found.back().frame < params.min_scene) {
			if (strength > found.back().strength) found.back() = {frame, strength};
			continue;
		}
		found.push_back({frame, strength});
	}
	return found;
}

namespace {
/// Fill diffs[s, e) and across[s, e), reading frames [s - span - 1, e + span)
/// so that the frames each side of the range are compared as well: the same
/// as reading the whole video through. Read frames are counted in @p read;
/// stops early, leaving the rest unfilled, once @p stop is set.
void MeasureRange(VideoProvider &video, int n, int span, Area const& area, int s, int e,
                  Measures &m, std::atomic<int> &read, std::atomic<bool> const& stop) {
	int const first = std::max(0, s - std::max(span + 1, gradual_span)), last = std::min(n, e + span);
	// The signatures of the last few frames, frame j at j % ring
	int const ring = std::max(span + 2, gradual_span + 1);
	std::vector<Signature> recent(ring);
	auto mine = [&](int i) { return i >= s && i < e; };
	auto &diffs = m.diffs, &across = m.across;
	VideoFrame frame;
	for (int j = first; j < last; ++j) {
		if (stop) return;
		video.GetFrame(j, frame);
		if (mine(j)) ++read;
		auto &sig = recent[j % ring] = Sign(frame);
		if (j == first) continue;
		if (mine(j)) diffs[j] = Difference(recent[(j - 1) % ring], sig, area);
		// Frame j - k - 1 against this one: is j - k - 1 back after a flash
		// at j - k ...
		for (int k = 1; k <= span && j - k - 1 >= first; ++k) {
			int const i = j - k;
			if (mine(i)) across[i] = std::min(across[i], Difference(recent[(i - 1) % ring], sig, area));
		}
		// ... and is this frame one from before a flash at j - 1
		if (mine(j))
			for (int m2 = 2; m2 <= span + 1 && j - m2 >= first; ++m2)
				across[j] = std::min(across[j], Difference(recent[(j - m2) % ring], sig, area));
		// The ends of a dissolve or fade, if this frame is one's end
		if (mine(j) && j - gradual_span >= first) {
			auto const& start = recent[(j - gradual_span) % ring];
			double const far_difference = m.far_difference[j] = Difference(start, sig, area);
			double const blend = m.blend[j] = BlendError(start, recent[(j - gradual_span / 2) % ring], sig, area);
			if (far_difference >= least_far && blend <= blend_share * far_difference)
				m.moved[j] = MovedDifference(start, sig, area);
		}
	}
}
}

Measures Measure(VideoProvider &video, agi::ProgressSink *ps, int flash_span, Area const& area) {
	return Measure(std::vector<VideoProvider*>{&video}, ps, flash_span, area);
}

Measures Measure(std::vector<VideoProvider*> const& videos, agi::ProgressSink *ps, int flash_span, Area const& area) {
	int const n = videos.empty() ? 0 : videos[0]->GetFrameCount();
	int const span = std::max(flash_span, 1);
	Measures m;
	m.flash_span = span;
	m.area = area;
	if (n <= 0) {
		m.diffs.assign(1, 0.);
		m.across.assign(1, std::numeric_limits<double>::infinity());
		m.far_difference.assign(1, 0.);
		m.blend.assign(1, 1.);
		m.moved.assign(1, 1.);
		return m;
	}
	m.diffs.assign(n, 0.);
	m.across.assign(n, std::numeric_limits<double>::infinity());
	m.far_difference.assign(n, 0.);
	m.blend.assign(n, 1.);
	m.moved.assign(n, 1.);
	// Parts no shorter than a few seconds: each reads a little either side
	// of its own frames, and starts by seeking
	int const parts = std::clamp(n / min_part_frames, 1, static_cast<int>(videos.size()));
	std::atomic<int> read{0};
	std::atomic<bool> stop{false};
	std::vector<std::exception_ptr> errors(parts);
	auto part = [&](int k) {
		try {
			MeasureRange(*videos[k], n, span, area, int(int64_t(n) * k / parts), int(int64_t(n) * (k + 1) / parts),
			             m, read, stop);
		}
		catch (...) {
			errors[k] = std::current_exception();
			stop = true;
		}
	};
	if (parts == 1 && !ps)
		part(0);
	else {
		std::vector<std::thread> threads;
		for (int k = 0; k < parts; ++k) threads.emplace_back(part, k);
		// This thread reports how far the parts have got, and passes on a cancel
		while (true) {
			int const r = read;
			if (ps) {
				if (ps->IsCancelled()) stop = true;
				ps->SetProgress(r, n);
			}
			if (r >= n || stop) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		for (auto &t : threads) t.join();
	}
	for (auto const& e : errors)
		if (e) std::rethrow_exception(e);
	if (stop) return {};
	if (ps) ps->SetProgress(n, n);
	return m;
}

std::vector<int> Cuts(Measures const& measures, Params const& params) {
	std::vector<int> result{0};
	auto cuts = FindCuts(measures.diffs, measures.across, params);
	result.insert(result.end(), cuts.begin(), cuts.end());
	if (params.gradual) {
		// A short dissolve changes each frame enough for its frames to look
		// like cuts: those within it are it, at its middle
		auto transitions = FindTransitions(measures, params);
		int const reach = std::max(params.min_scene, gradual_span / 2);
		std::vector<int> kept{0};
		for (int c : cuts)
			if (std::none_of(transitions.begin(), transitions.end(), [&](Transition const& t) { return std::abs(c - t.frame) <= reach; }))
				kept.push_back(c);
		for (auto const& t : transitions) kept.push_back(t.frame);
		std::sort(kept.begin(), kept.end());
		return kept;
	}
	return result;
}

std::vector<int> Detect(VideoProvider &video, agi::ProgressSink *ps, Params const& params) {
	auto measures = Measure(video, ps, params.flash_span);
	if (measures.empty()) return {};
	return Cuts(measures, params);
}

namespace {
// The file: this tag; the flash span, the frame count and the area's left,
// right, top and bottom as 32-bit integers; then each frame's difference,
// across, far, blend and moved as 32-bit floats. It is read back only on
// the computer that wrote it.
char const measures_tag[8] = {'A', 'g', 'S', 'c', 'n', 'M', '0', '3'};
constexpr int per_frame = 5;
}

void SaveMeasures(agi::fs::path const& file, Measures const& m) {
	agi::io::Save save(file, true);
	auto &out = save.Get();
	out.write(measures_tag, sizeof measures_tag);
	int32_t const head[6] = {m.flash_span, static_cast<int32_t>(m.diffs.size()), m.area.left, m.area.right, m.area.top, m.area.bottom};
	out.write(reinterpret_cast<char const*>(head), sizeof head);
	std::vector<float> row(per_frame * m.diffs.size());
	auto at = [](std::vector<double> const& v, size_t i, double otherwise) {
		return static_cast<float>(i < v.size() ? v[i] : otherwise);
	};
	for (size_t i = 0; i < m.diffs.size(); ++i) {
		row[per_frame * i] = static_cast<float>(m.diffs[i]);
		row[per_frame * i + 1] = at(m.across, i, std::numeric_limits<double>::infinity());
		row[per_frame * i + 2] = at(m.far_difference, i, 0);
		row[per_frame * i + 3] = at(m.blend, i, 1);
		row[per_frame * i + 4] = at(m.moved, i, 1);
	}
	out.write(reinterpret_cast<char const*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
}

Measures LoadMeasures(agi::fs::path const& file) {
	try {
		if (!agi::fs::FileExists(file)) return {};
		auto in = agi::io::Open(file, true);
		char tag[sizeof measures_tag];
		int32_t head[6];
		if (!in->read(tag, sizeof tag) || std::memcmp(tag, measures_tag, sizeof tag)) return {};
		if (!in->read(reinterpret_cast<char*>(head), sizeof head)) return {};
		if (head[0] < 1 || head[1] < 1) return {};
		Area area{head[2], head[3], head[4], head[5]};
		if (area.left < 0 || area.right > thumb_w || area.top < 0 || area.bottom > thumb_h || area.Cells() <= 0) return {};
		std::vector<float> row(per_frame * static_cast<size_t>(head[1]));
		if (!in->read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)))) return {};
		Measures m;
		m.flash_span = head[0];
		m.area = area;
		for (auto v : {&m.diffs, &m.across, &m.far_difference, &m.blend, &m.moved}) v->resize(head[1]);
		for (int32_t i = 0; i < head[1]; ++i) {
			m.diffs[i] = row[per_frame * i];
			m.across[i] = row[per_frame * i + 1];
			m.far_difference[i] = row[per_frame * i + 2];
			m.blend[i] = row[per_frame * i + 3];
			m.moved[i] = row[per_frame * i + 4];
		}
		return m;
	}
	catch (agi::Exception const&) {
		return {};
	}
}

}
