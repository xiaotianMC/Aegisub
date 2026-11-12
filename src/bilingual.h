// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <string>
#include <string_view>
#include <vector>

class AssDialogue;
class AssDialogueBase;
class AssFile;

/// A bilingual timing: each line said in one style, its translation in
/// another at the same times, so the original stays beside what it becomes
namespace bilingual {
/// The styles of the said lines and of their translations. Kept here only,
/// till the names to give them are settled.
inline constexpr std::string_view source_style = "JP";
inline constexpr std::string_view target_style = "CN";

bool IsSource(AssDialogueBase const& line);
bool IsTarget(AssDialogueBase const& line);
/// The translation line of @p source: the first line of the target style
/// at the same start and end, comments aside. Nothing when there is none.
AssDialogue const *Partner(AssFile const& file, AssDialogueBase const& source);

/// What auto-timing notes in a line's Effect field, words apart by spaces:
/// "#12" for a line split from others of group 12, to be translated with
/// them, and "?word" for each word heard but not surely
struct Marks {
	int group = 0;
	std::vector<std::string> unsure;
	/// What else the field holds, left as it was
	std::string rest;
};
Marks ParseMarks(std::string_view effect);
/// The marks written into an Effect field, after what else it held. A word
/// loses its spaces, and its commas become full-width ones: the field
/// can't hold a comma.
std::string FormatMarks(Marks const& marks);
/// @p effect without auto-timing's marks: as it was when it has none
std::string ClearMarks(std::string_view effect);
}
