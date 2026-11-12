// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The command palette (docs/command-palette.md): every command, searchable by
// name, by pinyin of a Chinese name, by English name, by menu, by shortcut and
// by command id. It runs the chosen command after closing.
#pragma once

#include <wx/dialog.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class wxStaticText;
class wxTextCtrl;
namespace agi { struct Context; }

namespace palette {
/// What a search key was made from: matches in the name, the menu path and
/// the English name are drawn, matches in the rest (the command id) are not.
enum class Field { Name, Path, Original, Hidden };

/// A text to search in, lower case, with where its words start and which
/// character of its field each of its characters stands for.
struct SearchKey {
    std::wstring text;
    std::vector<bool> starts;
    /// Index into the field's text for each character of @c text; empty when
    /// the key is not drawn (a command id).
    std::vector<size_t> shown;
    int weight = 0; ///< Added to the score of a match in this key
    Field field = Field::Name;
};

struct Match {
    int score = 0;
    /// Matched characters of the shown name, ascending.
    std::vector<size_t> positions;
    /// Matched characters of the menu path and of the English name.
    std::vector<size_t> path_positions, original_positions;
    /// The shortcut typed, as shown, when the match is by shortcut.
    std::wstring hotkey;

    /// Where matches in @p field are kept; null for fields not drawn.
    std::vector<size_t> *Marks(Field field);
};

/// Keys for the shown name @p name and the command id @p id: the name, its
/// pinyin and pinyin initials when it has Chinese characters, and the id.
/// Also, when given, the English @p original name (when it differs from the
/// name) and the menu @p path, with its pinyin, both weighted below the name.
std::vector<SearchKey> MakeKeys(std::wstring_view name, std::string_view id,
    std::wstring_view original = {}, std::wstring_view path = {});

/// Normalise what the user typed: lower case, without spaces.
std::wstring NormaliseQuery(std::wstring_view query);

/// The words of what the user typed, lower case.
std::vector<std::wstring> Tokenise(std::wstring_view query);

/// Best fuzzy match of the normalised @p query in @p key: every character of
/// the query in order, preferring word starts and runs. None if it does not match.
std::optional<Match> FuzzyMatch(std::wstring_view query, SearchKey const& key);

/// Best match over all @p keys.
std::optional<Match> BestMatch(std::wstring_view query, std::vector<SearchKey> const& keys);

/// Match what the user typed, @p query as typed: the whole of it without
/// spaces, or, when it has several words, each word on its own in any order
/// and any key ("tags hide", "timing shift"), whichever scores more.
std::optional<Match> MatchQuery(std::wstring_view query, std::vector<SearchKey> const& keys);

/// A shortcut in one form: lower case, modifiers in a fixed order, parts
/// joined by '-' ("Shift+Ctrl+P", "ctrl shift p" and "Ctrl-Shift-P" all give
/// "ctrl-shift-p"). With @p typed, empty unless the text looks like a
/// shortcut: modifiers and one key, or a function key alone; modifiers
/// followed by '-' or '+' give a prefix ending in '-' ("ctrl-").
std::wstring CanonicalHotkey(std::wstring_view text, bool typed);

/// The pinyin of one Chinese character, lower case without tones; empty for
/// other characters or when the system's ICU lacks the transliterator.
std::wstring Pinyin(wchar_t ch);
}

class CommandPalette final : public wxDialog {
public:
    struct Item {
        std::string name;
        wxString display, help, hotkey;
        wxString original; ///< English name, empty when the same as display
        wxString path;     ///< Where it is in the menu bar, e.g. "Timing"
        /// Every shortcut, as shown and in CanonicalHotkey form.
        std::vector<std::pair<wxString, std::wstring>> hotkeys;
        /// State now, read when the row is first shown (as menus do).
        bool resolved = false;
        bool enabled = true;
        bool checkable = false, checked = false;
        std::vector<palette::SearchKey> keys;
    };

private:
    agi::Context *c;
    wxTextCtrl *input;
    class PaletteList *list;
    wxStaticText *empty;
    std::vector<Item> items;
    std::vector<std::string> recent;
    /// Indices into @c items in the order shown, and their matches.
    std::vector<size_t> shown;
    std::vector<palette::Match> matches;
    std::string chosen;
    bool composing = false;
    bool was_active = false;

    void Filter();
    Item& Resolve(Item& item);
    void Move(int by);
    void OnKey(wxKeyEvent& event);
    friend class PaletteList;

public:
    explicit CommandPalette(agi::Context *c);

    /// Replace the typed text and filter for it.
    void SetQuery(wxString const& query);
    /// Commands shown, in order.
    std::vector<std::string> Shown() const;
    /// The command shown at @p index, its state read.
    Item const& ShownItem(size_t index);
    palette::Match const& ShownMatch(size_t index) const { return matches[index]; }
    int Selection() const;
    void Select(int index);
    /// Choose the selected command and close. False when nothing is selected
    /// or the command is not available now: the palette stays open.
    bool Accept();
    /// The chosen command, empty when closed without one.
    std::string const& Chosen() const { return chosen; }

    /// Put @p name first among the recently used commands.
    static void Remember(std::string const& name);
};

/// Show the palette over the main window. The chosen command, if any.
std::string ShowCommandPalette(agi::Context *c);
