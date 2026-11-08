// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "animation_tags.h"

#include "style_edit.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace animation {
namespace {
/// The {…} blocks of @p text holding tags: byte offsets of the opening
/// brace and one past the closing one
std::vector<std::pair<size_t, size_t>> TagBlocks(std::string const& text) {
	std::vector<std::pair<size_t, size_t>> blocks;
	for (size_t i = 0; i < text.size(); ) {
		if (text[i] == '{') {
			size_t close = text.find('}', i);
			if (close == std::string::npos) break;
			std::string_view content(text.data() + i + 1, close - i - 1);
			if (content.empty() || content.find('\\') != std::string_view::npos)
				blocks.emplace_back(i, close + 1);
			i = close + 1;
			continue;
		}
		++i;
	}
	return blocks;
}

/// The tags of a block's content one by one, each from its backslash; what
/// is in parentheses (a \t's tags) stays inside its tag
std::vector<std::string_view> Pieces(std::string_view content) {
	std::vector<std::string_view> pieces;
	int depth = 0;
	size_t start = std::string_view::npos;
	for (size_t i = 0; i < content.size(); ++i) {
		char ch = content[i];
		if (depth > 0) {
			if (ch == '(') ++depth;
			else if (ch == ')') --depth;
			continue;
		}
		if (ch == '\\') {
			if (start != std::string_view::npos) pieces.push_back(content.substr(start, i - start));
			start = i;
		}
		else if (ch == '(')
			++depth;
	}
	if (start != std::string_view::npos) pieces.push_back(content.substr(start));
	return pieces;
}

/// The name of the tag @p piece, among those read here; empty for others
std::string_view NameOf(std::string_view piece) {
	for (std::string_view name : {"\\move", "\\fade", "\\fad", "\\org", "\\pos", "\\t"}) {
		if (!piece.starts_with(name)) continue;
		std::string_view rest = piece.substr(name.size());
		// \t is followed by its parentheses, not other letters (\tag is junk)
		if (name == "\\t" && (rest.empty() || rest.front() != '(')) continue;
		if (!rest.empty() && rest.front() != '(' && rest.front() != ' ') continue;
		return name;
	}
	return {};
}

/// What is in the parentheses after a tag's name, without them
std::string_view Inside(std::string_view piece) {
	size_t open = piece.find('(');
	if (open == std::string_view::npos) return {};
	std::string_view rest = piece.substr(open + 1);
	// The closing parenthesis can be left out at the end of a block
	if (!rest.empty() && rest.back() == ')') rest.remove_suffix(1);
	return rest;
}

std::string Trim(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
	return std::string(s);
}

/// @p args split at its commas
std::vector<double> Numbers(std::string_view args) {
	std::vector<double> numbers;
	while (true) {
		size_t comma = args.find(',');
		std::string item = Trim(args.substr(0, comma));
		if (!item.empty()) numbers.push_back(std::strtod(item.c_str(), nullptr));
		if (comma == std::string_view::npos) break;
		args.remove_prefix(comma + 1);
	}
	return numbers;
}

int Ms(double value) { return static_cast<int>(std::lround(value)); }

std::optional<Transform> ReadTransform(std::string_view piece, int duration) {
	std::string_view inside = Inside(piece);
	size_t tags_at = inside.find('\\');
	if (tags_at == std::string_view::npos) return std::nullopt;
	Transform t;
	t.targets = SplitTargets(std::string(inside.substr(tags_at)));
	auto numbers = Numbers(inside.substr(0, tags_at));
	t.t1 = 0;
	t.t2 = duration;
	if (numbers.size() == 1) t.accel = numbers[0];
	else if (numbers.size() >= 2) {
		t.t1 = Ms(numbers[0]);
		t.t2 = Ms(numbers[1]);
		if (numbers.size() >= 3) t.accel = numbers[2];
		// \t(0,0,…) is over the whole line, as libass and VSFilter have it
		if (t.t1 == 0 && t.t2 == 0) t.t2 = duration;
	}
	return t;
}

std::string Point(double x, double y) { return Number(x) + "," + Number(y); }

/// Whether @p t1 to @p t2 is the whole line
bool Whole(int t1, int t2, int duration) { return t1 <= 0 && t2 >= duration; }
}

std::string Number(double value) {
	double rounded = std::round(value * 1000) / 1000;
	if (rounded == 0) rounded = 0; // no "-0"
	return agi::format("%g", rounded);
}

bool Fade::Simple(int duration) const {
	return a1 == 255 && a2 == 0 && a3 == 255 && t1 == 0 && t4 == duration && t2 <= t3;
}

std::vector<Target> SplitTargets(std::string const& tags) {
	std::vector<Target> targets;
	for (auto piece : Pieces(tags)) {
		// The name: the letters after the backslash, and the digit of
		// \1c…\4a. Without letters (a stray "\"), the whole piece.
		size_t end = 1;
		if (end < piece.size() && piece[end] >= '1' && piece[end] <= '4') ++end;
		while (end < piece.size() && std::isalpha(static_cast<unsigned char>(piece[end]))) ++end;
		std::string name(piece.substr(0, end));
		// \fscx and \fscy, not \fsc with "x"; \fs+ and \fs- stay names
		static const char *const longest[] = {"\\fscx", "\\fscy", "\\fsp", "\\fs", "\\frx", "\\fry", "\\frz",
			"\\fr", "\\fax", "\\fay", "\\fn", "\\fe", "\\xbord", "\\ybord", "\\bord", "\\xshad", "\\yshad",
			"\\shad", "\\blur", "\\be", "\\alpha", "\\clip", "\\iclip"};
		for (auto candidate : longest)
			if (piece.starts_with(candidate)) { name = candidate; break; }
		targets.push_back({name, Trim(piece.substr(name.size()))});
	}
	return targets;
}

