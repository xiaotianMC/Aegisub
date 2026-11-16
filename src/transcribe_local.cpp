// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "transcribe_local.h"

#include "compat.h"
#include "format.h"

#include <libaegisub/background_runner.h>
#include <libaegisub/exception.h>
#include <libaegisub/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <string_view>
#include <thread>

#include <wx/intl.h>

#ifdef WITH_WHISPER
#include <ggml-backend.h>
#include <whisper.h>
#endif

namespace transcribe {
namespace {
/// How many bytes a UTF-8 sequence starting with @p lead has; 0 if it is
/// not a lead byte
int SequenceLength(unsigned char lead) {
	if (lead < 0x80) return 1;
	if ((lead & 0xE0) == 0xC0) return 2;
	if ((lead & 0xF0) == 0xE0) return 3;
	if ((lead & 0xF8) == 0xF0) return 4;
	return 0;
}

/// Whether @p s does not end partway through a character
bool Whole(std::string_view s) {
	// The last lead byte, at most three back
	for (size_t back = 1; back <= std::min<size_t>(4, s.size()); ++back) {
		auto const c = static_cast<unsigned char>(s[s.size() - back]);
		if ((c & 0xC0) == 0x80) continue;
		int const len = SequenceLength(c);
		return len == 0 || static_cast<size_t>(len) <= back;
	}
	// Nothing but continuation bytes: not to be made whole by waiting
	return true;
}

/// The code point at @p s's start, or before its end; 0 for none
char32_t Decode(std::string_view s) {
	if (s.empty()) return 0;
	auto const lead = static_cast<unsigned char>(s[0]);
	int const len = SequenceLength(lead);
	if (len == 0 || static_cast<size_t>(len) > s.size()) return 0;
	if (len == 1) return lead;
	char32_t cp = lead & (0x7F >> len);
	for (int i = 1; i < len; ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
	return cp;
}
char32_t First(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
	return Decode(s);
}
char32_t Last(std::string_view s) {
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
	size_t at = s.size();
	while (at > 0 && (static_cast<unsigned char>(s[at - 1]) & 0xC0) == 0x80) --at;
	return at > 0 ? Decode(s.substr(at - 1)) : 0;
}

/// Written without spaces between words
bool Unspaced(char32_t c) {
	return (c >= 0x0E00 && c <= 0x0FFF)    // Thai, Lao, Tibetan
	    || (c >= 0x1000 && c <= 0x109F)    // Myanmar
	    || (c >= 0x1780 && c <= 0x17FF)    // Khmer
	    || (c >= 0x2E80 && c <= 0x9FFF)    // CJK punctuation, kana, ideographs
	    || (c >= 0xF900 && c <= 0xFAFF)    // CJK compatibility ideographs
	    || (c >= 0xFE30 && c <= 0xFE4F)    // CJK compatibility forms
	    || (c >= 0xFF00 && c <= 0xFFEF)    // full and half width forms
	    || (c >= 0x20000 && c <= 0x3FFFF); // the ideographs beyond
}

std::string_view Trimmed(std::string_view s) {
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
	return s;
}

/// Nothing but punctuation: ", " "。" "?!" "」"
bool Punctuation(std::string_view s) {
	s = Trimmed(s);
	if (s.empty()) return false;
	while (!s.empty()) {
		auto const c = Decode(s);
		int const len = std::max(1, SequenceLength(static_cast<unsigned char>(s[0])));
		bool const punct = (c < 0x80 && std::ispunct(static_cast<int>(c)))
			|| (c >= 0x2000 && c <= 0x206F)  // general punctuation: … — ‘ ’ “ ”
			|| (c >= 0x3000 && c <= 0x303F && c != 0x3005)  // CJK punctuation, not 々
			|| (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20)
			|| (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65);
		if (!punct) return false;
		s.remove_prefix(std::min<size_t>(len, s.size()));
	}
	return true;
}

/// Tokens joined into whole characters
struct Piece {
	std::string text;
	int start = -1, end = -1;
	double p = -1;
};

std::vector<Piece> Pieces(std::vector<Token> const& tokens) {
	std::vector<Piece> out;
	Piece pending;
	bool open = false;
	for (auto const& t : tokens) {
		if (t.special) continue;
		if (!open) {
			pending = Piece{};
			pending.start = t.start;
			open = true;
		}
		pending.text += t.text;
		if (t.end >= 0) pending.end = t.end;
		if (pending.start < 0) pending.start = t.start;
		if (t.p >= 0) pending.p = pending.p < 0 ? t.p : std::min(pending.p, t.p);
		if (Whole(pending.text)) {
			out.push_back(std::move(pending));
			open = false;
		}
	}
	// A character never finished: what there is of it kept, as whisper.cpp
	// itself would print it
	if (open) out.push_back(std::move(pending));
	return out;
}
}

speech::Transcript FromWhisper(std::vector<WhisperSegment> const& segments, std::string language) {
	speech::Transcript out;
	out.language = std::move(language);
	for (auto const& seg : segments) {
		speech::Segment s;
		s.start = seg.start;
		s.end = std::max(seg.end, seg.start);
		for (auto const& token : seg.tokens)
			if (!token.special) s.text += token.text;
		s.text = std::string(Trimmed(s.text));
		if (s.text.empty()) continue;

		auto &words = s.words;
		bool space_before = true;
		for (auto& piece : Pieces(seg.tokens)) {
			auto const text = Trimmed(piece.text);
			if (text.empty()) {
				space_before = true;
				continue;
			}
			// Punctuation goes on the word before, whatever its script, and
			// does not carry its end over the pause after it
			if (!words.empty() && Punctuation(text)) {
				words.back().text += text;
				space_before = piece.text.back() == ' ';
				continue;
			}
			bool const starts_word = words.empty() || space_before || piece.text.front() == ' '
				|| Unspaced(First(text)) || Unspaced(Last(words.back().text));
			space_before = piece.text.back() == ' ';
			int const start = piece.start;
			if (starts_word) {
				speech::Word w;
				w.text = std::string(text);
				w.start = start;
				w.end = piece.end;
				w.score = piece.p;
				words.push_back(std::move(w));
				continue;
			}
			auto &w = words.back();
			w.text += piece.text;
			if (piece.end >= 0) w.end = piece.end;
			if (w.start < 0) w.start = start;
			if (piece.p >= 0) w.score = w.score < 0 ? piece.p : std::min(w.score, piece.p);
		}

		// Each word within its segment, started no sooner than the one
		// before, and ended no later than the next starts nor sooner than
		// it starts itself
		int earliest = s.start;
		for (auto &w : words) {
			w.text = std::string(Trimmed(w.text));
			if (w.start >= 0) earliest = w.start = std::clamp(w.start, earliest, s.end);
			if (w.end >= 0) w.end = std::clamp(w.end, s.start, s.end);
		}
		for (size_t i = 0; i < words.size(); ++i) {
			auto &w = words[i];
			if (i + 1 < words.size() && words[i + 1].start >= 0 && w.end > words[i + 1].start)
				w.end = words[i + 1].start;
			if (w.start >= 0 && w.end >= 0 && w.end < w.start)
				w.end = w.start;
		}
		out.segments.push_back(std::move(s));
	}
	return out;
}

std::string TimeLeft(double elapsed_seconds, int done) {
	// Too little done for its pace to say much
	if (done < 3 || done >= 100 || elapsed_seconds < 2) return "";
	double const left = elapsed_seconds * (100 - done) / done;
	// Rounded up: it comes in tenths, and is better done before it said
	if (left < 30) return from_wx(_("less than a minute left"));
	int const minutes = static_cast<int>(std::ceil(left / 60));
	return from_wx(wxString::Format(wxPLURAL("about %d minute left", "about %d minutes left", minutes), minutes));
}

#ifdef WITH_WHISPER
bool CanRunLocal() { return true; }

std::string Gpu() {
	for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
		auto dev = ggml_backend_dev_get(i);
		auto const type = ggml_backend_dev_type(dev);
		if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
			char const *description = ggml_backend_dev_description(dev);
			return description && *description ? description : ggml_backend_dev_name(dev);
		}
	}
	return "";
}

