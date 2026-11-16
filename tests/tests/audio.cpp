// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include <main.h>

#include <libaegisub/audio/provider.h>
#include <libaegisub/audio/resample.h>
#include <libaegisub/background_runner.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <libaegisub/util.h>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <fstream>

TEST(lagi_audio, dummy_blank) {
	auto provider = agi::CreateDummyAudioProvider("dummy-audio:", nullptr);

	char buff[1024];
	memset(buff, 1, sizeof(buff));
	provider->GetAudio(buff, 12356, 512);
	for (size_t i = 0; i < sizeof(buff); ++i) ASSERT_EQ(0, buff[i]);
}

TEST(lagi_audio, dummy_noise) {
	auto provider = agi::CreateDummyAudioProvider("dummy-audio:noise?", nullptr);

	char buff[1024];
	memset(buff, 0, sizeof(buff));
	provider->GetAudio(buff, 12356, 512);
	for (size_t i = 0; i < sizeof(buff); ++i) {
		if (buff[i] != 0)
			return;
	}
	bool all_zero = true;
	ASSERT_FALSE(all_zero);
}

TEST(lagi_audio, dummy_rejects_non_dummy_url) {
	auto provider = agi::CreateDummyAudioProvider("/tmp", nullptr);
	ASSERT_EQ(nullptr, provider.get());
}

template<typename Sample=uint16_t>
struct TestAudioProvider : agi::AudioProvider {
	int bias = 0;

	TestAudioProvider(int64_t duration = 90, int rate=48000) {
		channels = 1;
		num_samples = duration * 48000;
		decoded_samples = num_samples;
		sample_rate = rate;
		bytes_per_sample = sizeof(Sample);
		float_samples = false;
	}

	void FillBuffer(void *buf, int64_t start, int64_t count) const override {
		auto out = static_cast<Sample *>(buf);
		for (int64_t end = start + count; start < end; ++start)
			*out++ = (Sample)(start + bias);
	}
};

TEST(lagi_audio, before_sample_zero) {
	TestAudioProvider<> provider;

	uint16_t buff[16];
	memset(buff, 1, sizeof(buff));
	provider.GetAudio(buff, -8, 16);

	for (int i = 0; i < 8; ++i)
		ASSERT_EQ(0, buff[i]);
	for (int i = 8; i < 16; ++i)
		ASSERT_EQ(i - 8, buff[i]);
}

TEST(lagi_audio, before_sample_zero_8bit) {
	TestAudioProvider<uint8_t> provider;
	provider.bias = 128;

	uint8_t buff[16];
	memset(buff, 1, sizeof(buff));
	provider.GetAudio(buff, -8, 16);

	for (int i = 0; i < 8; ++i)
		ASSERT_EQ(128, buff[i]);
	for (int i = 8; i < 16; ++i)
		ASSERT_EQ(128 + i - 8, buff[i]);
}

TEST(lagi_audio, after_end) {
	TestAudioProvider<> provider(1);

	uint16_t buff[16];
	memset(buff, 1, sizeof(buff));
	provider.GetAudio(buff, provider.GetNumSamples() - 8, 16);

	for (int i = 0; i < 8; ++i)
		ASSERT_NE(0, buff[i]);
	for (int i = 8; i < 16; ++i)
		ASSERT_EQ(0, buff[i]);
}

TEST(lagi_audio, save_audio_clip) {
	auto path = agi::Path().Decode("?temp/save_clip");
	agi::fs::Remove(path);

	auto provider = agi::CreateDummyAudioProvider("dummy-audio:noise?", nullptr);
	agi::SaveAudioClip(*provider, path, 60 * 60 * 1000, (60 * 60 + 10) * 1000);

	{
		std::ifstream s(path, std::ios_base::binary);
		ASSERT_TRUE(s.good());
		s.seekg(0, std::ios::end);
		// 10 seconds of 44.1 kHz samples per second of 16-bit mono, plus 44 bytes of header
		EXPECT_EQ(10 * 44100 * 2 + 44, s.tellg());
	}
	agi::fs::Remove(path);
}

