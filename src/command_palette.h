// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The command palette (docs/command-palette.md): every command, searchable by
// name, by pinyin of a Chinese name and by command id. It runs the chosen
// command after closing.
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
/// A text to search in, lower case, with where its words start and which
/// character of the shown name each of its characters stands for.
struct SearchKey {
    std::wstring text;
    std::vector<bool> starts;
    /// Index into the shown name for each character of @c text; empty when
    /// the key is not drawn (a command id).
    std::vector<size_t> shown;
    int weight = 0; ///< Added to the score of a match in this key
};

struct Match {
    int score = 0;
    /// Matched characters of the shown name, ascending.
    std::vector<size_t> positions;
};

/// Keys for the shown name @p name and the command id @p id: the name, its
/// pinyin and pinyin initials when it has Chinese characters, and the id.
std::vector<SearchKey> MakeKeys(std::wstring_view name, std::string_view id);

/// Normalise what the user typed: lower case, without spaces.
std::wstring NormaliseQuery(std::wstring_view query);

/// Best fuzzy match of the normalised @p query in @p key: every character of
/// the query in order, preferring word starts and runs. None if it does not match.
std::optional<Match> FuzzyMatch(std::wstring_view query, SearchKey const& key);

/// Best match over all @p keys.
std::optional<Match> BestMatch(std::wstring_view query, std::vector<SearchKey> const& keys);

/// The pinyin of one Chinese character, lower case without tones; empty for
/// other characters or when the system's ICU lacks the transliterator.
std::wstring Pinyin(wchar_t ch);
}

class CommandPalette final : public wxDialog {
public:
    struct Item {
        std::string name;
        wxString display, help, hotkey;
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
