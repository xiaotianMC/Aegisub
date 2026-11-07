// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Changes made in the style panel (docs/style-panel.md): one property of the
// selected text, or of the selected lines as a whole, written as override
// tags.
#pragma once

#include "ass_dialogue.h"

#include <functional>
#include <string>
#include <vector>

#include <wx/string.h>

namespace agi { struct Context; }
class AssStyle;
struct StyleLook;

namespace style_edit {
/// What the panel can change. Each is set by its own override tags.
enum class Property {
	Font, Size, Weight, Italic, Underline, Strikeout, Spacing, Alignment,
	FillColour, FillAlpha, OutlineColour, OutlineAlpha, Border,
	ShadowColour, ShadowAlpha, Shadow, Blur,
};

/// The tags that set @p property, e.g. \bord, \xbord and \ybord
std::vector<std::string> const& TagNames(Property property);
/// Whether @p property is one for the whole line (alignment)
bool LineWide(Property property);
/// The tags that give @p look's value of @p property, e.g. "\fs48"
std::string TagFor(Property property, StyleLook const& look);

/// Remove the tags @p names everywhere in @p text, outside \t, then put
/// @p tag at the start of the line unless it is empty
std::string SetForLine(std::string const& text, std::vector<std::string> const& names, std::string const& tag);

struct RangeResult {
	std::string text;
	int start, end; ///< The same text selected in the result
};
/// Set @p tag for the text between the byte offsets @p start and @p end,
/// removing @p names inside it; @p restore goes after it, to keep the rest
/// of the line as it was (empty: nothing follows, or nothing to restore)
RangeResult SetForRange(std::string const& text, int start, int end,
	std::vector<std::string> const& names, std::string const& tag, std::string const& restore);

/// Change @p property of the selection in @p c: the selected text when
/// there is some in one line, otherwise every selected line from its start.
/// @p change is given the look before and makes the new one; a line whose
/// result is its style's value keeps no tag for it. @p amend_id is the
/// commit to amend (-1: a new undo step).
/// @return The commit's id, or -1 when nothing was written
int Apply(agi::Context *c, Property property, std::function<void(StyleLook&)> const& change, int amend_id = -1);

/// As Apply, with @p change also given how the line's style looks
int ApplyWithStyle(agi::Context *c, Property property,
	std::function<void(StyleLook&, StyleLook const&)> const& change, int amend_id = -1);

/// The rows Apply would write, without writing them: for a preview
/// written through an interaction (the colour picker)
struct Staged {
	std::vector<AssDialogueBase> rows; ///< The selected rows, changed
	std::vector<int> ids;              ///< Their ids
	bool changed = false;              ///< Whether any row changed
	wxString name;                     ///< For the undo step
	int single_line_id = 0;            ///< For the commit
	bool select = false;               ///< Whether the editor's selection moves
	int start = 0, end = 0;            ///< To there
};
Staged Stage(agi::Context const* c, Property property, std::function<void(StyleLook&, StyleLook const&)> const& change);
/// Move the editor's selection as @p staged says, after writing it
void Select(agi::Context *c, Staged const& staged);

/// Set the border style of the selected lines' styles: 3 draws an opaque
/// box behind the text, 1 an outline. @return As Apply
int SetBorderStyle(agi::Context *c, int border_style, int amend_id = -1);
}
