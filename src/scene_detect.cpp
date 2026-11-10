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

double Difference(Signature const& a, Signature const& b) {
	int changed = 0;
	for (size_t i = 0; i < a.colour.size(); i += 3) {
		int const d = std::max({std::abs(int(a.colour[i]) - int(b.colour[i])),
		                        std::abs(int(a.colour[i + 1]) - int(b.colour[i + 1])),
		                        std::abs(int(a.colour[i + 2]) - int(b.colour[i + 2]))});
		changed += d >= changed_level;
	}
	return double(changed) / (a.colour.size() / 3);
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

namespace {
/// Fill diffs[s, e) and across[s, e), reading frames [s - span - 1, e + span)
/// so that the frames each side of the range are compared as well: the same
/// as reading the whole video through. Read frames are counted in @p read;
/// stops early, leaving the rest unfilled, once @p stop is set.
void MeasureRange(VideoProvider &video, int n, int span, int s, int e,
                  std::vector<double> &diffs, std::vector<double> &across,
                  std::atomic<int> &read, std::atomic<bool> const& stop) {
	int const first = std::max(0, s - span - 1), last = std::min(n, e + span);
	// The signatures of the last span + 2 frames, frame j at j % ring
	int const ring = span + 2;
	std::vector<Signature> recent(ring);
	auto mine = [&](int i) { return i >= s && i < e; };
	VideoFrame frame;
	for (int j = first; j < last; ++j) {
		if (stop) return;
		video.GetFrame(j, frame);
		if (mine(j)) ++read;
		auto &sig = recent[j % ring] = Sign(frame);
		if (j == first) continue;
		if (mine(j)) diffs[j] = Difference(recent[(j - 1) % ring], sig);
		// Frame j - k - 1 against this one: is j - k - 1 back after a flash
		// at j - k ...
		for (int k = 1; k <= span && j - k - 1 >= first; ++k) {
			int const i = j - k;
			if (mine(i)) across[i] = std::min(across[i], Difference(recent[(i - 1) % ring], sig));
		}
		// ... and is this frame one from before a flash at j - 1
		if (mine(j))
			for (int m = 2; m <= span + 1 && j - m >= first; ++m)
				across[j] = std::min(across[j], Difference(recent[(j - m) % ring], sig));
	}
}
}

Measures Measure(VideoProvider &video, agi::ProgressSink *ps, int flash_span) {
	return Measure(std::vector<VideoProvider*>{&video}, ps, flash_span);
}

Measures Measure(std::vector<VideoProvider*> const& videos, agi::ProgressSink *ps, int flash_span) {
	int const n = videos.empty() ? 0 : videos[0]->GetFrameCount();
	int const span = std::max(flash_span, 1);
	Measures result;
	result.flash_span = span;
	if (n <= 0) {
		result.diffs.assign(1, 0.);
		result.across.assign(1, std::numeric_limits<double>::infinity());
		return result;
	}
	std::vector<double> diffs(n, 0.), across(n, std::numeric_limits<double>::infinity());
	// Parts no shorter than a few seconds: each reads a little either side
	// of its own frames, and starts by seeking
	int const parts = std::clamp(n / min_part_frames, 1, static_cast<int>(videos.size()));
	std::atomic<int> read{0};
	std::atomic<bool> stop{false};
	std::vector<std::exception_ptr> errors(parts);
	auto part = [&](int k) {
		try {
			MeasureRange(*videos[k], n, span, int(int64_t(n) * k / parts), int(int64_t(n) * (k + 1) / parts),
			             diffs, across, read, stop);
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
	result.diffs = std::move(diffs);
	result.across = std::move(across);
	return result;
}

std::vector<int> Cuts(Measures const& measures, Params const& params) {
	std::vector<int> result{0};
	auto cuts = FindCuts(measures.diffs, measures.across, params);
	result.insert(result.end(), cuts.begin(), cuts.end());
	return result;
}

std::vector<int> Detect(VideoProvider &video, agi::ProgressSink *ps, Params const& params) {
	auto measures = Measure(video, ps, params.flash_span);
	if (measures.empty()) return {};
	return Cuts(measures, params);
}

namespace {
// The file: this tag, then the flash span and the frame count as 32-bit
// integers, then each frame's difference and across as 32-bit floats. It
// is read back only on the computer that wrote it.
char const measures_tag[8] = {'A', 'g', 'S', 'c', 'n', 'M', '0', '2'};
}

void SaveMeasures(agi::fs::path const& file, Measures const& measures) {
	agi::io::Save save(file, true);
	auto &out = save.Get();
	out.write(measures_tag, sizeof measures_tag);
	int32_t const head[2] = {measures.flash_span, static_cast<int32_t>(measures.diffs.size())};
	out.write(reinterpret_cast<char const*>(head), sizeof head);
	std::vector<float> row(2 * measures.diffs.size());
	for (size_t i = 0; i < measures.diffs.size(); ++i) {
		row[2 * i] = static_cast<float>(measures.diffs[i]);
		row[2 * i + 1] = static_cast<float>(i < measures.across.size() ? measures.across[i] : std::numeric_limits<double>::infinity());
	}
	out.write(reinterpret_cast<char const*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
}

Measures LoadMeasures(agi::fs::path const& file) {
	try {
		if (!agi::fs::FileExists(file)) return {};
		auto in = agi::io::Open(file, true);
		char tag[sizeof measures_tag];
		int32_t head[2];
		if (!in->read(tag, sizeof tag) || std::memcmp(tag, measures_tag, sizeof tag)) return {};
		if (!in->read(reinterpret_cast<char*>(head), sizeof head)) return {};
		if (head[0] < 1 || head[1] < 1) return {};
		std::vector<float> row(2 * static_cast<size_t>(head[1]));
		if (!in->read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)))) return {};
		Measures m;
		m.flash_span = head[0];
		m.diffs.resize(head[1]);
		m.across.resize(head[1]);
		for (int32_t i = 0; i < head[1]; ++i) {
			m.diffs[i] = row[2 * i];
			m.across[i] = row[2 * i + 1];
		}
		return m;
	}
	catch (agi::Exception const&) {
		return {};
	}
}

}
