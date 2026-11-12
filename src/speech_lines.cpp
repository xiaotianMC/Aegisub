// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "speech_lines.h"

#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/visitor.h>
#include <libaegisub/exception.h>
#include <libaegisub/unicode.h>

#include <algorithm>
#include <climits>
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

/// Languages written without spaces between words
bool Unspaced(std::string const& language) {
	for (auto l : {"ja", "zh", "yue", "th", "lo", "my", "km", "bo"})
		if (language == l) return true;
	return language.empty();
}

struct Unit {
	std::vector<Word> words;
	std::string speaker;
	int group = 0;
	int Start() const { return words.front().start; }
	int End() const { return words.back().end; }
};

class Maker {
	Rules const& rules;
	std::vector<int> const& cuts;
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
		for (size_t i = 0; i < u.words.size();) {
			auto unsure = [&](Word const& w) {
				if (w.score < 0 || w.score >= rules.unsure_below) return false;
				// Punctuation alone is nothing to check
				auto t = Trim(w.text);
				return !t.empty() && !EndsWithAny(t, {"\xE3\x80\x81", "\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81", "?", "!", ",", "."});
			};
			if (!unsure(u.words[i])) { ++i; continue; }
			size_t j = i;
			while (j < u.words.size() && unsure(u.words[j])) ++j;
			line.unsure.push_back(Join(u.words.begin() + i, u.words.begin() + j));
			i = j;
		}
		std::string_view t = Trim(line.text);
		bool bracketed = (t.starts_with("\xEF\xBC\x88") && t.ends_with("\xEF\xBC\x89")) || (t.starts_with("(") && t.ends_with(")"));
		bool only_music = !t.empty();
		for (std::string_view rest = t; !rest.empty();) {
			if (rest.starts_with("\xE2\x99\xAA")) rest.remove_prefix(3); // ♪
			else if (rest.starts_with("\xE3\x80\x9C")) rest.remove_prefix(3); // 〜
			else if (rest.starts_with(" ")) rest.remove_prefix(1);
			else { only_music = false; break; }
		}
		line.note = bracketed || only_music;
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
			// The neighbours bound it, unless already overlapping them
			int const prev_end = i > 0 && said[i - 1].end <= s.start ? said[i - 1].end : 0;
			int const next_start = i + 1 < said.size() && said[i + 1].start >= s.end ? said[i + 1].start : INT_MAX;
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

public:
	Maker(Rules const& rules, std::vector<int> const& cuts, std::string language)
	: rules(rules), cuts(cuts), language(std::move(language)), unspaced(Unspaced(this->language)) { }

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
		// sentence end, a long pause or another speaker starts the next
		std::vector<Unit> units;
		for (size_t si = 0; si < segments.size(); ++si) {
			auto const& words = segments[si];
			for (size_t i = 0; i < words.size(); ++i) {
				auto const& w = words[i];
				bool start_new = units.empty();
				if (!start_new) {
					auto const& prev = units.back().words.back();
					bool const other_speaker = !w.speaker.empty() && !prev.speaker.empty() && w.speaker != prev.speaker;
					int const gap = w.start - prev.end;
					if (i == 0)
						start_new = other_speaker || EndsSentence(Join(units.back().words.begin(), units.back().words.end()))
							|| gap > rules.join_gap || w.end - units.back().Start() > rules.max_duration;
					else
						start_new = other_speaker || EndsSentence(prev.text) || gap > rules.pause_break;
				}
				if (start_new) units.push_back({{}, w.speaker, 0});
				units.back().words.push_back(w);
			}
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

Transcript ReadJson(std::istream &stream) {
	json::UnknownElement root;
	try { json::Reader::Read(root, stream); }
	catch (json::Exception const& e) { throw agi::InvalidInputException(std::string("Not a speech recognition JSON file: ") + e.what()); }
	json::Object const* obj;
	try { obj = &static_cast<json::Object const&>(root); }
	catch (json::Exception const&) { throw agi::InvalidInputException("Not a speech recognition JSON file: it holds no object"); }
	auto segments = Child<json::Array>(*obj, "segments");
	if (!segments) throw agi::InvalidInputException("Not a speech recognition JSON file: it has no \"segments\"");

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

std::vector<Line> MakeLines(Transcript const& transcript, Rules const& rules, std::vector<int> const& cuts) {
	return Maker(rules, cuts, transcript.language).Make(transcript);
}
}