TEST(lagi_audio, save_audio_clip_out_of_audio_range) {
	const auto path = agi::Path().Decode("?temp/save_clip");
	agi::fs::Remove(path);

	const auto provider = agi::CreateDummyAudioProvider("dummy-audio:noise?", nullptr);
	const auto end_time = 150 * 60 * 1000;

	// Start time after end of clip: empty file
	agi::SaveAudioClip(*provider, path, end_time, end_time + 1);
	{
		std::ifstream s(path, std::ios_base::binary);
		ASSERT_TRUE(s.good());
		s.seekg(0, std::ios::end);
		EXPECT_EQ(44, s.tellg());
	}
	agi::fs::Remove(path);

	// Start time >= end time: empty file
	agi::SaveAudioClip(*provider, path, end_time - 1, end_time - 1);
	{
		std::ifstream s(path, std::ios_base::binary);
		ASSERT_TRUE(s.good());
		s.seekg(0, std::ios::end);
		EXPECT_EQ(44, s.tellg());
	}
	agi::fs::Remove(path);

	// Start time during clip, end time after end of clip: save only the part that exists
	agi::SaveAudioClip(*provider, path, end_time - 1000, end_time + 1000);
	{
		std::ifstream s(path, std::ios_base::binary);
		ASSERT_TRUE(s.good());
		s.seekg(0, std::ios::end);
		// 1 second of 44.1 kHz samples per second of 16-bit mono, plus 44 bytes of header
		EXPECT_EQ(44100 * 2 + 44, s.tellg());
	}
	agi::fs::Remove(path);
}

TEST(lagi_audio, get_with_volume) {
	TestAudioProvider<> provider;
	uint16_t buff[4];

	provider.GetAudioWithVolume(buff, 0, 4, 1.0);
	EXPECT_EQ(0, buff[0]);
	EXPECT_EQ(1, buff[1]);
	EXPECT_EQ(2, buff[2]);
	EXPECT_EQ(3, buff[3]);

	provider.GetAudioWithVolume(buff, 0, 4, 0.0);
	EXPECT_EQ(0, buff[0]);
	EXPECT_EQ(0, buff[1]);
	EXPECT_EQ(0, buff[2]);
	EXPECT_EQ(0, buff[3]);

	provider.GetAudioWithVolume(buff, 0, 4, 2.0);
	EXPECT_EQ(0, buff[0]);
	EXPECT_EQ(2, buff[1]);
	EXPECT_EQ(4, buff[2]);
	EXPECT_EQ(6, buff[3]);
}

TEST(lagi_audio, volume_should_clamp_rather_than_wrap) {
	TestAudioProvider<> provider;
	uint16_t buff[1];
	provider.GetAudioWithVolume(buff, 30000, 1, 2.0);
	EXPECT_EQ(SHRT_MAX, buff[0]);
}

TEST(lagi_audio, ram_cache) {
	auto provider = agi::CreateRAMAudioProvider(std::make_unique<TestAudioProvider<>>());
	EXPECT_EQ(1, provider->GetChannels());
	EXPECT_EQ(90 * 48000, provider->GetNumSamples());
	EXPECT_EQ(48000, provider->GetSampleRate());
	EXPECT_EQ(2, provider->GetBytesPerSample());
	EXPECT_EQ(false, provider->AreSamplesFloat());
	EXPECT_EQ(false, provider->NeedsCache());
	while (provider->GetDecodedSamples() != provider->GetNumSamples()) agi::util::sleep_for(0);

	uint16_t buff[512];
	provider->GetAudio(buff, (1 << 22) - 256, 512); // Stride two cache blocks

	for (size_t i = 0; i < 512; ++i)
		ASSERT_EQ(static_cast<uint16_t>((1 << 22) - 256 + i), buff[i]);
}

TEST(lagi_audio, hd_cache) {
	auto provider = agi::CreateHDAudioProvider(std::make_unique<TestAudioProvider<>>(), agi::Path().Decode("?temp"));
	while (provider->GetDecodedSamples() != provider->GetNumSamples()) agi::util::sleep_for(0);

	uint16_t buff[512];
	provider->GetAudio(buff, (1 << 22) - 256, 512);

	for (size_t i = 0; i < 512; ++i)
		ASSERT_EQ(static_cast<uint16_t>((1 << 22) - 256 + i), buff[i]);
}

TEST(lagi_audio, hd_cache_file_goes_with_the_provider) {
	auto dir = agi::Path().Decode("?temp") / "aegisub-hd-cache-test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	{
		auto provider = agi::CreateHDAudioProvider(std::make_unique<TestAudioProvider<>>(), dir);
		while (provider->GetDecodedSamples() != provider->GetNumSamples()) agi::util::sleep_for(0);
	}
	EXPECT_TRUE(std::filesystem::is_empty(dir)) << "the cache file was left behind";
	std::filesystem::remove_all(dir);
}

