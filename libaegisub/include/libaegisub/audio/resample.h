// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <cstdint>
#include <vector>

namespace agi {
class AudioProvider;
class ProgressSink;

/// @p provider's audio, which must be 16-bit mono as the audio providers'
/// converter makes it, at @p rate as floats from -1 to 1: what speech
/// recognition wants, whisper.cpp's 16 kHz. Filtered against aliasing by a
/// windowed sinc, exactly for any pair of rates.
/// @param ps Told how far it has got, and cancelling there stops it; may be null
/// @throws agi::UserCancelException when cancelled on @p ps
/// @throws agi::InternalError if the audio is not 16-bit mono
std::vector<float> ResampleToFloat(AudioProvider const& provider, int rate, ProgressSink *ps = nullptr);

/// The filter ResampleToFloat uses from @p in_rate to @p out_rate: for each
/// of the phases an output sample can fall at between two input samples,
/// the weights of the input samples about it. Exposed for its tests.
struct ResampleFilter {
	/// Phases, the output rate over the rates' greatest common divisor; and
	/// how many input samples the output's position moves on for each sample
	int64_t phases = 1, step = 1;
	/// Input samples weighed before and after the position: taps = 2 * half
	int half = 0;
	/// phases * taps weights, each phase's adding to 1
	std::vector<float> weights;

	ResampleFilter(int in_rate, int out_rate);
	int taps() const { return 2 * half; }
	float const *Phase(int64_t phase) const { return weights.data() + phase * taps(); }
};
}
