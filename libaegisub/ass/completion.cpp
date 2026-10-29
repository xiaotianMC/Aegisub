// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/ass/completion.h>
#include <libaegisub/calltip_provider.h>

#include <algorithm>
#include <cctype>

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
	if (template_line) {
		bool expression = false;
		for (size_t i = 0; i < pos; ++i)
			if (text[i] == '!') expression = !expression;
		if (expression) return result;
	}
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

	std::vector<std::string_view> parentheses;
	std::string_view owner;
	for (size_t i = open + 1; i < slash; ++i) {
		if (text[i] == '\\') owner = tag_at(text.substr(i + 1));
		else if (text[i] == '(') { parentheses.push_back(owner); owner = {}; }
		else if (text[i] == ')' && !parentheses.empty()) parentheses.pop_back();
	}
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
}