namespace {
std::string Utf8(agi::fs::path const& p) {
	auto const u8 = p.u8string();
	return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

/// whisper.cpp's and ggml's chatter into Aegisub's log, not the console
void Log(ggml_log_level level, const char *text, void *) {
	if (!text) return;
	std::string_view t(text);
	while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.remove_suffix(1);
	if (t.empty()) return;
	if (level == GGML_LOG_LEVEL_ERROR) LOG_E("transcribe/whisper") << t;
	else if (level == GGML_LOG_LEVEL_WARN) LOG_W("transcribe/whisper") << t;
	else LOG_D("transcribe/whisper") << t;
}

struct Context {
	whisper_context *ctx = nullptr;
	~Context() { if (ctx) whisper_free(ctx); }
};

/// What the decoder is told came before: the terms to recognise, and for
/// Chinese a sentence in simplified characters, which it otherwise often
/// writes traditional
std::string Prompt(Request const& request) {
	std::string prompt;
	if (request.language == "zh") prompt = "以下是普通话的句子。";
	for (auto const& term : request.keyterms) {
		if (prompt.size() > 600) break;
		if (!prompt.empty()) prompt += request.language == "zh" || request.language == "ja" ? "、" : ", ";
		prompt += term;
	}
	return prompt;
}
}

speech::Transcript RunLocal(Request const& request, Local const& settings, std::vector<float> const& samples, agi::ProgressSink *ps) {
	if (samples.empty()) throw agi::EnvironmentError(from_wx(_("There is no audio to hear.")));
	if (!agi::fs::FileExists(settings.model))
		throw agi::EnvironmentError(agi::format(from_wx(_("There is no model file %s.")), settings.model.string()));
	std::string const lang = request.language.empty() ? "auto" : request.language;
	if (lang != "auto" && whisper_lang_id(lang.c_str()) < 0)
		throw agi::EnvironmentError(agi::format(from_wx(_("Whisper does not know the language %s: give its two-letter code, such as ja or zh.")), lang));

	whisper_log_set(Log, nullptr);

	// Not DTW: its time for a token is about when the token was said to the
	// end, later than the timestamp tokens' start, which fit the speech
	auto cparams = whisper_context_default_params();
	cparams.use_gpu = settings.use_gpu;

	if (ps) {
		ps->SetMessage(from_wx(_("Loading the speech recognition model...")));
		ps->SetIndeterminate();
	}
	Context c;
	c.ctx = whisper_init_from_file_with_params(Utf8(settings.model).c_str(), cparams);
	if (!c.ctx && cparams.use_gpu) {
		// A GPU that would not start: the processor instead
		LOG_W("transcribe/whisper") << "could not start on the GPU; trying the CPU";
		cparams.use_gpu = false;
		c.ctx = whisper_init_from_file_with_params(Utf8(settings.model).c_str(), cparams);
	}
	if (!c.ctx)
		throw agi::EnvironmentError(agi::format(from_wx(_("Could not load %s as a Whisper model: is it a ggml model file, and whole?")), settings.model.filename().string()));
	if (ps && ps->IsCancelled()) throw agi::UserCancelException("cancelled");

	auto wp = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
	unsigned const cores = std::max(1u, std::thread::hardware_concurrency());
	wp.n_threads = settings.threads > 0 ? settings.threads : static_cast<int>(std::min(8u, cores));
	wp.print_progress = wp.print_realtime = wp.print_timestamps = wp.print_special = false;
	wp.token_timestamps = true;
	wp.language = lang.c_str();
	wp.detect_language = false;
	wp.suppress_nst = true;
	std::string const prompt = Prompt(request);
	if (!prompt.empty()) wp.initial_prompt = prompt.c_str();
	std::string const vad = Utf8(settings.vad_model);
	if (!settings.vad_model.empty() && agi::fs::FileExists(settings.vad_model)) {
		wp.vad = true;
		wp.vad_model_path = vad.c_str();
	}
	// How far it has got, and from its pace so far what is left
	struct Pace {
		agi::ProgressSink *ps;
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		std::string said;
	} pace{ps};
	if (ps) {
		ps->SetMessage(from_wx(_("Hearing the audio through...")));
		ps->SetProgress(0, 100);
		wp.progress_callback = [](whisper_context *, whisper_state *, int progress, void *user) {
			auto pace = static_cast<Pace *>(user);
			pace->ps->SetProgress(progress, 100);
			double const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - pace->start).count();
			auto left = TimeLeft(elapsed, progress);
			if (left.empty() || left == pace->said) return;
			pace->said = left;
			pace->ps->SetMessage(agi::format(from_wx(_("Hearing the audio through: %s")), left));
		};
		wp.progress_callback_user_data = &pace;
		// Asked from ggml's threads: DialogProgress's answer is an atomic
		wp.abort_callback = [](void *user) { return static_cast<agi::ProgressSink *>(user)->IsCancelled(); };
		wp.abort_callback_user_data = ps;
	}