TEST(lagi_audio, hd_cache_file_names) {
	EXPECT_TRUE(agi::IsHDAudioCacheFile("aegisub-audio-1763100000-4242"));
	EXPECT_TRUE(agi::IsHDAudioCacheFile("audio-1763100000-4242")) << "as named before";
	EXPECT_FALSE(agi::IsHDAudioCacheFile("audio-1763100000-4242.wav"));
	EXPECT_FALSE(agi::IsHDAudioCacheFile("audio-1763100000"));
	EXPECT_FALSE(agi::IsHDAudioCacheFile("audio--4242"));
	EXPECT_FALSE(agi::IsHDAudioCacheFile("audio-abc-4242"));
	EXPECT_FALSE(agi::IsHDAudioCacheFile("my-audio-1-2"));
	EXPECT_FALSE(agi::IsHDAudioCacheFile("aegisub-audio-"));
}

TEST(lagi_audio, convert_8bit) {
	auto provider = agi::CreateConvertAudioProvider(std::make_unique<TestAudioProvider<uint8_t>>());

	int16_t data[256];
	provider->GetAudio(data, 0, 256);
	for (int i = 0; i < 256; ++i)
		ASSERT_EQ((i - 128) * 256, data[i]);
}

TEST(lagi_audio, convert_32bit) {
	auto src = std::make_unique<TestAudioProvider<uint32_t>>(100000);
	src->bias = INT_MIN;
	auto provider = agi::CreateConvertAudioProvider(std::move(src));

	int16_t sample;
	provider->GetAudio(&sample, 0, 1);
	EXPECT_EQ(SHRT_MIN, sample);

	provider->GetAudio(&sample, 1LL << 31, 1);
	EXPECT_EQ(0, sample);

	provider->GetAudio(&sample, (1LL << 32) - 1, 1);
	EXPECT_EQ(SHRT_MAX, sample);
}

TEST(lagi_audio, sample_doubling) {
	struct AudioProvider : agi::AudioProvider {
		AudioProvider() {
			channels = 1;
			num_samples = 90 * 20000;
			decoded_samples = num_samples;
			sample_rate = 20000;
			bytes_per_sample = 2;
			float_samples = false;
		}

		void FillBuffer(void *buf, int64_t start, int64_t count) const override {
			auto out = static_cast<int16_t *>(buf);
			for (int64_t end = start + count; start < end; ++start)
				*out++ = (int16_t)(start * 2);
		}
	};

	auto provider = agi::CreateConvertAudioProvider(std::make_unique<AudioProvider>());
	EXPECT_EQ(40000, provider->GetSampleRate());

	int16_t samples[6];
	for (int k = 0; k < 6; ++k) {
		SCOPED_TRACE(k);
		for (int i = k; i < 6; ++i) {
			SCOPED_TRACE(i);
			memset(samples, 0, sizeof(samples));
			provider->GetAudio(samples, k, i - k);
			for (int j = 0; j < i - k; ++j)
				EXPECT_EQ(j + k, samples[j]);
			for (int j = i - k; j < 6 - k; ++j)
				EXPECT_EQ(0, samples[j]);
		}
	}
}

TEST(lagi_audio, stereo_downmix) {
	struct AudioProvider : agi::AudioProvider {
		AudioProvider() {
			channels = 2;
			num_samples = 90 * 480000;
			decoded_samples = num_samples;
			sample_rate = 480000;
			bytes_per_sample = 2;
			float_samples = false;
		}

		void FillBuffer(void *buf, int64_t start, int64_t count) const override {
			auto out = static_cast<int16_t *>(buf);
			for (int64_t end = start + count; start < end; ++start) {
				*out++ = (int16_t)(start * 2);
				*out++ = 0;
			}
		}
	};

	auto provider = agi::CreateConvertAudioProvider(std::make_unique<AudioProvider>());
	EXPECT_EQ(1, provider->GetChannels());

	int16_t samples[100];
	provider->GetAudio(samples, 0, 100);
	for (int i = 0; i < 100; ++i)
		EXPECT_EQ(i, samples[i]);
}

template<typename Float>
struct FloatAudioProvider : agi::AudioProvider {
	FloatAudioProvider() {
		channels = 1;
		num_samples = 90 * 480000;
		decoded_samples = num_samples;
		sample_rate = 480000;
		bytes_per_sample = sizeof(Float);
		float_samples = true;
	}