std::string JoinTargets(std::vector<Target> const& targets) {
	std::string tags;
	for (auto const& target : targets) tags += target.name + target.value;
	return tags;
}

std::string TransformTag(Transform const& t, int duration) {
	std::string tag = "\\t(";
	if (!Whole(t.t1, t.t2, duration))
		tag += agi::format("%d,%d,", std::max(0, t.t1), std::max(0, t.t2));
	if (std::abs(t.accel - 1) > 1e-6) tag += Number(t.accel) + ",";
	return tag + JoinTargets(t.targets) + ")";
}

Animation Read(std::string const& text, int duration) {
	Animation result;
	auto blocks = TagBlocks(text);
	bool placed = false;
	for (size_t b = 0; b < blocks.size(); ++b) {
		auto [begin, end] = blocks[b];
		std::string_view content(text.data() + begin + 1, end - begin - 2);
		for (auto piece : Pieces(content)) {
			auto name = NameOf(piece);
			if (name.empty()) continue;
			if (name == "\\t") {
				// Those of the block the line starts with
				if (b == 0 && begin == 0)
					if (auto t = ReadTransform(piece, duration)) result.transforms.push_back(*t);
				continue;
			}
			auto n = Numbers(Inside(piece));
			// The first of each counts, as the renderers have it
			if ((name == "\\move" || name == "\\pos") && !placed) {
				if (name == "\\move" && n.size() >= 4) {
					Move m{n[0], n[1], n[2], n[3], 0, duration};
					if (n.size() >= 6) {
						m.t1 = Ms(n[4]);
						m.t2 = Ms(n[5]);
						if (m.t1 == 0 && m.t2 == 0) m.t2 = duration;
						if (m.t1 > m.t2) std::swap(m.t1, m.t2);
					}
					result.move = m;
					placed = true;
				}
				else if (name == "\\pos" && n.size() >= 2) {
					result.position = std::make_pair(n[0], n[1]);
					placed = true;
				}
			}
			else if (name == "\\fad" && !result.fade && n.size() >= 2)
				result.fade = Fade{255, 0, 255, 0, Ms(n[0]), duration - Ms(n[1]), duration};
			else if (name == "\\fade" && !result.fade && n.size() >= 7)
				result.fade = Fade{Ms(n[0]), Ms(n[1]), Ms(n[2]), Ms(n[3]), Ms(n[4]), Ms(n[5]), Ms(n[6])};
			else if (name == "\\org" && !result.origin && n.size() >= 2)
				result.origin = Origin{n[0], n[1]};
		}
	}
	return result;
}

std::string WriteMove(std::string const& text, std::optional<Move> const& move, int duration) {
	std::string tag;
	if (move) {
		tag = "\\move(" + Point(move->x1, move->y1) + "," + Point(move->x2, move->y2);
		if (!Whole(move->t1, move->t2, duration))
			tag += agi::format(",%d,%d", std::max(0, move->t1), std::max(0, move->t2));
		tag += ")";
	}
	return style_edit::SetForLine(text, {"\\move", "\\pos"}, tag);
}

std::string WritePosition(std::string const& text, double x, double y) {
	return style_edit::SetForLine(text, {"\\move", "\\pos"}, "\\pos(" + Point(x, y) + ")");
}

std::string WriteFade(std::string const& text, std::optional<Fade> const& fade, int duration) {
	std::string tag;
	if (fade && fade->Simple(duration))
		tag = agi::format("\\fad(%d,%d)", std::max(0, fade->t2), std::max(0, duration - fade->t3));
	else if (fade)
		tag = agi::format("\\fade(%d,%d,%d,%d,%d,%d,%d)", fade->a1, fade->a2, fade->a3,
			std::max(0, fade->t1), std::max(0, fade->t2), std::max(0, fade->t3), std::max(0, fade->t4));
	return style_edit::SetForLine(text, {"\\fad", "\\fade"}, tag);
}

std::string WriteOrigin(std::string const& text, std::optional<Origin> const& origin) {
	return style_edit::SetForLine(text, {"\\org"}, origin ? "\\org(" + Point(origin->x, origin->y) + ")" : std::string());
}

std::string WriteTransforms(std::string const& text, std::vector<Transform> const& transforms, int duration) {
	std::string added;
	for (auto const& t : transforms) added += TransformTag(t, duration);
	auto blocks = TagBlocks(text);
	if (blocks.empty() || blocks.front().first != 0)
		return added.empty() ? text : "{" + added + "}" + text;

	size_t end = blocks.front().second;
	std::string_view content(text.data() + 1, end - 2);
	std::string kept;
	// Anything before the first tag (a stray word) stays where it is
	size_t first = content.find('\\');
	kept += content.substr(0, first == std::string_view::npos ? content.size() : first);
	for (auto piece : Pieces(content))
		if (NameOf(piece) != "\\t") kept += piece;
	kept += added;
	if (kept.empty()) return text.substr(end);
	return "{" + kept + "}" + text.substr(end);
}
}
