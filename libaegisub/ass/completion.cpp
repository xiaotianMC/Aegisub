// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/ass/completion.h>
#include <libaegisub/ass/dialogue_parser.h>
#include <libaegisub/calltip_provider.h>

#include <algorithm>
#include <cctype>
#include <optional>

namespace agi::ass {
namespace {
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool starts(std::string_view text, std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; }

std::string_view tag_at(std::string_view text) {
	std::string_view name;
	for (auto const& tag : GetAssTagCatalog())
		if (tag.name.size() > name.size() && starts(text, tag.name)) name = tag.name;
	return name;
}

std::vector<std::string_view> parents(std::string_view text, size_t start, size_t end) {
	std::vector<std::string_view> result;
	std::string_view owner;
	for (size_t i = start; i < end; ++i) {
		if (text[i] == '\\') owner = tag_at(text.substr(i + 1));
		else if (text[i] == '(') { result.push_back(owner); owner = {}; }
		else if (text[i] == ')' && !result.empty()) result.pop_back();
	}
	return result;
}

bool in_expression(std::string_view text, size_t pos, bool template_line) {
	if (!template_line) return false;
	return std::count(text.begin(), text.begin() + pos, '!') % 2;
}

struct DrawingCommand {
	char name;
	const char *signature;
	const char *description;
	size_t coordinates;
};
const DrawingCommand commands[] = {
	{'m', "m x y", "Move (close previous shape)", 2},
	{'n', "n x y", "Move (leave previous shape open)", 2},
	{'l', "l x y", "Straight line", 2},
	{'b', "b x1 y1 x2 y2 x3 y3", "Cubic Bezier curve", 6},
	{'s', "s x1 y1 x2 y2 x3 y3 xN yN ...", "B-spline (at least 3 points)", 8},
	{'p', "p x y", "Extend B-spline", 2},
	{'c', "c", "Close B-spline", 0}
};

std::optional<std::pair<size_t, size_t>> drawing_body(std::string_view text, size_t pos) {
	auto before = text.substr(0, pos);
	auto open = before.rfind('{'), close = before.rfind('}');
	bool in_block = open != text.npos && (close == text.npos || open > close);
	if (in_block) {
		auto slash = before.rfind('\\');
		if (slash == text.npos || slash < open) return {};
		auto tag = tag_at(text.substr(slash + 1));
		if (tag != "clip" && tag != "iclip") return {};
		auto enclosing = parents(text, open + 1, slash);
		if (std::find(enclosing.begin(), enclosing.end(), "t") != enclosing.end()) return {};
		size_t start = slash + 1 + tag.size();
		if (start >= text.size() || text[start] != '(' || pos <= start) return {};
		++start;
		auto end = text.find_first_of(")}\\", start);
		if (end == text.npos) end = text.size();
		if (pos > end) return {};
		auto comma = text.find(',', start);
		if (comma < pos) {
			if (text.substr(start, comma - start).find_first_not_of("0123456789 \t") != text.npos) return {};
			start = comma + 1;
			if (text.find(',', start) < pos) return {};
		}
		auto first = text.find_first_not_of(" \t", start);
		if (first < pos && text[first] != 'm') return {};
		return std::pair{start, end};
	}
	// Reuse the syntax highlighter's drawing-mode detection. The probe gives
	// an empty drawing at the caret a token without changing the document.
	std::string probe(text);
	probe.insert(pos, "m");
	auto tokens = TokenizeDialogueBody(probe);
	MarkDrawings(probe, tokens);
	size_t offset = 0;
	for (auto const& token : tokens) {
		if (token.type == DialogueTokenType::DRAWING_FULL && offset <= pos && pos < offset + token.length)
			return std::pair{offset, offset + token.length - 1};
		offset += token.length;
	}
	return {};
}

Completion drawing_completion(std::string_view text, size_t pos, std::pair<size_t, size_t> body) {
	Completion result;
	result.start = pos;
	while (result.start > body.first && !std::isspace(static_cast<unsigned char>(text[result.start - 1]))) --result.start;
	result.end = pos;
	while (result.end < body.second && letter(text[result.end])) ++result.end;
	auto prefix = text.substr(result.start, pos - result.start);
	if (prefix.size() > 1 || (!prefix.empty() && !letter(prefix[0]))) return result;
	bool has_move = false, spline = false;
	for (size_t i = body.first; i < result.start; ++i) {
		if (letter(text[i])) {
			has_move |= text[i] == 'm';
			spline = text[i] == 's' || text[i] == 'p';
		}
	}
	for (auto const& command : commands) {
		if ((!prefix.empty() && prefix[0] != command.name) || (!has_move && command.name != 'm')) continue;
		if ((command.name == 'p' || command.name == 'c') && !spline) continue;
		std::string value(1, command.name);
		size_t caret = 1;
		if (command.coordinates) {
			if (result.end == text.size() || !std::isspace(static_cast<unsigned char>(text[result.end]))) value += ' ';
			++caret;
		}
		result.items.push_back({command.signature, value, caret, command.description});
	}
	return result;
}

bool animatable(std::string_view tag) {
	constexpr std::string_view tags = "|fs|fsp|fscx|fscy|c|1c|2c|3c|4c|alpha|1a|2a|3a|4a|fr|frx|fry|frz|fax|fay|bord|xbord|ybord|shad|xshad|yshad|clip|iclip|be|blur|";
	return tags.find("|" + std::string(tag) + "|") != tags.npos;
}

std::string description(std::string_view name) {
	static const std::pair<std::string_view, const char*> descriptions[] = {
		{"pos", "Position"}, {"move", "Movement"}, {"org", "Rotation origin"},
		{"clip", "Clipping"}, {"iclip", "Inverse clipping"}, {"t", "Animated transform"},
		{"fn", "Font name"}, {"fs", "Font size"}, {"fs+", "Increase font size"}, {"fs-", "Decrease font size"},
		{"fscx", "Horizontal scale"}, {"fscy", "Vertical scale"}, {"fsp", "Character spacing"},
		{"an", "Alignment"}, {"a", "Legacy alignment"}, {"r", "Reset to style"},
		{"b", "Bold / font weight"}, {"i", "Italic"}, {"u", "Underline"}, {"s", "Strikeout"},
		{"bord", "Border width"}, {"xbord", "Horizontal border"}, {"ybord", "Vertical border"},
		{"shad", "Shadow depth"}, {"xshad", "Horizontal shadow"}, {"yshad", "Vertical shadow"},
		{"blur", "Gaussian blur"}, {"be", "Edge blur"},
		{"fr", "Z rotation"}, {"frz", "Z rotation"}, {"frx", "X rotation"}, {"fry", "Y rotation"},
		{"fax", "Horizontal shear"}, {"fay", "Vertical shear"},
		{"c", "Primary color (BBGGRR)"}, {"1c", "Primary color (BBGGRR)"},
		{"2c", "Secondary color (BBGGRR)"}, {"3c", "Outline color (BBGGRR)"}, {"4c", "Shadow color (BBGGRR)"},
		{"alpha", "Transparency (00 opaque, FF invisible)"},
		{"1a", "Primary transparency"}, {"2a", "Secondary transparency"},
		{"3a", "Outline transparency"}, {"4a", "Shadow transparency"},
		{"fad", "Fade in / out (ms)"}, {"fade", "Complex fade (ms)"},
		{"k", "Karaoke duration (centiseconds)"}, {"K", "Karaoke fill (centiseconds)"},
		{"kf", "Karaoke fill (centiseconds)"}, {"ko", "Karaoke outline (centiseconds)"},
		{"q", "Wrap style"}, {"p", "Drawing mode"}, {"pbo", "Drawing baseline offset"}, {"fe", "Font encoding"}
	};
	for (auto const& [tag, label] : descriptions)
		if (tag == name) return label;
	return {};
}
}

Completion GetCompletion(std::string_view text, size_t pos, bool template_line,
	std::vector<std::string> const& styles, std::vector<std::string> const& fonts) {
	Completion result;
	if (pos > text.size()) return result;
	// Template expressions have their own language; never treat their slashes as ASS.
	if (in_expression(text, pos, template_line)) return result;
	if (auto body = drawing_body(text, pos)) return drawing_completion(text, pos, *body);
	const auto before = text.substr(0, pos);
	auto open = before.rfind('{'), close = before.rfind('}');
	bool in_block = open != text.npos && (close == text.npos || open > close);
	auto slash = before.rfind('\\');
	if (slash == text.npos || (in_block && slash < open)) return result;
	result.start = slash;
	result.end = pos;
	auto prefix = text.substr(slash + 1, pos - slash - 1);
	if (!in_block) {
		if (prefix.size() > 1) return result;
		for (auto name : {"N", "n", "h"}) {
			if (!starts(name, prefix)) continue;
			std::string value = std::string("\\") + name;
			result.items.push_back({value, value, value.size()});
		}
		if (pos < text.size() && (text[pos] == 'N' || text[pos] == 'n' || text[pos] == 'h')) ++result.end;
		return result;
	}

	auto parentheses = parents(text, open + 1, slash);
	bool transform = std::find(parentheses.begin(), parentheses.end(), "t") != parentheses.end();
	// A slash inside numeric/vector parameters is not a nested ASS tag.
	if (!parentheses.empty() && parentheses.back() != "t") return result;
	auto current_tag = tag_at(text.substr(slash + 1));
	if (!current_tag.empty() && prefix.size() >= current_tag.size()
		&& (!transform || animatable(current_tag))) {
		Completion parameters;
		parameters.start = slash + 1 + current_tag.size();
		parameters.end = text.find_first_of("\\}", parameters.start);
		if (parameters.end == text.npos) parameters.end = text.size();
		bool string_parameter = current_tag == "fn" || current_tag == "r";
		if (!string_parameter) {
			auto delimiter = text.find_first_of("(),", parameters.start);
			if (delimiter != text.npos) parameters.end = std::min(parameters.end, delimiter);
		}
		if (pos >= parameters.start && pos <= parameters.end) {
			auto argument = text.substr(parameters.start, pos - parameters.start);
			parameters.font = current_tag == "fn";
			auto add = [&](std::string value, std::string label = {}) {
				if (value.find_first_of("\n\r\t\\{}") != value.npos) return;
				bool match = starts(value, argument);
				if (parameters.font && value.size() >= argument.size())
					match = std::equal(argument.begin(), argument.end(), value.begin(), [](unsigned char a, unsigned char b) {
						return std::tolower(a) == std::tolower(b);
					});
				if (match) parameters.items.push_back({value, value, value.size(), label});
			};
			if (current_tag == "r" || current_tag == "fn") {
				for (auto const& value : current_tag == "r" ? styles : fonts) add(value);
			}
			else if (current_tag == "an") {
				const char *alignments[] = {"Bottom left", "Bottom center", "Bottom right", "Middle left", "Middle center", "Middle right", "Top left", "Top center", "Top right"};
				for (int i = 1; i <= 9; ++i) add(std::to_string(i), alignments[i - 1]);
			}
			else if (current_tag == "i" || current_tag == "u" || current_tag == "s" || current_tag == "b") {
				add("0", "Off"); add("1", "On");
				if (current_tag == "b") { add("400", "Normal weight"); add("700", "Bold weight"); }
			}
			else if (current_tag == "q") {
				add("0", "Smart wrap, wider top line"); add("1", "End-of-line wrap");
				add("2", "No automatic wrapping"); add("3", "Smart wrap, wider bottom line");
			}
			else if (current_tag == "p") {
				add("0", "Text mode");
				for (int i = 1; i <= 4; ++i) add(std::to_string(i), "Drawing mode");
			}
			else if (current_tag == "c" || (current_tag.size() == 2 && current_tag[1] == 'c')) {
				add("&HFFFFFF&", "White"); add("&H000000&", "Black");
				add("&H0000FF&", "Red"); add("&H00FF00&", "Green"); add("&HFF0000&", "Blue");
			}
			else if (current_tag == "alpha" || (current_tag.size() == 2 && current_tag[1] == 'a')) {
				add("&H00&", "Opaque"); add("&H40&", "25% transparent"); add("&H80&", "50% transparent");
				add("&HC0&", "75% transparent"); add("&HFF&", "Invisible");
			}
			if (parameters.font || current_tag == "r") {
				std::sort(parameters.items.begin(), parameters.items.end(), [](auto const& a, auto const& b) { return a.text < b.text; });
				parameters.items.erase(std::unique(parameters.items.begin(), parameters.items.end(), [](auto const& a, auto const& b) { return a.text == b.text; }), parameters.items.end());
				return parameters;
			}
			if (!parameters.items.empty()) return parameters;
		}
	}

	// Find the entire tag name, including the suffix to the right of the caret,
	// but leave an existing parameter (including a font/style name) untouched.
	size_t name_end = slash + 1;
	if (name_end < text.size() && text[name_end] >= '1' && text[name_end] <= '4') ++name_end;
	while (name_end < text.size() && letter(text[name_end])) ++name_end;
	if (name_end < text.size() && (text[name_end] == '+' || text[name_end] == '-')) ++name_end;
	std::string_view full = text.substr(slash + 1, name_end - slash - 1);
	size_t known_length = 0;
	for (auto const& tag : GetAssTagCatalog())
		if (starts(full, tag.name)) known_length = std::max(known_length, tag.name.size());
	result.end = known_length ? slash + 1 + known_length : name_end;
	if (prefix.size() > known_length) result.end = name_end;

	std::string previous;
	for (auto const& tag : GetAssTagCatalog()) {
		if (tag.name == previous || !starts(tag.name, prefix)) continue;
		if (transform && !animatable(tag.name)) continue;
		if (transform && (tag.name == "clip" || tag.name == "iclip") && tag.signature.find("X1") == std::string::npos) continue;
		previous = tag.name;
		std::string value = "\\" + tag.name;
		bool parens = tag.signature.find('(') != std::string::npos;
		size_t caret = value.size();
		if (parens && (result.end == text.size() || text[result.end] != '(')) {
			value += "()";
			++caret;
		}
		else if (parens) ++caret;
		result.items.push_back({tag.signature, value, caret, description(tag.name)});
	}
	return result;
}

agi::Calltip GetDrawingCalltip(std::string_view text, size_t pos, bool template_line) {
	agi::Calltip none{nullptr, 0, 0, 0};
	if (pos > text.size() || in_expression(text, pos, template_line)) return none;
	auto body = drawing_body(text, pos);
	if (!body) return none;
	DrawingCommand const* current = nullptr;
	size_t command_pos = 0, coordinate = 0;
	for (size_t i = body->first; i < pos;) {
		if (std::isspace(static_cast<unsigned char>(text[i]))) { ++i; continue; }
		size_t start = i;
		while (i < pos && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
		if (i - start == 1 && letter(text[start])) {
			current = nullptr;
			for (auto const& command : commands)
				if (command.name == text[start]) current = &command;
			command_pos = start;
			coordinate = 0;
		}
		else if (i < pos) ++coordinate;
	}
	if (!current) return none;
	if (!current->coordinates) return {current->signature, 0, 0, command_pos};
	if (current->name == 's' && coordinate >= 6) coordinate = 6 + coordinate % 2;
	else coordinate %= current->coordinates;
	std::string_view signature = current->signature;
	size_t start = 2;
	for (size_t i = 0; i < coordinate; ++i) start = signature.find(' ', start) + 1;
	auto end = signature.find(' ', start);
	return {current->signature, start, end == signature.npos ? signature.size() : end, command_pos};
}
}