	void FillBuffer(void *buf, int64_t start, int64_t count) const override {
		auto out = static_cast<Float *>(buf);
		for (int64_t end = start + count; start < end; ++start) {
			auto shifted = start + SHRT_MIN;
			*out++ = (Float)(1.0 * shifted / (shifted < 0 ? -SHRT_MIN : SHRT_MAX));
		}
	}
};

TEST(lagi_audio, float_conversion) {
	auto provider = agi::CreateConvertAudioProvider(std::make_unique<FloatAudioProvider<float>>());
	EXPECT_FALSE(provider->AreSamplesFloat());

	int16_t samples[1 << 16];
	provider->GetAudio(samples, 0, 1 << 16);
	for (int i = 0; i < (1 << 16); ++i)
		ASSERT_EQ(i + SHRT_MIN, samples[i]);
}

TEST(lagi_audio, double_conversion) {
	auto provider = agi::CreateConvertAudioProvider(std::make_unique<FloatAudioProvider<double>>());
	EXPECT_FALSE(provider->AreSamplesFloat());

	int16_t samples[1 << 16];
	provider->GetAudio(samples, 0, 1 << 16);
	for (int i = 0; i < (1 << 16); ++i)
		ASSERT_EQ(i + SHRT_MIN, samples[i]);
}

TEST(lagi_audio, pcm_simple) {
	auto path = agi::Path().Decode("?temp/pcm_simple");
	{
		TestAudioProvider<> provider;
		agi::SaveAudioClip(provider, path, 0, 1000);
	}

	{
		auto provider = agi::CreatePCMAudioProvider(path, nullptr);
		EXPECT_EQ(1, provider->GetChannels());
		EXPECT_EQ(48000, provider->GetNumSamples());
		EXPECT_EQ(48000, provider->GetSampleRate());
		EXPECT_EQ(2, provider->GetBytesPerSample());
		EXPECT_EQ(false, provider->AreSamplesFloat());
		EXPECT_EQ(false, provider->NeedsCache());

		for (int i = 0; i < 100; ++i) {
			uint16_t sample;
			provider->GetAudio(&sample, i, 1);
			ASSERT_EQ(i, sample);
		}
	}

	agi::fs::Remove(path);
}

TEST(lagi_audio, pcm_truncated) {
	auto path = agi::Path().Decode("?temp/pcm_truncated");
	{
		TestAudioProvider<> provider;
		agi::SaveAudioClip(provider, path, 0, 1000);
	}

	char file[1000];

	{ std::ifstream s(path, std::ios_base::binary); s.read(file, sizeof file); }
	{ std::ofstream s(path, std::ios_base::binary); s.write(file, sizeof file); }

	{
		auto provider = agi::CreatePCMAudioProvider(path, nullptr);

		// Should still report full duration
		EXPECT_EQ(48000, provider->GetNumSamples());

		// And should zero-pad past the end
		int64_t sample_count = (1000 - 44) / 2;
		uint16_t sample;

		provider->GetAudio(&sample, sample_count - 1, 1);
		EXPECT_EQ(sample_count - 1, sample);

		provider->GetAudio(&sample, sample_count, 1);
		EXPECT_EQ(0, sample);
	}

	agi::fs::Remove(path);
}

#define RIFF "RIFF\0\0\0\x60WAVE"
#define FMT_VALID "fmt \x10\0\0\0\1\0\1\0\x10\0\0\0\x20\0\0\0\2\0\x10\0"
#define DATA_VALID "data\1\0\0\0\0\0"
#define WRITE(str) do { std::ofstream s(path, std::ios_base::binary); s.write(str, sizeof(str) - 1); } while (false)

TEST(lagi_audio, pcm_incomplete) {
	auto path = agi::Path().Decode("?temp/pcm_incomplete");

	agi::fs::Remove(path);
	ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::fs::FileNotFound);

	{std::ofstream(path, std::ios_base::binary); }
	ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioDataNotFound);

	// Invalid tags
	WRITE("ASDF");
	ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioDataNotFound);

	WRITE("RIFF\0\0\0\x60" "ASDF");
	ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioDataNotFound);

	// Incomplete files
	const char valid_file[] = RIFF FMT_VALID DATA_VALID;

	// -1 for nul term, -3 so that longest file is still invalid
	for (size_t i = 0; i < sizeof(valid_file) - 4; ++i) {
		{
			std::ofstream s(path, std::ios_base::binary);
			s.write(valid_file, i);
		}
		ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioDataNotFound);
	}

	// fmt must come before data
	WRITE(RIFF "data\0\0\0\x60");
	ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioProviderError);

	// Bad compression format
	WRITE(RIFF "fmt \x60\0\0\0\2\0");
	ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioProviderError);

	// Multiple fmt chunks not supported
	WRITE(RIFF FMT_VALID FMT_VALID);
	ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioProviderError);

	agi::fs::Remove(path);
}