	int const result = whisper_full(c.ctx, wp, samples.data(), static_cast<int>(samples.size()));
	if (ps && ps->IsCancelled()) throw agi::UserCancelException("cancelled");
	if (result != 0)
		throw agi::EnvironmentError(agi::format(from_wx(_("Whisper could not hear the audio through (%d): see the log for why.")), result));

	// Times in centiseconds, the tokens' mapped back to the audio's own
	// timeline when VAD left silence out
	auto ms = [](int64_t cs) { return cs < 0 ? -1 : static_cast<int>(cs * 10); };
	whisper_token const eot = whisper_token_eot(c.ctx);
	std::vector<WhisperSegment> segments;
	int const n = whisper_full_n_segments(c.ctx);
	for (int i = 0; i < n; ++i) {
		WhisperSegment seg;
		seg.start = ms(whisper_full_get_segment_t0(c.ctx, i));
		seg.end = ms(whisper_full_get_segment_t1(c.ctx, i));
		int const tokens = whisper_full_n_tokens(c.ctx, i);
		for (int k = 0; k < tokens; ++k) {
			auto const data = whisper_full_get_token_data(c.ctx, i, k);
			Token t;
			t.special = data.id >= eot;
			if (auto text = whisper_full_get_token_text(c.ctx, i, k)) t.text = text;
			t.start = ms(whisper_full_get_token_t0(c.ctx, i, k));
			t.end = ms(whisper_full_get_token_t1(c.ctx, i, k));
			t.p = data.p;
			seg.tokens.push_back(std::move(t));
		}
		segments.push_back(std::move(seg));
	}
	char const *detected = whisper_lang_str(whisper_full_lang_id(c.ctx));
	return FromWhisper(segments, detected ? detected : request.language);
}
#else
bool CanRunLocal() { return false; }
std::string Gpu() { return ""; }
speech::Transcript RunLocal(Request const&, Local const&, std::vector<float> const&, agi::ProgressSink *) {
	throw agi::EnvironmentError(from_wx(_("This build of Aegisub has no Whisper built in.")));
}
#endif
}
