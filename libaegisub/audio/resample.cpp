// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "libaegisub/audio/resample.h"

#include "libaegisub/audio/provider.h"
#include "libaegisub/background_runner.h"
#include "libaegisub/exception.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace agi {
namespace {
constexpr double pi = 3.14159265358979323846;
/// The sinc's zero crossings either side of the centre kept: the steepness
constexpr double zero_crossings = 16;
/// The Kaiser window's shape: about 80 dB down in the stopband
constexpr double beta = 8;
/// What of the lower Nyquist frequency is passed, leaving room for the
/// filter to fall before it
constexpr double passband = 0.92;

/// The modified Bessel function of the first kind, order 0, by its series;
/// not std::cyl_bessel_i, which libc++ does not have
double BesselI0(double x) {
	double sum = 1, term = 1;
	double const q = x * x / 4;
	for (int k = 1; k < 64; ++k) {
		term *= q / (double(k) * k);
		sum += term;
		if (term < sum * 1e-17) break;
	}
	return sum;
}
}

ResampleFilter::ResampleFilter(int in_rate, int out_rate) {
	if (in_rate <= 0 || out_rate <= 0) throw InternalError("Resampling from or to a rate of nothing");
	int64_t const g = std::gcd<int64_t>(in_rate, out_rate);
	phases = out_rate / g;
	step = in_rate / g;

	// Cycles per input sample passed: under half the slower rate's
	double const fc = 0.5 * passband * std::min(1.0, double(out_rate) / in_rate);
	double const reach = zero_crossings / (2 * fc);
	half = static_cast<int>(std::ceil(reach));
	double const i0_beta = BesselI0(beta);

	weights.resize(static_cast<size_t>(phases * taps()));
	for (int64_t p = 0; p < phases; ++p) {
		double const frac = double(p) / phases;
		float *w = weights.data() + p * taps();
		double sum = 0;
		for (int k = 0; k < taps(); ++k) {
			// The input sample's distance from where the output falls
			double const x = (k - half + 1) - frac;
			double const t = x / reach;
			double v = 0;
			if (std::abs(t) < 1) {
				double const arg = 2 * fc * x;
				double const sinc = arg == 0 ? 1 : std::sin(pi * arg) / (pi * arg);
				v = 2 * fc * sinc * BesselI0(beta * std::sqrt(1 - t * t)) / i0_beta;
			}
			w[k] = static_cast<float>(v);
			sum += v;
		}
		// Each phase passes a constant as it is
		if (sum != 0)
			for (int k = 0; k < taps(); ++k) w[k] = static_cast<float>(w[k] / sum);
	}
}

std::vector<float> ResampleToFloat(AudioProvider const& provider, int rate, ProgressSink *ps) {
	if (provider.GetBytesPerSample() != 2 || provider.GetChannels() != 1 || provider.AreSamplesFloat())
		throw InternalError("Resampling audio that is not 16-bit mono");
	int64_t const in_count = provider.GetNumSamples();
	int const in_rate = provider.GetSampleRate();
	constexpr int64_t block = 1 << 16;
	constexpr float scale = 1.f / 32768.f;

	auto tell = [&](int64_t done, int64_t total) {
		if (!ps) return;
		if (ps->IsCancelled()) throw UserCancelException("cancelled");
		ps->SetProgress(done, total);
	};

	std::vector<float> out;
	std::vector<int16_t> in;
	if (in_rate == rate) {
		out.resize(static_cast<size_t>(in_count));
		in.resize(block);
		for (int64_t i = 0; i < in_count; i += block) {
			tell(i, in_count);
			int64_t const n = std::min(block, in_count - i);
			provider.GetAudio(in.data(), i, n);
			for (int64_t k = 0; k < n; ++k) out[i + k] = in[k] * scale;
		}
		return out;
	}

	ResampleFilter const filter(in_rate, rate);
	int64_t const out_count = (in_count * filter.phases + filter.step - 1) / filter.step;
	out.resize(static_cast<size_t>(out_count));
	int const taps = filter.taps();
	for (int64_t j0 = 0; j0 < out_count; j0 += block) {
		tell(j0, out_count);
		int64_t const j1 = std::min(out_count, j0 + block);
		// The input about each output sample in the block: before the first
		// one's position to after the last one's
		int64_t const first = (j0 * filter.step) / filter.phases - filter.half + 1;
		int64_t const last = ((j1 - 1) * filter.step) / filter.phases + filter.half;
		in.resize(static_cast<size_t>(last - first + 1));
		provider.GetAudio(in.data(), first, last - first + 1);
		for (int64_t j = j0; j < j1; ++j) {
			int64_t const pos = j * filter.step;
			int64_t const base = pos / filter.phases;
			float const *w = filter.Phase(pos % filter.phases);
			int16_t const *x = in.data() + (base - filter.half + 1 - first);
			float acc = 0;
			for (int k = 0; k < taps; ++k) acc += x[k] * w[k];
			out[j] = acc * scale;
		}
	}
	tell(out_count, out_count);
	return out;
}
}