TEST(lagi_audio, multiple_data_chunks) {
	auto path = agi::Path().Decode("?temp/multiple_data");

	WRITE(RIFF FMT_VALID "data\2\0\0\0\1\0" "data\2\0\0\0\2\0" "data\2\0\0\0\3\0");

	{
		auto provider = agi::CreatePCMAudioProvider(path, nullptr);
		ASSERT_EQ(3, provider->GetNumSamples());

		uint16_t samples[3];

		provider->GetAudio(samples, 0, 3);
		EXPECT_EQ(1, samples[0]);
		EXPECT_EQ(2, samples[1]);
		EXPECT_EQ(3, samples[2]);

		samples[1] = 5;
		provider->GetAudio(samples, 2, 1);
		EXPECT_EQ(3, samples[0]);
		EXPECT_EQ(5, samples[1]);

		provider->GetAudio(samples, 1, 1);
		EXPECT_EQ(2, samples[0]);
		EXPECT_EQ(5, samples[1]);

		provider->GetAudio(samples, 0, 1);
		EXPECT_EQ(1, samples[0]);
		EXPECT_EQ(5, samples[1]);
	}

	agi::fs::Remove(path);
}

#define WAVE64_FILE \
	"riff\x2e\x91\xcf\x11\xa5\xd6\x28\xdb\x04\xc1\x00\x00"   /* RIFF GUID */          \
	"\x74\x00\0\0\0\0\0\0"                                   /* file size */          \
	"wave\xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a"   /* WAVE GUID */          \
	"fmt \xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a"   /* fmt GUID */           \
	"\x30\x00\0\0\0\0\0\0"                                   /* fmt chunk size */     \
	"\1\0\1\0\x10\0\0\0\x20\0\0\0\2\0\x10\0\0\0\0\0\0\0\0\0" /* fmt chunk */          \
	"data\xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a"   /* data GUID */          \
	"\x1c\0\0\0\0\0\0\0"                                     /* data chunk size */    \
	"\1\0\2\0"                                               /* actual sample data */ \

TEST(lagi_audio, wave64_simple) {
	auto path = agi::Path().Decode("?temp/w64_valid");
	WRITE(WAVE64_FILE);

	{
		auto provider = agi::CreatePCMAudioProvider(path, nullptr);
		ASSERT_EQ(2, provider->GetNumSamples());

		uint16_t samples[2];
		provider->GetAudio(samples, 0, 2);
		EXPECT_EQ(1, samples[0]);
		EXPECT_EQ(2, samples[1]);
	}

	agi::fs::Remove(path);
}

TEST(lagi_audio, wave64_truncated) {
	auto path = agi::Path().Decode("?temp/w64_truncated");

	// Should be invalid until there's an entire sample
	for (size_t i = 0; i < sizeof(WAVE64_FILE) - 4; ++i) {
		{
			std::ofstream s(path, std::ios_base::binary);
			s.write(WAVE64_FILE, i);
		}
		ASSERT_THROW(agi::CreatePCMAudioProvider(path, nullptr), agi::AudioDataNotFound);
	}

	{
		std::ofstream s(path, std::ios_base::binary);
		s.write(WAVE64_FILE, sizeof(WAVE64_FILE) - 3);
	}
	ASSERT_NO_THROW(agi::CreatePCMAudioProvider(path, nullptr));

	{
		auto provider = agi::CreatePCMAudioProvider(path, nullptr);
		uint16_t sample;
		provider->GetAudio(&sample, 0, 1);
		EXPECT_EQ(1, sample);
	}

	agi::fs::Remove(path);
}

