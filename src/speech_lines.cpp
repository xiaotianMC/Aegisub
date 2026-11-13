// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "speech_lines.h"

#include <libaegisub/audio/provider.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/visitor.h>
#include <libaegisub/exception.h>
#include <libaegisub/unicode.h>

#include <algorithm>
#include <climits>
#include <map>
#include <cmath>
#include <memory>
#include <set>
#include <string_view>

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/utext.h>

namespace speech {
namespace {
/// A JSON number, integer or not; nothing for anything else
struct NumberOf final : json::ConstVisitor {
	bool found = false;
	double value = 0;
	void Visit(const json::Array&) override {}
	void Visit(const json::Object&) override {}
	void Visit(int64_t n) override { found = true; value = static_cast<double>(n); }
	void Visit(double n) override { found = !std::isnan(n); value = n; }
	void Visit(const json::String&) override {}
	void Visit(bool) override {}
	void Visit(const json::Null&) override {}
};

bool Number(json::Object const& obj, const char *key, double &out) {
	auto it = obj.find(key);
	if (it == obj.end()) return false;
	NumberOf n;
	it->second.Accept(n);
	if (n.found) out = n.value;
	return n.found;
}

struct StringOf final : json::ConstVisitor {
	std::string const *value = nullptr;
	void Visit(const json::Array&) override {}
	void Visit(const json::Object&) override {}
	void Visit(int64_t) override {}
	void Visit(double) override {}
	void Visit(const json::String& s) override { value = &s; }
	void Visit(bool) override {}
	void Visit(const json::Null&) override {}
};

std::string String(json::Object const& obj, const char *key) {
	auto it = obj.find(key);
	if (it == obj.end()) return {};
	StringOf s;
	it->second.Accept(s);
	return s.value ? *s.value : std::string();
}

template<class T> T const* Child(json::Object const& obj, const char *key) {
	auto it = obj.find(key);
	if (it == obj.end()) return nullptr;
	try { return &static_cast<T const&>(it->second); }
	catch (json::Exception const&) { return nullptr; }
}

int Ms(double seconds) { return static_cast<int>(std::lround(seconds * 1000)); }

std::string_view Trim(std::string_view s) {
	auto space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
	while (!s.empty() && space(s.front())) s.remove_prefix(1);
	while (!s.empty() && space(s.back())) s.remove_suffix(1);
	// The ideographic space
	while (s.starts_with("\xE3\x80\x80")) s.remove_prefix(3);
	while (s.ends_with("\xE3\x80\x80")) s.remove_suffix(3);
	return s;
}

bool EndsWithAny(std::string_view s, std::initializer_list<std::string_view> ends) {
	for (auto e : ends) if (s.ends_with(e)) return true;
	return false;
}
bool StartsWithAny(std::string_view s, std::initializer_list<std::string_view> starts) {
	for (auto e : starts) if (s.starts_with(e)) return true;
	return false;
}

/// Whether @p text ends a sentence, closing quotes aside
bool EndsSentence(std::string_view text) {
	text = Trim(text);
	for (bool again = true; again;) {
		again = false;
		for (std::string_view close : {"\xE3\x80\x8D", "\xE3\x80\x8F", "\xEF\xBC\x89", ")", "\""}) // 」』）
			if (text.ends_with(close)) { text.remove_suffix(close.size()); again = true; }
	}
	return EndsWithAny(text, {"\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81", "?", "!", "\xE2\x80\xA6", "\xEF\xBC\x8E", "."}); // 。？！…．
}

/// Whether @p text ends as a Japanese sentence does, punctuation or not:
/// です, ます, た, ね, よ... A pause after it ends the sentence.
bool EndsSentenceForm(std::string_view text) {
	if (EndsSentence(text)) return true;
	text = Trim(text);
	return EndsWithAny(text, {
		"\xE3\x81\xA7\xE3\x81\x99", // です
		"\xE3\x81\xBE\xE3\x81\x99", // ます
		"\xE3\x81\x9B\xE3\x82\x93", // せん
		"\xE3\x81\x95\xE3\x81\x84", // さい
		"\xE3\x81\x9F", "\xE3\x81\xA0", // た だ
		"\xE3\x81\xAD", "\xE3\x82\x88", "\xE3\x82\x8F", "\xE3\x81\x8B", "\xE3\x81\xAA", // ね よ わ か な
		"\xE3\x81\xAF\xE3\x81\x84", // はい
		"\xE3\x81\xA1\xE3\x81\xAF", "\xE3\x82\x93\xE3\x81\xAF", // (こんに)ちは (こんば)んは
		"\xE3\x81\x82\xE3\x82\x8A\xE3\x81\x8C\xE3\x81\xA8\xE3\x81\x86", // ありがとう
		"\xE3\x81\x8A\xE3\x81\xAF\xE3\x82\x88\xE3\x81\x86", // おはよう
	});
}

/// The first or last code point of @p s
char32_t FirstChar(std::string_view s) {
	if (s.empty()) return 0;
	unsigned char c = s[0];
	if (c < 0x80) return c;
	int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
	char32_t cp = c & (0x3F >> n);
	for (int i = 1; i <= n && i < static_cast<int>(s.size()); ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
	return cp;
}
char32_t LastChar(std::string_view s) {
	size_t i = s.size();
	while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
	return i > 0 ? FirstChar(s.substr(i - 1)) : 0;
}
bool Hiragana(char32_t c) { return c >= 0x3041 && c <= 0x309F; }

/// Languages written without spaces between words
bool Unspaced(std::string const& language) {
	for (auto l : {"ja", "zh", "yue", "th", "lo", "my", "km", "bo"})
		if (language == l) return true;
	return language.empty();
}

/// Not said but heard: "（笑い）", "(laughter)", "♪〜"; a line of it alone is
/// kept as a comment
bool Bracketed(std::string_view text) {
	std::string_view t = Trim(text);
	return (t.starts_with("\xEF\xBC\x88") && t.ends_with("\xEF\xBC\x89")) || (t.starts_with("(") && t.ends_with(")"));
}

bool IsNote(std::string_view text) {
	if (Bracketed(text)) return true;
	std::string_view t = Trim(text);
	if (t.empty()) return false;
	while (!t.empty()) {
		if (t.starts_with("\xE2\x99\xAA")) t.remove_prefix(3); // ♪
		else if (t.starts_with("\xE3\x80\x9C")) t.remove_prefix(3); // 〜
		else if (t.starts_with(" ")) t.remove_prefix(1);
		else return false;
	}
	return true;
}

struct Unit {
	std::vector<Word> words;
	std::string speaker;
	int group = 0;
	int Start() const { return words.front().start; }
	int End() const { return words.back().end; }
};

/// How long @p text takes to say, roughly, in morae: a kanji two, a small
/// kana or long vowel mark half, a digit two, other letters one
double Morae(std::string_view text) {
	double n = 0;
	for (size_t i = 0; i < text.size();) {
		unsigned char c = text[i];
		size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
		char32_t cp = FirstChar(text.substr(i));
		i += len;
		if (cp == ' ' || cp == 0x3000) continue;
		if (cp >= 0x4E00 && cp <= 0x9FFF) n += 2;
		else if (cp >= '0' && cp <= '9') n += 2;
		else if (cp == 0x30FC || cp == 0x3063 || cp == 0x30C3 || (cp >= 0x3041 && cp <= 0x3049 && cp % 2) || cp == 0x3083 || cp == 0x3085 || cp == 0x3087
			|| (cp >= 0x30A1 && cp <= 0x30A9 && cp % 2) || cp == 0x30E3 || cp == 0x30E5 || cp == 0x30E7) n += 0.5;
		else n += 1;
	}
	return n;
}

/// Whether a sentence cannot end just before @p next: ん, っ, ー or a small kana
bool Continues(char32_t next) {
	return next == 0x3093 || next == 0x3063 || next == 0x30FC || next == 0x30F3 || next == 0x30C3
		|| (next >= 0x3041 && next <= 0x3049 && next % 2) || next == 0x3083 || next == 0x3085 || next == 0x3087
		|| (next >= 0x30A1 && next <= 0x30A9 && next % 2) || next == 0x30E3 || next == 0x30E5 || next == 0x30E7;
}

class Maker {
	Rules const& rules;
	std::vector<int> const& cuts;
	std::vector<std::pair<int, int>> const& quiet;
	std::string language;
	bool unspaced;
	int next_group = 1;

	std::string Join(std::vector<Word>::const_iterator begin, std::vector<Word>::const_iterator end) const {
		std::string text;
		for (auto it = begin; it != end; ++it) {
			auto word = Trim(it->text);
			if (word.empty()) continue;
			if (!unspaced && !text.empty()) text += ' ';
			text += word;
		}
		return text;
	}

	/// The byte offsets in @p text a word may start or end at
	std::set<int> WordBoundaries(std::string const& text) const {
		std::set<int> out;
		UErrorCode err = U_ZERO_ERROR;
		agi::UTextPtr ut(utext_openUTF8(nullptr, text.data(), static_cast<int64_t>(text.size()), &err));
		std::unique_ptr<icu::BreakIterator> bi(icu::BreakIterator::createWordInstance(icu::Locale(language.empty() ? "ja" : language.c_str()), err));
		if (U_FAILURE(err) || !bi) return out;
		bi->setText(ut.get(), err);
		if (U_FAILURE(err)) return out;
		for (int32_t p = bi->first(); p != icu::BreakIterator::DONE; p = bi->next()) out.insert(p);
		return out;
	}

	/// The best place to split @p u, after its word at the index returned;
	/// -1 when no place leaves both pieces long enough
	int SplitPoint(Unit const& u) const {
		std::string const text = Join(u.words.begin(), u.words.end());
		auto const boundaries = WordBoundaries(text);
		double best = -1e9;
		int at = -1;
		int const span = std::max(1, u.End() - u.Start());
		for (size_t k = 0; k + 1 < u.words.size(); ++k) {
			int const left = u.words[k].end - u.Start(), right = u.End() - u.words[k + 1].start;
			if (left < rules.min_piece || right < rules.min_piece) continue;
			std::string const before = Join(u.words.begin(), u.words.begin() + k + 1);
			std::string const after = Join(u.words.begin() + k + 1, u.words.end());
			double score = 0;
			if (EndsWithAny(before, {"\xE3\x80\x81", "\xEF\xBC\x8C", ","})) score += 3; // 、，
			else if (EndsWithAny(before, {"\xE3\x81\x91\xE3\x81\xA9", "\xE3\x81\x8B\xE3\x82\x89", "\xE3\x81\xAE\xE3\x81\xA7", "\xE3\x81\xAE\xE3\x81\xAB"})) score += 2; // けど から ので のに
			else if (EndsWithAny(before, {"\xE3\x81\xA6", "\xE3\x81\xA7"})) score += 1; // て で
			// Never before what ends the sentence: ない です ます だ でしょ よ ね か の
			if (StartsWithAny(after, {"\xE3\x81\xAA\xE3\x81\x84", "\xE3\x81\xA7\xE3\x81\x99", "\xE3\x81\xBE\xE3\x81\x99", "\xE3\x81\xA0",
				"\xE3\x81\xA7\xE3\x81\x97\xE3\x82\x87", "\xE3\x82\x88", "\xE3\x81\xAD", "\xE3\x81\x8B", "\xE3\x81\xAE",
				"\xE3\x80\x81", "\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81", "?", "!", ","}))
				score -= 5;
			if (unspaced && !boundaries.empty() && !boundaries.count(static_cast<int>(before.size())))
				score -= 10;
			int const gap = std::max(0, u.words[k + 1].start - u.words[k].end);
			if (unspaced) {
				char32_t const last = LastChar(Trim(before)), next = FirstChar(Trim(after));
				// Kana on both sides with no pause: within a verb's endings,
				// which a dictionary still breaks (ござ|いました)
				if (Hiragana(last) && Hiragana(next) && gap < 150) score -= 4;
				// After a particle, before a new word in kanji or katakana
				else if (!Hiragana(next) && EndsWithAny(before, {"\xE3\x81\xAF", "\xE3\x81\x8C", "\xE3\x82\x92", "\xE3\x81\xAB",
					"\xE3\x81\xA7", "\xE3\x81\xA8", "\xE3\x82\x82", "\xE3\x81\xB8", "\xE3\x81\xAD"})) // は が を に で と も へ ね
					score += 1.5;
				// A sentence ended where Whisper wrote no stop
				if (gap >= 150 && EndsSentenceForm(before)) score += 3;
			}
			score += std::min(gap, 600) / 120.0;
			double const where = double(u.words[k].end - u.Start()) / span;
			score -= 6 * std::abs(where - 0.5);
			if (score > best) { best = score; at = static_cast<int>(k); }
		}
		return at;
	}

	void SplitAt(Unit &u, int k, std::vector<Unit> &out) {
		if (!u.group) u.group = next_group++;
		Unit first{{u.words.begin(), u.words.begin() + k + 1}, u.speaker, u.group};
		Unit second{{u.words.begin() + k + 1, u.words.end()}, u.speaker, u.group};
		out.push_back(std::move(first));
		out.push_back(std::move(second));
	}

	void SplitLong(Unit u, std::vector<Unit> &out) {
		if (u.End() - u.Start() <= rules.max_duration) { out.push_back(std::move(u)); return; }
		int k = SplitPoint(u);
		if (k < 0) { out.push_back(std::move(u)); return; }
		std::vector<Unit> pieces;
		SplitAt(u, k, pieces);
		for (auto& p : pieces) SplitLong(std::move(p), out);
	}

	/// Split at a cut both sides of which hold enough of the line, if a
	/// pause is near it
	void SplitAtCuts(Unit u, std::vector<Unit> &out) {
		for (int cut : cuts) {
			if (cut - u.Start() < rules.cut_side || u.End() - cut < rules.cut_side) continue;
			for (size_t k = 0; k + 1 < u.words.size(); ++k) {
				if (u.words[k + 1].start - u.words[k].end < rules.min_pause) continue;
				if (u.words[k].end - rules.cut_pause > cut || u.words[k + 1].start + rules.cut_pause < cut) continue;
				std::vector<Unit> pieces;
				SplitAt(u, static_cast<int>(k), pieces);
				out.push_back(std::move(pieces[0]));
				SplitAtCuts(std::move(pieces[1]), out);
				return;
			}
		}
		out.push_back(std::move(u));
	}

	Line ToLine(Unit const& u) const {
		Line line;
		line.start = u.Start();
		line.end = std::max(u.End(), line.start + 1);
		line.text = Join(u.words.begin(), u.words.end());
		line.speaker = u.speaker;
		line.group = u.group;
		// What is unsure goes by words: an aligner scores each character of
		// Japanese, how well it placed it rather than how surely it was heard,
		// so a word is marked only when its characters are on the whole
		struct Token { size_t begin, end; double sum = 0; int scored = 0; };
		std::vector<Token> tokens;
		std::vector<std::pair<size_t, size_t>> spans;
		{
			std::string text;
			for (auto const& w : u.words) {
				auto word = Trim(w.text);
				if (!unspaced && !text.empty() && !word.empty()) text += ' ';
				spans.emplace_back(text.size(), text.size() + word.size());
				text += word;
			}
			if (unspaced) {
				auto bounds = WordBoundaries(text);
				bounds.insert(0);
				bounds.insert(static_cast<int>(text.size()));
				for (auto it = bounds.begin(); std::next(it) != bounds.end(); ++it)
					tokens.push_back({static_cast<size_t>(*it), static_cast<size_t>(*std::next(it))});
			}
			else
				for (auto const& span : spans) tokens.push_back({span.first, span.second});
			for (size_t i = 0; i < u.words.size(); ++i) {
				if (u.words[i].score < 0 || spans[i].first == spans[i].second) continue;
				for (auto& t : tokens)
					if (spans[i].first < t.end && spans[i].second > t.begin) { t.sum += u.words[i].score; ++t.scored; }
			}
			auto unsure = [&](Token const& t) {
				if (!t.scored || t.sum / t.scored >= rules.unsure_below) return false;
				// Punctuation alone is nothing to check
				auto word = Trim(std::string_view(text).substr(t.begin, t.end - t.begin));
				return !word.empty() && !EndsWithAny(word, {"\xE3\x80\x81", "\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81", "?", "!", ",", "."});
			};
			for (size_t i = 0; i < tokens.size();) {
				if (!unsure(tokens[i])) { ++i; continue; }
				size_t j = i;
				while (j < tokens.size() && unsure(tokens[j])) ++j;
				line.unsure.emplace_back(Trim(std::string_view(text).substr(tokens[i].begin, tokens[j - 1].end - tokens[i].begin)));
				i = j;
			}
		}
		line.note = IsNote(line.text);
		return line;
	}

	/// The first cut in [from, to), or INT_MAX
	int FirstCut(int from, int to) const {
		auto it = std::lower_bound(cuts.begin(), cuts.end(), from);
		return it != cuts.end() && *it < to ? *it : INT_MAX;
	}
	/// The last cut in (from, to], or INT_MIN
	int LastCut(int from, int to) const {
		auto it = std::upper_bound(cuts.begin(), cuts.end(), to);
		if (it == cuts.begin()) return INT_MIN;
		--it;
		return *it > from ? *it : INT_MIN;
	}

	void Time(std::vector<Line> &lines) const {
		std::vector<Line> const said = lines;
		for (size_t i = 0; i < lines.size(); ++i) {
			auto const& s = said[i];
			// The neighbours bound it, unless already overlapping them: a
			// pause too short for both leads shared between them in proportion
			auto meet = [&](Line const& a, Line const& b) {
				int const gap = b.start - a.end;
				int const leads = std::max(1, rules.lead_in + rules.lead_out);
				return gap >= leads ? -1 : a.end + static_cast<int>(int64_t(gap) * rules.lead_out / leads);
			};
			int prev_end = 0, next_start = INT_MAX;
			if (i > 0 && said[i - 1].end <= s.start) {
				int m = meet(said[i - 1], s);
				prev_end = m < 0 ? said[i - 1].end : m;
			}
			if (i + 1 < said.size() && said[i + 1].start >= s.end) {
				int m = meet(s, said[i + 1]);
				next_start = m < 0 ? said[i + 1].start : m;
			}
			int start = s.start - rules.lead_in;
			if (int cut = LastCut(start - 1, s.start); cut != INT_MIN) start = cut;
			start = std::max({start, prev_end, 0});
			int end = s.end + rules.lead_out;
			if (int cut = FirstCut(s.end, end); cut != INT_MAX) end = cut;
			end = std::min(end, next_start);
			if (end - start < rules.min_duration) {
				int longer = start + rules.min_duration;
				if (int cut = FirstCut(s.end + 1, longer + 1); cut != INT_MAX) longer = cut;
				end = std::max(end, std::min(longer, next_start));
			}
			lines[i].start = std::min(start, s.start);
			lines[i].end = std::max(end, s.end);
		}
		// Near lines meet: at the cut between them if there is one
		for (size_t i = 0; i + 1 < lines.size(); ++i) {
			int const gap = lines[i + 1].start - lines[i].end;
			if (gap <= 0 || gap > rules.adjacent_gap) continue;
			if (int cut = FirstCut(lines[i].end, lines[i + 1].start + 1); cut != INT_MAX)
				lines[i].end = lines[i + 1].start = cut;
			else
				lines[i].end = lines[i + 1].start;
		}
		// A cut just inside an end takes it
		for (auto& line : lines) {
			for (int cut = FirstCut(line.start + 1, line.end); cut != INT_MAX; cut = FirstCut(cut + 1, line.end)) {
				if (cut - line.start < rules.cut_side) line.start = cut;
				else if (line.end - cut < rules.cut_side) { line.end = cut; break; }
			}
			line.end = std::max(line.end, line.start + 1);
		}
	}

	/// Where in the audio each segment's sentences end. WhisperX's times for
	/// Japanese, a character at a time, can be seconds off, so a sentence end
	/// is looked for both where it puts it and where it would be at an even
	/// pace through the segment's speech; and the sentence ends, in order,
	/// are paired with the pauses, in order, the nearest they can be.
	/// @return For each word a sentence ends after, the pause after it
	std::map<std::pair<size_t, size_t>, std::pair<int, int>> PauseBreaks(std::vector<std::vector<Word>> const& segments) const {
		std::map<std::pair<size_t, size_t>, std::pair<int, int>> breaks;
		if (quiet.empty() || !unspaced) return breaks;

		auto in_quiet = [&](int t) -> std::pair<int, int> const* {
			for (auto const& q : quiet) if (q.first <= t && t < q.second) return &q;
			return nullptr;
		};

		struct Candidate { size_t seg, word; int said, even; bool sure; };
		std::vector<Candidate> candidates;
		for (size_t s = 0; s < segments.size(); ++s) {
			auto const& words = segments[s];
			// The segment's speech: from its first word to the next segment's,
			// or for the last, to the pause the sound ends with
			int lo = words.front().start;
			int hi = s + 1 < segments.size() ? segments[s + 1].front().start : words.back().end;
			if (s + 1 == segments.size() && quiet.back().first >= hi) hi = quiet.back().first;
			if (auto q = in_quiet(lo)) lo = q->second;
			if (auto q = in_quiet(hi - 1)) hi = q->first;
			if (hi <= lo) continue;
			// Its speaking time, without the pauses within
			std::vector<std::pair<int, int>> speech;
			int at = lo;
			for (auto const& q : quiet) {
				if (q.second <= at || q.first >= hi) continue;
				if (q.first > at) speech.emplace_back(at, q.first);
				at = std::max(at, q.second);
			}
			if (at < hi) speech.emplace_back(at, hi);
			double total_time = 0;
			for (auto const& sp : speech) total_time += sp.second - sp.first;
			double total_morae = 0;
			for (auto const& w : words) total_morae += Morae(w.text);
			if (total_time <= 0 || total_morae <= 0) continue;
			auto even_time = [&](double morae) {
				double left = morae / total_morae * total_time;
				for (auto const& sp : speech) {
					if (left <= sp.second - sp.first) return sp.first + static_cast<int>(left);
					left -= sp.second - sp.first;
				}
				return speech.back().second;
			};

			double morae = 0;
			for (size_t i = 0; i < words.size(); ++i) {
				morae += Morae(words[i].text);
				bool const last = i + 1 == words.size();
				if (!last && Continues(FirstChar(Trim(words[i + 1].text)))) continue;
				std::string const before = Join(words.begin() + (i >= 8 ? i - 8 : 0), words.begin() + i + 1);
				if (!EndsSentenceForm(before)) continue;
				if (!last) {
					// Not within a word: くだ|さい, ありがとう|ございます
					std::string const after = Join(words.begin() + i + 1, words.begin() + std::min(words.size(), i + 9));
					if (after.starts_with("\xE3\x81\x94\xE3\x81\x96") // ござ
						|| after.starts_with("\xE3\x81\x95\xE3\x81\x84") // (くだ, な)さい
						|| after.starts_with("\xE3\x81\xAD") || after.starts_with("\xE3\x82\x88")) continue; // ね よ
					std::string const around = before + after;
					if (!WordBoundaries(around).count(static_cast<int>(before.size()))) continue;
				}
				// た, だ, か... end a sentence less surely than です or ました
				bool const sure = EndsSentence(before) || !EndsWithAny(Trim(before), {
					"\xE3\x81\x9F", "\xE3\x81\xA0", "\xE3\x82\x88", "\xE3\x82\x8F", "\xE3\x81\x8B", "\xE3\x81\xAA"}) // た だ よ わ か な
					|| EndsWithAny(Trim(before), {"\xE3\x81\x97\xE3\x81\x9F", "\xE3\x81\xA3\xE3\x81\x9F"}); // した った
				candidates.push_back({s, i, words[i].end, even_time(morae), sure});
			}
		}

		// The pauses long enough to end a sentence at, paired in order with
		// the sentence ends: the most pairs, each its nearest
		constexpr int reach = 2000;
		auto distance = [](int t, std::pair<int, int> const& q) { return std::max({0, q.first - t, t - q.second}); };
		auto score = [&](Candidate const& c, std::pair<int, int> const& q) {
			int d = std::min(distance(c.said, q), distance(c.even, q));
			return d > reach ? -1 : reach - d + std::min(q.second - q.first, rules.pause_break) - (c.sure ? 0 : reach / 2);
		};
		size_t const n = candidates.size(), m = quiet.size();
		std::vector<std::vector<int>> best(n + 1, std::vector<int>(m + 1, 0));
		for (size_t i = 1; i <= n; ++i) {
			for (size_t j = 1; j <= m; ++j) {
				best[i][j] = std::max(best[i - 1][j], best[i][j - 1]);
				int sc = score(candidates[i - 1], quiet[j - 1]);
				if (sc >= 0) best[i][j] = std::max(best[i][j], best[i - 1][j - 1] + sc);
			}
		}
		for (size_t i = n, j = m; i > 0 && j > 0;) {
			if (best[i][j] == best[i - 1][j]) --i;
			else if (best[i][j] == best[i][j - 1]) --j;
			else {
				breaks[{candidates[i - 1].seg, candidates[i - 1].word}] = quiet[j - 1];
				--i; --j;
			}
		}
		return breaks;
	}

	/// @p u's words put, in proportion, between @p start and @p end
	static void Retime(Unit &u, int start, int end) {
		int const from = u.Start(), to = u.End();
		end = std::max(end, start + 1);
		double const scale = to > from ? double(end - start) / (to - from) : 0;
		for (auto& w : u.words) {
			w.start = start + static_cast<int>((w.start - from) * scale);
			w.end = start + static_cast<int>((w.end - from) * scale);
		}
		u.words.back().end = end;
	}

public:
	Maker(Rules const& rules, std::vector<int> const& cuts, std::vector<std::pair<int, int>> const& quiet, std::string language)
	: rules(rules), cuts(cuts), quiet(quiet), language(std::move(language)), unspaced(Unspaced(this->language)) { }

	std::vector<Line> Make(Transcript const& t) {
		// Each segment's words, with times for those the aligner left out
		std::vector<std::vector<Word>> segments;
		for (auto const& seg : t.segments) {
			std::vector<Word> words = seg.words;
			if (words.empty()) words.push_back({seg.text, seg.start, seg.end, -1, seg.speaker});
			for (size_t i = 0; i < words.size(); ++i) {
				if (words[i].speaker.empty()) words[i].speaker = seg.speaker;
				if (words[i].start < 0) words[i].start = i ? words[i - 1].end : seg.start;
			}
			for (size_t i = words.size(); i-- > 0;) {
				if (words[i].end < 0) {
					int next = seg.end;
					for (size_t j = i + 1; j < words.size(); ++j) if (words[j].start >= 0) { next = words[j].start; break; }
					words[i].end = next;
				}
				words[i].end = std::max(words[i].end, words[i].start);
			}
			std::erase_if(words, [](Word const& w) { return Trim(w.text).empty(); });
			if (!words.empty()) segments.push_back(std::move(words));
		}

		// Sentences: a segment joins the one before when that did not end
		// one, the pause is short and the speaker the same; within one, a
		// sentence end, a long pause or another speaker starts the next.
		// With the audio's pauses known, they say where sentences end and
		// the words' own gaps do not.
		auto const pause_breaks = PauseBreaks(segments);
		bool const by_pauses = !pause_breaks.empty();
		std::vector<Unit> units;
		std::vector<std::pair<int, int>> unit_times; // -1 where the words' own
		std::pair<int, int> const* pending = nullptr;
		for (size_t si = 0; si < segments.size(); ++si) {
			auto const& words = segments[si];
			for (size_t i = 0; i < words.size(); ++i) {
				auto const& w = words[i];
				bool start_new = units.empty();
				if (!start_new) {
					auto const& prev = units.back().words.back();
					bool const other_speaker = !w.speaker.empty() && !prev.speaker.empty() && w.speaker != prev.speaker;
					// A sound heard, "(laughter)", is a line of its own
					bool const note_edge = Bracketed(w.text) || Bracketed(prev.text);
					int const gap = w.start - prev.end;
					std::string const so_far = Join(units.back().words.begin(), units.back().words.end());
					bool const ended = EndsSentence(so_far) || pending
						|| (!by_pauses && gap >= rules.sentence_pause && EndsSentenceForm(so_far));
					if (i == 0)
						start_new = other_speaker || note_edge || ended || (!by_pauses && gap > rules.join_gap) || w.end - units.back().Start() > rules.max_duration;
					else
						start_new = other_speaker || note_edge || ended || (!by_pauses && gap > rules.pause_break);
				}
				if (start_new) {
					if (pending) unit_times.back().second = pending->first;
					units.push_back({{}, w.speaker, 0});
					unit_times.emplace_back(pending ? pending->second : -1, -1);
				}
				pending = nullptr;
				units.back().words.push_back(w);
				auto it = pause_breaks.find({si, i});
				if (it != pause_breaks.end()) pending = &it->second;
			}
		}
		if (pending && !unit_times.empty()) unit_times.back().second = pending->first;
		for (size_t k = 0; k < units.size(); ++k) {
			auto [start, end] = unit_times[k];
			if (start < 0 && end < 0) continue;
			if (start < 0) start = std::min(units[k].Start(), end - 1);
			if (end < 0) {
				end = units[k].End();
				// Its words heard before where it begins: to the pause after
				if (end <= start) {
					end = start + (units[k].End() - units[k].Start());
					for (auto const& q : quiet) if (q.first > start) { end = q.first; break; }
				}
			}
			Retime(units[k], start, end);
		}

		std::vector<Unit> split;
		for (auto& u : units) {
			std::vector<Unit> long_split;
			SplitLong(std::move(u), long_split);
			for (auto& p : long_split) SplitAtCuts(std::move(p), split);
		}

		std::vector<Line> lines;
		for (auto const& u : split) lines.push_back(ToLine(u));
		std::stable_sort(lines.begin(), lines.end(), [](Line const& a, Line const& b) { return a.start < b.start; });
		Time(lines);
		return lines;
	}
};
}

namespace {
/// ISO 639-3, as ElevenLabs writes, to the 639-1 the rest of this and ICU
/// know, for the languages that need it; others are kept as they are
std::string ShortLanguage(std::string const& code) {
	static const std::pair<const char *, const char *> codes[] = {
		{"jpn", "ja"}, {"zho", "zh"}, {"cmn", "zh"}, {"tha", "th"}, {"lao", "lo"},
		{"mya", "my"}, {"khm", "km"}, {"bod", "bo"}, {"eng", "en"}, {"kor", "ko"},
		{"fra", "fr"}, {"deu", "de"}, {"spa", "es"}, {"ita", "it"}, {"por", "pt"},
		{"rus", "ru"}, {"vie", "vi"}, {"ind", "id"}, {"ara", "ar"}, {"hin", "hi"},
	};
	for (auto [long_code, short_code] : codes)
		if (code == long_code) return short_code;
	return code;
}

/// ElevenLabs Scribe's: one list of words, spaces and sounds, in seconds,
/// a speaker_id on each if diarized. Made into a piece for each run of one
/// speaker, a sound heard its own piece.
Transcript ReadScribe(json::Object const& obj, json::Array const& words) {
	Transcript t;
	t.language = ShortLanguage(String(obj, "language_code"));
	Segment *seg = nullptr;
	bool seg_is_event = false;
	for (auto const& we : words) {
		json::Object const* w;
		try { w = &static_cast<json::Object const&>(we); }
		catch (json::Exception const&) { continue; }
		std::string const type = String(*w, "type");
		if (type == "spacing") continue;
		Word word;
		word.text = String(*w, "text");
		if (Trim(word.text).empty()) continue;
		double v;
		if (Number(*w, "start", v)) word.start = Ms(v);
		if (Number(*w, "end", v)) word.end = Ms(v);
		// A log of the probability
		if (Number(*w, "logprob", v) && v <= 0) word.score = std::exp(v);
		word.speaker = String(*w, "speaker_id");
		bool const event = type == "audio_event";
		if (event && !IsNote(word.text)) word.text = "(" + std::string(Trim(word.text)) + ")";
		if (!seg || event || seg_is_event || word.speaker != seg->speaker) {
			t.segments.emplace_back();
			seg = &t.segments.back();
			seg->speaker = word.speaker;
			seg->start = word.start;
			seg_is_event = event;
		}
		if (seg->start < 0) seg->start = word.start;
		if (word.end >= 0) seg->end = word.end;
		else if (word.start >= 0) seg->end = word.start;
		seg->words.push_back(std::move(word));
	}
	for (auto& s : t.segments) {
		if (s.start < 0) s.start = 0;
		s.end = std::max(s.end, s.start);
	}
	return t;
}
}

Transcript ReadJson(std::istream &stream) {
	json::UnknownElement root;
	try { json::Reader::Read(root, stream); }
	catch (json::Exception const& e) { throw agi::InvalidInputException(std::string("Not a speech recognition JSON file: ") + e.what()); }
	json::Object const* obj;
	try { obj = &static_cast<json::Object const&>(root); }
	catch (json::Exception const&) { throw agi::InvalidInputException("Not a speech recognition JSON file: it holds no object"); }
	auto segments = Child<json::Array>(*obj, "segments");
	if (!segments) {
		if (auto words = Child<json::Array>(*obj, "words")) return ReadScribe(*obj, *words);
		if (Child<json::Array>(*obj, "transcripts"))
			throw agi::InvalidInputException("ElevenLabs gave a transcript for each channel: ask for them combined (multichannel_output_style=combined)");
		throw agi::InvalidInputException("Not a speech recognition JSON file: it has no \"segments\" or \"words\"");
	}

	Transcript t;
	t.language = String(*obj, "language");
	for (auto const& element : *segments) {
		json::Object const* seg;
		try { seg = &static_cast<json::Object const&>(element); }
		catch (json::Exception const&) { continue; }
		Segment s;
		double v;
		if (!Number(*seg, "start", v)) continue;
		s.start = Ms(v);
		s.end = Number(*seg, "end", v) ? Ms(v) : s.start;
		s.text = String(*seg, "text");
		s.speaker = String(*seg, "speaker");
		if (auto words = Child<json::Array>(*seg, "words")) {
			for (auto const& we : *words) {
				json::Object const* w;
				try { w = &static_cast<json::Object const&>(we); }
				catch (json::Exception const&) { continue; }
				Word word;
				word.text = String(*w, "word");
				if (word.text.empty()) word.text = String(*w, "text");
				if (Number(*w, "start", v)) word.start = Ms(v);
				if (Number(*w, "end", v)) word.end = Ms(v);
				if (Number(*w, "score", v) || Number(*w, "probability", v)) word.score = v;
				word.speaker = String(*w, "speaker");
				s.words.push_back(std::move(word));
			}
		}
		t.segments.push_back(std::move(s));
	}
	return t;
}

std::vector<float> Levels(agi::AudioProvider const& audio, int step_ms) {
	std::vector<float> levels;
	int const rate = audio.GetSampleRate(), channels = audio.GetChannels(), bytes = audio.GetBytesPerSample();
	bool const is_float = audio.AreSamplesFloat();
	if (rate <= 0 || channels <= 0 || (bytes != 2 && !(is_float && bytes == 4))) return levels;
	int64_t const step = int64_t(rate) * step_ms / 1000;
	if (step <= 0) return levels;
	std::vector<char> buf(static_cast<size_t>(step * channels * bytes));
	for (int64_t start = 0; start + step <= audio.GetNumSamples(); start += step) {
		audio.GetAudio(buf.data(), start, step);
		double sum = 0;
		for (int64_t i = 0; i < step * channels; ++i) {
			double v = is_float ? reinterpret_cast<float const*>(buf.data())[i] : reinterpret_cast<int16_t const*>(buf.data())[i] / 32768.0;
			sum += v * v;
		}
		levels.push_back(static_cast<float>(10 * std::log10(sum / double(step * channels) + 1e-10)));
	}
	return levels;
}


std::vector<std::pair<int, int>> FindQuiet(std::vector<float> const& levels, int step_ms, int min_ms) {
	std::vector<std::pair<int, int>> quiet;
	if (levels.size() < 10 || step_ms <= 0) return quiet;
	auto sorted = levels;
	std::sort(sorted.begin(), sorted.end());
	float const floor = sorted[sorted.size() / 10];
	float const loud = sorted[sorted.size() * 95 / 100];
	float const threshold = std::max(floor + 6.f, floor + 0.3f * (loud - floor));
	for (size_t i = 0; i < levels.size();) {
		if (levels[i] >= threshold) { ++i; continue; }
		size_t j = i;
		while (j < levels.size() && levels[j] < threshold) ++j;
		if (static_cast<int>(j - i) * step_ms >= min_ms)
			quiet.emplace_back(static_cast<int>(i) * step_ms, static_cast<int>(j) * step_ms);
		i = j;
	}
	return quiet;
}

void TrimToSpeech(Transcript &transcript, std::vector<std::pair<int, int>> const& quiet) {
	if (quiet.empty()) return;
	for (auto& seg : transcript.segments) {
		for (auto& w : seg.words) {
			if (w.start < 0 || w.end <= w.start) continue;
			// The parts of the word outside the quiet: its first and last
			int first = -1, last = -1, at = w.start;
			auto it = std::upper_bound(quiet.begin(), quiet.end(), std::make_pair(w.start, INT_MAX));
			if (it != quiet.begin()) --it;
			for (; it != quiet.end() && it->first < w.end; ++it) {
				if (it->second <= at) continue;
				if (it->first > at) {
					if (first < 0) first = at;
					last = it->first;
				}
				at = std::max(at, it->second);
			}
			if (at < w.end) {
				if (first < 0) first = at;
				last = w.end;
			}
			if (first < 0) continue; // All quiet: as the aligner put it
			w.start = first;
			w.end = std::max(last, first + 1);
		}
	}
}

std::vector<Line> MakeLines(Transcript const& transcript, Rules const& rules, std::vector<int> const& cuts, std::vector<std::pair<int, int>> const& quiet) {
	return Maker(rules, cuts, quiet, transcript.language).Make(transcript);
}
}
