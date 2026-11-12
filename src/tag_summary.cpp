// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "tag_summary.h"

#include "compat.h"
#include "format.h"

#include <algorithm>
#include <iterator>
#include <cctype>
#include <cstdlib>
#include <utility>

#include <wx/intl.h>

namespace tag_summary {
namespace {

struct Known { std::string_view name; Kind kind; };
// The tags by name, longest first where one starts another (\fscx before
// \fs, \bord before \b), as the renderer reads them
constexpr Known known[] = {
	{"xbord", Kind::Border}, {"ybord", Kind::Border}, {"xshad", Kind::Shadow}, {"yshad", Kind::Shadow},
	{"iclip", Kind::Clip}, {"alpha", Kind::Alpha},
	{"fscx", Kind::Scale}, {"fscy", Kind::Scale}, {"fade", Kind::Fade}, {"move", Kind::Move},
	{"clip", Kind::Clip}, {"bord", Kind::Border}, {"shad", Kind::Shadow}, {"blur", Kind::Blur},
	{"fad", Kind::Fade}, {"fax", Kind::Shear}, {"fay", Kind::Shear}, {"frx", Kind::Rotate3D},
	{"fry", Kind::Rotate3D}, {"frz", Kind::Rotate}, {"fsp", Kind::Spacing}, {"pos", Kind::Pos},
	{"org", Kind::Origin}, {"pbo", Kind::Drawing},
	{"1c", Kind::Colour}, {"2c", Kind::Colour}, {"3c", Kind::Colour}, {"4c", Kind::Colour},
	{"1a", Kind::Alpha}, {"2a", Kind::Alpha}, {"3a", Kind::Alpha}, {"4a", Kind::Alpha},
	{"an", Kind::Align}, {"fs", Kind::FontSize}, {"fn", Kind::FontName}, {"fr", Kind::Rotate},
	{"be", Kind::Blur}, {"kf", Kind::Karaoke}, {"ko", Kind::Karaoke},
	{"a", Kind::Align}, {"b", Kind::Bold}, {"c", Kind::Colour}, {"i", Kind::Italic},
	{"k", Kind::Karaoke}, {"K", Kind::Karaoke}, {"p", Kind::Drawing}, {"q", Kind::Unknown},
	{"r", Kind::Reset}, {"s", Kind::Strikeout}, {"t", Kind::Transform}, {"u", Kind::Underline},
	{"", Kind::Unknown}, // Any other: its letters
};

std::string_view Trim(std::string_view s) {
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
	return s;
}

/// The numbers in @p s, as written
std::vector<std::string_view> Numbers(std::string_view s) {
	std::vector<std::string_view> found;
	size_t i = 0;
	while (i < s.size()) {
		char ch = s[i];
		bool sign = (ch == '-' || ch == '+') && i + 1 < s.size() && (std::isdigit(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == '.');
		if (std::isdigit(static_cast<unsigned char>(ch)) || sign || (ch == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
			size_t start = i++;
			while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
			found.push_back(s.substr(start, i - start));
		}
		else ++i;
	}
	return found;
}

/// An ASS colour, &HBBGGRR&, or as many hex digits as are there
agi::Color ParseColour(std::string_view arg) {
	unsigned long value = 0;
	size_t i = arg.find_first_of("Hh");
	i = i == std::string_view::npos ? 0 : i + 1;
	for (int digits = 0; i < arg.size() && digits < 6 && std::isxdigit(static_cast<unsigned char>(arg[i])); ++i, ++digits) {
		char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(arg[i])));
		value = value * 16 + static_cast<unsigned long>(ch <= '9' ? ch - '0' : ch - 'a' + 10);
	}
	return agi::Color(value & 0xff, (value >> 8) & 0xff, (value >> 16) & 0xff);
}

/// \a's numbers (SSA's: 1-3 bottom, 5-7 top, 9-11 middle) as \an's
int LegacyAlign(int a) {
	int column = (a - 1) % 4 + 1;
	if (column > 3 || a < 1 || a > 11) return 2;
	if (a >= 9) return column + 3;
	if (a >= 5) return column + 6;
	return column;
}

struct Tag { std::string_view name, arg, written; Kind kind; };

/// The tags of a block's text (without its braces)
std::vector<Tag> Tags(std::string_view body) {
	std::vector<Tag> tags;
	size_t i = body.find('\\');
	while (i != std::string_view::npos && i < body.size()) {
		size_t start = i++;
		auto it = std::find_if(std::begin(known), std::end(known), [&](Known const& k) {
			return body.substr(i, k.name.size()) == k.name;
		});
		std::string_view name = it->name;
		if (name.empty()) {
			// Not one the renderer knows: its letters
			size_t end = i;
			while (end < body.size() && std::isalpha(static_cast<unsigned char>(body[end]))) ++end;
			name = body.substr(i, end - i);
		}
		i += name.size();
		size_t arg_start = i;
		while (i < body.size() && body[i] == ' ') ++i;
		if (i < body.size() && body[i] == '(') {
			// To the matching bracket: \t's tags are its argument
			int depth = 0;
			for (; i < body.size(); ++i) {
				if (body[i] == '(') ++depth;
				else if (body[i] == ')' && --depth == 0) { ++i; break; }
			}
		}
		else {
			while (i < body.size() && body[i] != '\\') ++i;
		}
		tags.push_back({name, Trim(body.substr(arg_start, i - arg_start)), Trim(body.substr(start, i - start)), it->kind});
		i = body.find('\\', i);
	}
	return tags;
}

/// The marks for a block's tags, one a kind (one a colour), in the order
/// first written; @p drawing is set to whether a drawing follows
std::vector<Mark> Marks(std::vector<Tag> const& tags, bool &drawing) {
	std::vector<Mark> marks;
	for (auto const& tag : tags) {
		Kind kind = tag.kind;
		int index = 0;
		if (kind == Kind::Colour) index = tag.name.size() == 2 ? tag.name[0] - '0' : 1;
		if (kind == Kind::Clip) {
			// A rectangle has four numbers; a shape is drawn
			auto inside = tag.arg;
			if (!inside.empty() && inside.front() == '(') inside.remove_prefix(1);
			bool rectangle = Numbers(inside).size() == 4 && inside.find_first_of("mlbsnpc") == std::string_view::npos;
			if (!rectangle) kind = Kind::VectorClip;
		}
		if (tag.name == "p") drawing = !Numbers(tag.arg).empty() && std::atof(std::string(Numbers(tag.arg)[0]).c_str()) > 0;
		auto it = std::find_if(marks.begin(), marks.end(), [&](Mark const& m) { return m.kind == kind && m.index == index; });
		if (kind == Kind::Align) {
			it = std::find_if(marks.begin(), marks.end(), [](Mark const& m) { return m.kind == Kind::Align; });
		}
		if (it == marks.end()) {
			marks.emplace_back();
			marks.back().kind = kind;
			it = marks.end() - 1;
			it->index = index;
		}
		it->tags.emplace_back(tag.written);
		switch (kind) {
			case Kind::Colour: it->colour = ParseColour(tag.arg); break;
			case Kind::Align: {
				auto numbers = Numbers(tag.arg);
				int n = numbers.empty() ? 2 : std::atoi(std::string(numbers[0]).c_str());
				it->index = tag.name == "a" ? LegacyAlign(n) : std::clamp(n, 1, 9);
				break;
			}
			case Kind::Bold: case Kind::Italic: case Kind::Underline: case Kind::Strikeout:
				it->off = tag.arg == "0";
				break;
			default: break;
		}
	}
	return marks;
}

int DrawingPoints(std::string_view commands) {
	return static_cast<int>(Numbers(commands).size() / 2);
}

wxString ColourName(int index) {
	switch (index) {
		case 2: return _("Secondary colour");
		case 3: return _("Border colour");
		case 4: return _("Shadow colour");
		default: return _("Primary colour");
	}
}

/// "(640,80)" as "(640, 80)"
wxString Spaced(std::string_view arg) {
	std::string out;
	for (char ch : arg) {
		out += ch;
		if (ch == ',') out += ' ';
	}
	return to_wx(out);
}

/// The part of a tag after its name
std::string_view Argument(std::string_view written) {
	size_t i = 1;
	auto it = std::find_if(std::begin(known), std::end(known), [&](Known const& k) {
		return !k.name.empty() && written.substr(1, k.name.size()) == k.name;
	});
	if (it != std::end(known)) i += it->name.size();
	else while (i < written.size() && std::isalpha(static_cast<unsigned char>(written[i]))) ++i;
	return Trim(written.substr(i));
}

wxString Detail(Mark const& mark) {
	if (mark.tags.empty()) return {};
	std::string_view first = mark.tags.front();
	std::string_view arg = Argument(first);
	switch (mark.kind) {
		case Kind::Colour: {
			auto c = mark.colour;
			return fmt_wx("#%02X%02X%02X", (int)c.r, (int)c.g, (int)c.b);
		}
		case Kind::Align: return fmt_wx("%d", mark.index);
		case Kind::Bold: case Kind::Italic: case Kind::Underline: case Kind::Strikeout:
			if (mark.kind == Kind::Bold && arg.size() > 1) return to_wx(std::string(arg));
			return mark.off ? _("off") : _("on");
		case Kind::Fade: {
			auto numbers = Numbers(arg);
			if (first.substr(0, 4) == "\\fad" && first.substr(0, 5) != "\\fade" && numbers.size() == 2)
				return fmt_tl("in %s ms, out %s ms", to_wx(std::string(numbers[0])), to_wx(std::string(numbers[1])));
			break;
		}
		case Kind::Note: return to_wx(std::string(first));
		default: break;
	}
	if (mark.tags.size() == 1 && !arg.empty() && arg.front() == '(' && arg.size() < 80) return Spaced(arg);
	if (mark.tags.size() == 1 && !arg.empty() && arg.size() < 80 && mark.kind != Kind::Unknown && mark.kind != Kind::Transform) return to_wx(std::string(arg));
	wxString joined;
	for (auto const& tag : mark.tags) {
		if (!joined.empty()) joined += " ";
		joined += to_wx(tag.size() > 80 ? tag.substr(0, 80) + "…" : tag);
	}
	return joined;
}

wxString MarkName(Mark const& mark) {
	if (mark.kind == Kind::Colour) return ColourName(mark.index);
	return KindName(mark.kind);
}

}

std::vector<Piece> Summarize(std::string_view text) {
	std::vector<Piece> pieces;
	bool drawing = false;
	auto add_run = [&](std::string_view run) {
		if (run.empty()) return;
		Piece piece;
		if (drawing) piece.drawing_points = DrawingPoints(run);
		else piece.text = std::string(run);
		pieces.push_back(std::move(piece));
	};
	size_t start = 0;
	while (start < text.size()) {
		size_t open = text.find('{', start);
		if (open == std::string_view::npos) break;
		size_t close = text.find('}', open);
		if (close == std::string_view::npos) break;
		add_run(text.substr(start, open - start));
		std::string_view body = text.substr(open + 1, close - open - 1);
		start = close + 1;

		Piece block;
		block.block = true;
		block.raw = std::string(text.substr(open, close - open + 1));
		if (body.find('\\') == std::string_view::npos) {
			// No tags: a note for whoever reads the script
			Mark note;
			note.kind = Kind::Note;
			note.tags.emplace_back(Trim(body));
			block.marks.push_back(std::move(note));
			pieces.push_back(std::move(block));
			continue;
		}
		auto tags = Tags(body);
		bool was_drawing = drawing;
		block.marks = Marks(tags, drawing);
		// Only the end of a drawing: nothing to show
		if (was_drawing && !drawing && tags.size() == 1 && tags[0].name == "p") continue;
		block.karaoke = !block.marks.empty() && std::all_of(block.marks.begin(), block.marks.end(),
			[](Mark const& m) { return m.kind == Kind::Karaoke; });
		pieces.push_back(std::move(block));
	}
	add_run(text.substr(std::min(start, text.size())));
	return pieces;
}

const char *IconName(Kind kind) {
	switch (kind) {
		case Kind::Pos: return "visual_move_conv_pos";
		case Kind::Move: return "visual_move";
		case Kind::Fade: return "tag_fade";
		case Kind::Transform: return "tag_transform";
		case Kind::Alpha: return "tag_alpha";
		case Kind::FontName: return "button_fontname";
		case Kind::FontSize: return "tag_fontsize";
		case Kind::Bold: return "button_bold";
		case Kind::Italic: return "button_italics";
		case Kind::Underline: return "button_underline";
		case Kind::Strikeout: return "button_strikeout";
		case Kind::Border: return "tag_border";
		case Kind::Shadow: return "tag_shadow";
		case Kind::Blur: return "tag_blur";
		case Kind::Rotate: return "visual_rotatez";
		case Kind::Rotate3D: return "visual_rotatexy";
		case Kind::Scale: return "visual_scale";
		case Kind::Shear: return "tag_shear";
		case Kind::Clip: return "visual_clip";
		case Kind::VectorClip: return "visual_vector_clip";
		case Kind::Origin: return "tag_origin";
		case Kind::Spacing: return "tag_spacing";
		case Kind::Reset: return "tag_reset";
		case Kind::Karaoke: return "kara_mode";
		case Kind::Drawing: return "shape_polygon";
		case Kind::Note: return "tag_comment";
		case Kind::Unknown: return "tag_unknown";
		case Kind::Colour: case Kind::Align: return nullptr;
	}
	return nullptr;
}

wxString KindName(Kind kind) {
	switch (kind) {
		case Kind::Pos: return _("Position");
		case Kind::Move: return _("Movement");
		case Kind::Fade: return _("Fade");
		case Kind::Transform: return _("Animation");
		case Kind::Alpha: return _("Transparency");
		case Kind::FontName: return _("Font");
		case Kind::FontSize: return _("Font size");
		case Kind::Bold: return _("Bold");
		case Kind::Italic: return _("Italic");
		case Kind::Underline: return _("Underline");
		case Kind::Strikeout: return _("Strikeout");
		case Kind::Border: return _("Border");
		case Kind::Shadow: return _("Shadow");
		case Kind::Blur: return _("Blur");
		case Kind::Rotate: return _("Rotation");
		case Kind::Rotate3D: return _("3D rotation");
		case Kind::Scale: return _("Scale");
		case Kind::Shear: return _("Shear");
		case Kind::Clip: return _("Clip");
		case Kind::VectorClip: return _("Vector clip");
		case Kind::Origin: return _("Origin");
		case Kind::Spacing: return _("Letter spacing");
		case Kind::Reset: return _("Reset to style");
		case Kind::Karaoke: return _("Karaoke");
		case Kind::Drawing: return _("Drawing");
		case Kind::Colour: return _("Colour");
		case Kind::Align: return _("Alignment");
		case Kind::Note: return _("Note");
		case Kind::Unknown: return _("Other tag");
	}
	return {};
}

wxString Describe(Piece const& block) {
	wxString out;
	for (auto const& mark : block.marks) {
		if (!out.empty()) out += "\n";
		wxString detail = Detail(mark);
		out += detail.empty() ? MarkName(mark) : fmt_tl("%s: %s", MarkName(mark), detail);
	}
	// A note is the whole block
	if (block.marks.size() == 1 && block.marks[0].kind == Kind::Note) return out;
	std::string raw = block.raw.size() > 300 ? block.raw.substr(0, 300) + "…" : block.raw;
	if (!out.empty()) out += "\n";
	return out + to_wx(raw);
}

wxString PlainText(Piece const& piece) {
	if (piece.drawing_points >= 0)
		return fmt_plural(piece.drawing_points, "(%d point)", "(%d points)", piece.drawing_points);
	if (!piece.block) return to_wx(piece.text);
	if (piece.karaoke) return "|";
	wxString names;
	for (auto const& mark : piece.marks) {
		if (!names.empty()) names += wxS("·");
		names += MarkName(mark);
	}
	return "[" + names + "]";
}

}