namespace {
/// A sine of @p hz at @p amplitude, 16-bit mono
struct SineAudioProvider final : agi::AudioProvider {
	double hz, amplitude;
	SineAudioProvider(int rate, double seconds, double hz, double amplitude = 0.5) : hz(hz), amplitude(amplitude) {
		channels = 1;
		sample_rate = rate;
		num_samples = static_cast<int64_t>(seconds * rate);
		decoded_samples = num_samples;
		bytes_per_sample = 2;
		float_samples = false;
	}
	void FillBuffer(void *buf, int64_t start, int64_t count) const override {
		auto out = static_cast<int16_t *>(buf);
		for (int64_t i = 0; i < count; ++i)
			out[i] = static_cast<int16_t>(std::lround(32767 * amplitude * std::sin(2 * 3.14159265358979323846 * hz * double(start + i) / sample_rate)));
	}
};

/// The RMS of @p s away from its ends, where the filter runs into the silence about the audio
double MiddleRms(std::vector<float> const& s) {
	size_t const from = s.size() / 10, to = s.size() - s.size() / 10;
	double sum = 0;
	for (size_t i = from; i < to; ++i) sum += double(s[i]) * s[i];
	return std::sqrt(sum / double(to - from));
}

/// How many times @p s goes from below 0 to 0 or above, away from its ends
int RisingCrossings(std::vector<float> const& s) {
	int n = 0;
	for (size_t i = s.size() / 10 + 1; i < s.size() - s.size() / 10; ++i)
		if (s[i - 1] < 0 && s[i] >= 0) ++n;
	return n;
}

struct CancelledSink final : agi::ProgressSink {
	void SetIndeterminate() override {}
	void SetTitle(std::string const&) override {}
	void SetMessage(std::string const&) override {}
	void SetProgress(int64_t, int64_t) override {}
	void Log(std::string const&) override {}
	bool IsCancelled() override { return true; }
};
}

TEST(lagi_audio, resample_filter_phases_each_pass_a_constant) {
	for (auto [in, out] : {std::pair{48000, 16000}, {44100, 16000}, {22050, 16000}, {32000, 16000}, {8000, 16000}}) {
		agi::ResampleFilter f(in, out);
		EXPECT_EQ(out / std::gcd(in, out), f.phases) << in;
		EXPECT_EQ(in / std::gcd(in, out), f.step) << in;
		for (int64_t p = 0; p < f.phases; ++p) {
			double sum = 0;
			for (int k = 0; k < f.taps(); ++k) sum += f.Phase(p)[k];
			ASSERT_NEAR(1.0, sum, 1e-4) << in << " phase " << p;
		}
	}
}

TEST(lagi_audio, resample_keeps_a_tone_below_the_new_nyquist_and_its_pitch) {
	for (int in : {48000, 44100, 32000}) {
		SineAudioProvider p(in, 2.0, 1000);
		auto s = agi::ResampleToFloat(p, 16000);
		EXPECT_EQ(static_cast<size_t>((p.GetNumSamples() * 16000 + in - 1) / in), s.size()) << in;
		EXPECT_NEAR(0.5 / std::sqrt(2.0), MiddleRms(s), 0.005) << in;
		// 1000 rising crossings a second over the middle 1.6 s
		EXPECT_NEAR(1600, RisingCrossings(s), 2) << in;
	}
}

TEST(lagi_audio, resample_takes_away_what_is_above_the_new_nyquist_rather_than_folding_it_down) {
	for (double hz : {9000.0, 12000.0, 20000.0}) {
		SineAudioProvider p(48000, 1.0, hz);
		auto s = agi::ResampleToFloat(p, 16000);
		EXPECT_LT(MiddleRms(s), 0.5 / std::sqrt(2.0) * 0.01) << hz << " Hz, 40 dB down at least";
	}
}

TEST(lagi_audio, resample_at_the_same_rate_is_the_samples_as_floats) {
	SineAudioProvider p(16000, 0.1, 440);
	auto s = agi::ResampleToFloat(p, 16000);
	ASSERT_EQ(static_cast<size_t>(p.GetNumSamples()), s.size());
	std::vector<int16_t> raw(s.size());
	p.GetAudio(raw.data(), 0, static_cast<int64_t>(raw.size()));
	for (size_t i = 0; i < s.size(); ++i) ASSERT_FLOAT_EQ(raw[i] / 32768.f, s[i]);
}

TEST(lagi_audio, resample_wants_16_bit_mono_and_stops_when_cancelled) {
	TestAudioProvider<uint8_t> eight_bit(1);
	EXPECT_THROW(agi::ResampleToFloat(eight_bit, 16000), agi::InternalError);
	SineAudioProvider p(48000, 1.0, 1000);
	CancelledSink cancelled;
	EXPECT_THROW(agi::ResampleToFloat(p, 16000, &cancelled), agi::UserCancelException);
}
