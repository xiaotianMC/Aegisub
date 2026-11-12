// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// What the override tags of a line do, for the subtitle grid's text column
// when it simplifies tags (docs/subtitle-grid-tags.md): each {…} becomes a
// chip of icons, one for each kind of tag in it, with what they do said in
// its tooltip. Kept apart from the drawing so it can be tested.
#pragma once

#include <libaegisub/color.h>

#include <string>
#include <string_view>
#include <vector>

#include <wx/string.h>

namespace tag_summary {

/// What a tag does, as one icon shows it
enum class Kind {
	Pos, Move, Fade, Transform, Alpha, FontName, FontSize, Bold, Italic,
	Underline, Strikeout, Border, Shadow, Blur, Rotate, Rotate3D, Scale,
	Shear, Clip, VectorClip, Origin, Spacing, Reset, Karaoke, Drawing,
	Colour, Align, Note, Unknown
};

/// One icon of a chip: a kind of tag, with what tells it apart
struct Mark {
	Kind kind = Kind::Unknown;
	/// \b0, \i0, \u0, \s0: the style switched off, drawn dimmed
	bool off = false;
	/// Colour: which, 1 to 4 (\c is 1). Align: the \an number, 1 to 9
	int index = 0;
	/// Colour: the one the tag sets
	agi::Color colour;
	/// The tags of this kind as written, for the tooltip ("\fad(200,200)")
	std::vector<std::string> tags;
};

/// A run of the line's text, or one {…} block
struct Piece {
	/// The text of a run: what the line shows
	std::string text;
	/// A run of a drawing's commands (after \p1): their points, shown as a
	/// count rather than as text. -1 for text and blocks
	int drawing_points = -1;
	/// A {…} block: what it holds, in the order first written
	bool block = false;
	std::vector<Mark> marks;
	/// The block holds only karaoke timing (\k, \kf, \ko, \K): a thin bar
	/// between syllables rather than a chip
	bool karaoke = false;
	/// The block as written, braces and all
	std::string raw;
};

/// The pieces of a line's text. A block holding only \p0, ending a drawing,
/// is left out: the drawing's chip says it
std::vector<Piece> Summarize(std::string_view text);

/// The icon (libresrc_modern_icon) for @p kind; null for Colour and
/// Align, which are drawn from their values
const char *IconName(Kind kind);
/// The kind's name, as the tooltip says it ("Fade")
wxString KindName(Kind kind);
/// The tooltip for a block: a line for each mark, saying what it sets,
/// and the block as written last
wxString Describe(Piece const& block);
/// The text a piece stands for when it can't be drawn: a run's text, and
/// a block's kinds in brackets ("[Fade·Position]")
wxString PlainText(Piece const& piece);

}
