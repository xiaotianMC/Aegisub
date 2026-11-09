// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "scene_detect.h"

#include "include/aegisub/video_provider.h"
#include "video_frame.h"

#include <libaegisub/background_runner.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace scene_detect {

Params ForSensitivity(int sensitivity) {
	Params p;
	p.threshold = sensitivity <= 0 ? 0.35 : sensitivity == 1 ? 0.25 : 0.18;
	return p;
}

Signature Sign(VideoFrame const& frame) {
	Signature s;
	int const w = frame.width, h = frame.height;
	if (w <= 0 || h <= 0) return s;
	std::array<uint32_t, thumb_w * thumb_h> sum{}, count{};
	std::array<uint32_t, 3 * hist_bins> hist{};
	uint32_t pixels = 0;
	// A few dozen pixels to each cell of the thumbnail are plenty: about
	// every fourth pixel each way of a 1080p frame
	int const step = std::max(1, std::min(w / (thumb_w * 6), h / (thumb_h * 6)));
	std::vector<int> cell_x;
	for (int x = 0; x < w; x += step) cell_x.push_back(x * thumb_w / w);
	for (int y = 0; y < h; y += step) {
		int const row = frame.flipped ? h - 1 - y : y;
		unsigned char const *p = frame.data.data() + static_cast<size_t>(row) * frame.pitch;
		uint32_t *const row_sum = &sum[y * thumb_h / h * thumb_w];
		uint32_t *const row_count = &count[y * thumb_h / h * thumb_w];
		for (size_t i = 0; i < cell_x.size(); ++i) {
			unsigned char const *px = p + i * step * 4;
			int const b = px[0], g = px[1], r = px[2];
			row_sum[cell_x[i]] += (r * 77 + g * 150 + b * 29) >> 8;
			++row_count[cell_x[i]];
			++hist[b >> 4];
			++hist[hist_bins + (g >> 4)];
			++hist[2 * hist_bins + (r >> 4)];
			++pixels;
		}
	}
	for (size_t i = 0; i < sum.size(); ++i)
		s.luma[i] = count[i] ? static_cast<uint8_t>(sum[i] / count[i]) : 0;
	for (size_t i = 0; i < hist.size(); ++i)
		s.hist[i] = pixels ? static_cast<float>(hist[i]) / pixels : 0.f;
	// A frame narrower or shorter than the thumbnail leaves cells empty:
	// they take the cell before's value
	for (size_t i = 1; i < sum.size(); ++i)
		if (!count[i]) s.luma[i] = s.luma[i - 1];
	return s;
}

double Difference(Signature const& a, Signature const& b) {
	double hist = 0;
	for (size_t i = 0; i < a.hist.size(); ++i)
		hist += std::abs(a.hist[i] - b.hist[i]);
	// Each channel's histograms differ by at most 2
	hist /= 6;
	long luma = 0;
	for (size_t i = 0; i < a.luma.size(); ++i)
		luma += std::abs(int(a.luma[i]) - int(b.luma[i]));
	double const grey = double(luma) / (255. * a.luma.size());
	return (hist + grey) / 2;
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

std::vector<int> Detect(VideoProvider &video, agi::ProgressSink *ps, Params const& params) {
	int const n = video.GetFrameCount();
	if (n <= 0) return {0};
	int const span = std::max(params.flash_span, 1);
	// The signatures of the last span + 2 frames, frame j at j % ring
	int const ring = span + 2;
	std::vector<Signature> recent(ring);
	std::vector<double> diffs(n, 0.), across(n, std::numeric_limits<double>::infinity());
	VideoFrame frame;
	for (int j = 0; j < n; ++j) {
		if (ps) {
			if (ps->IsCancelled()) return {};
			if (j % 50 == 0) ps->SetProgress(j, n);
		}
		video.GetFrame(j, frame);
		auto &sig = recent[j % ring] = Sign(frame);
		if (j == 0) continue;
		diffs[j] = Difference(recent[(j - 1) % ring], sig);
		// Frame j - k - 1 against this one: is j - k - 1 back after a flash
		// at j - k ...
		for (int k = 1; k <= span && j - k >= 1; ++k) {
			int const i = j - k;
			across[i] = std::min(across[i], Difference(recent[(i - 1) % ring], sig));
		}
		// ... and is this frame one from before a flash at j - 1
		for (int m = 2; m <= span + 1 && j - m >= 0; ++m)
			across[j] = std::min(across[j], Difference(recent[(j - m) % ring], sig));
	}
	if (ps) ps->SetProgress(n, n);
	std::vector<int> result{0};
	auto cuts = FindCuts(diffs, across, params);
	result.insert(result.end(), cuts.begin(), cuts.end());
	return result;
}

}
