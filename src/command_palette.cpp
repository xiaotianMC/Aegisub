// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "command_palette.h"

#include "command/command.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "options.h"
#include "theme.h"
#include "workspace_tabs.h"

#include <algorithm>
#include <climits>
#include <map>
#include <memory>

#include <unicode/translit.h>
#include <unicode/unistr.h>

#include <wx/control.h>
#include <wx/dcclient.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/vlbox.h>

#ifdef WITH_GTK3_APPEARANCE
#include <gtk/gtk.h>
#endif

namespace palette {
namespace {
bool IsHan(wchar_t ch) {
    return (ch >= 0x4e00 && ch <= 0x9fff) || (ch >= 0x3400 && ch <= 0x4dbf) || (ch >= 0xf900 && ch <= 0xfaff);
}

bool IsSeparator(wchar_t ch) {
    return !std::iswalnum(ch) && !IsHan(ch);
}

icu::Transliterator *HanLatin() {
    static std::unique_ptr<icu::Transliterator> transliterator = [] {
        UErrorCode status = U_ZERO_ERROR;
        std::unique_ptr<icu::Transliterator> made(icu::Transliterator::createInstance(
            "Han-Latin; Latin-ASCII; Lower", UTRANS_FORWARD, status));
        if (U_FAILURE(status)) made.reset();
        return made;
    }();
    return transliterator.get();
}
}

std::wstring Pinyin(wchar_t ch) {
    if (!IsHan(ch)) return {};
    static std::map<wchar_t, std::wstring> cache;
    if (auto it = cache.find(ch); it != cache.end()) return it->second;
    std::wstring result;
    if (auto transliterator = HanLatin()) {
        icu::UnicodeString text(static_cast<UChar32>(ch));
        transliterator->transliterate(text);
        std::string utf8;
        text.toUTF8String(utf8);
        for (char letter : utf8)
            if (letter >= 'a' && letter <= 'z') result += static_cast<wchar_t>(letter);
    }
    cache.emplace(ch, result);
    return result;
}

std::wstring NormaliseQuery(std::wstring_view query) {
    std::wstring result;
    for (wchar_t ch : query)
        if (!std::iswspace(ch)) result += static_cast<wchar_t>(std::towlower(ch));
    return result;
}

std::vector<SearchKey> MakeKeys(std::wstring_view name, std::string_view id) {
    std::vector<SearchKey> keys;

    // The name. Words start after separators, at capitals after lower case
    // letters, and at every Chinese character.
    SearchKey plain;
    bool has_han = false;
    for (size_t i = 0; i < name.size(); ++i) {
        wchar_t ch = name[i];
        bool start = i == 0 || IsSeparator(name[i - 1]) || IsHan(ch)
            || (std::iswupper(ch) && std::iswlower(name[i - 1]));
        plain.text += static_cast<wchar_t>(std::towlower(ch));
        plain.starts.push_back(start && !IsSeparator(ch));
        plain.shown.push_back(i);
        has_han = has_han || IsHan(ch);
    }
    keys.push_back(std::move(plain));

    // Pinyin, each syllable a word, and pinyin initials: "打开字幕" is found
    // by "dakaizimu" and by "dkzm".
    if (has_han) {
        SearchKey full, initials;
        full.weight = initials.weight = -1;
        for (size_t i = 0; i < name.size(); ++i) {
            auto syllable = Pinyin(name[i]);
            if (syllable.empty()) {
                bool start = i == 0 || IsSeparator(name[i - 1]) || IsHan(name[i - 1]);
                wchar_t lower = static_cast<wchar_t>(std::towlower(name[i]));
                for (auto key : {&full, &initials}) {
                    key->text += lower;
                    key->starts.push_back(start && !IsSeparator(name[i]));
                    key->shown.push_back(i);
                }
                continue;
            }
            for (size_t j = 0; j < syllable.size(); ++j) {
                full.text += syllable[j];
                full.starts.push_back(j == 0);
                full.shown.push_back(i);
            }
            initials.text += syllable[0];
            initials.starts.push_back(true);
            initials.shown.push_back(i);
        }
        keys.push_back(std::move(full));
        keys.push_back(std::move(initials));
    }

    // The command id, e.g. "subtitle/open/video": found but not drawn.
    SearchKey command;
    command.weight = -6;
    for (size_t i = 0; i < id.size(); ++i) {
        wchar_t ch = static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(id[i])));
        command.text += ch;
        command.starts.push_back(!IsSeparator(ch) && (i == 0 || IsSeparator(static_cast<wchar_t>(id[i - 1]))));
    }
    keys.push_back(std::move(command));
    return keys;
}

std::optional<Match> FuzzyMatch(std::wstring_view query, SearchKey const& key) {
    size_t m = query.size(), n = key.text.size();
    if (m == 0) return Match{};
    if (m > n) return std::nullopt;

    // score[i][j]: best score of the first i + 1 query characters with the
    // last one at j. A matched character is worth 16, 8 more at a word start
    // (12 at the very start), 8 more right after the previous match; each
    // skipped character between matches costs 1.
    constexpr int none = INT_MIN / 4;
    std::vector<int> score(m * n, none), from(m * n, -1);
    for (size_t i = 0; i < m; ++i) {
        int best_gap = none, best_k = -1; // max over k <= j - 2 of score[i-1][k] + k
        for (size_t j = i; j < n; ++j) {
            if (i > 0 && j >= 2) {
                int k = static_cast<int>(j) - 2;
                int prev = score[(i - 1) * n + k];
                if (prev > none && prev + k > best_gap) { best_gap = prev + k; best_k = k; }
            }
            if (key.text[j] != query[i]) continue;
            int bonus = 16 + (key.starts[j] ? (j == 0 ? 12 : 8) : 0);
            if (i == 0) { score[j] = bonus; continue; }
            int candidate = none, origin = -1;
            if (int prev = score[(i - 1) * n + j - 1]; prev > none) {
                candidate = prev + 8;
                origin = static_cast<int>(j) - 1;
            }
            if (best_gap > none && best_gap - static_cast<int>(j) + 1 > candidate) {
                candidate = best_gap - static_cast<int>(j) + 1;
                origin = best_k;
            }
            if (candidate == none) continue;
            score[i * n + j] = candidate + bonus;
            from[i * n + j] = origin;
        }
    }

    int best = none, end = -1;
    for (size_t j = m - 1; j < n; ++j)
        if (score[(m - 1) * n + j] > best) { best = score[(m - 1) * n + j]; end = static_cast<int>(j); }
    if (best == none) return std::nullopt;

    Match match;
    match.score = best + key.weight;
    if (!key.shown.empty()) {
        for (int i = static_cast<int>(m) - 1, j = end; i >= 0 && j >= 0; j = from[i * n + j], --i)
            match.positions.push_back(key.shown[j]);
        std::sort(match.positions.begin(), match.positions.end());
        match.positions.erase(std::unique(match.positions.begin(), match.positions.end()), match.positions.end());
    }
    return match;
}

std::optional<Match> BestMatch(std::wstring_view query, std::vector<SearchKey> const& keys) {
    std::optional<Match> best;
    for (auto const& key : keys)
        if (auto match = FuzzyMatch(query, key); match && (!best || match->score > best->score))
            best = std::move(match);
    return best;
}
}

namespace {
constexpr size_t max_recent = 12;
constexpr int visible_rows = 10;

std::wstring Wide(wxString const& text) {
    return text.ToStdWstring();
}

wxString HotkeyLabel(std::string const& name) {
    auto key = hotkey::get_hotkey_str_first("Default", name);
    for (auto context : {"Subtitle Edit Box", "Subtitle Grid", "Audio", "Video"}) {
        if (!key.empty()) break;
        key = hotkey::get_hotkey_str_first(context, name);
    }
    return to_wx(key);
}

bool Listed(std::string_view name) {
    return !name.starts_with("recent/") && name != "app/command_palette";
}
}

/// The rows: the name with the matched characters marked, a check mark for
/// a setting that is on, the shortcut on the right, the help below.
class PaletteList final : public wxVListBox {
    CommandPalette *palette;
    int Pad() const { return FromDIP(5); }
    int IconSize() const { return FromDIP(16); }

public:
    PaletteList(CommandPalette *palette)
    : wxVListBox(palette, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , palette(palette)
    {
        SetName("command-palette-list");
    }

    int RowHeight() const {
        int line = GetCharHeight();
        return Pad() * 2 + line * 2 + FromDIP(1);
    }

    wxCoord OnMeasureItem(size_t) const override { return RowHeight(); }

    void OnDrawBackground(wxDC& dc, wxRect const& rect, size_t n) const override {
        if (!IsSelected(n)) return;
        auto colours = theme::UiColours();
        dc.SetBrush(wxBrush(colours.list_selected));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(rect);
        dc.SetBrush(wxBrush(colours.accent));
        dc.DrawRectangle(rect.x, rect.y, FromDIP(3), rect.height);
    }

    void OnDrawItem(wxDC& dc, wxRect const& rect, size_t n) const override {
        auto const& item = palette->ShownItem(n);
        auto const& match = palette->ShownMatch(n);
        auto colours = theme::UiColours();
        auto text_colour = item.enabled ? colours.list_text : colours.list_dim;
        auto font = GetFont();
        auto bold = font.Bold();
        int pad = Pad(), x = rect.x + pad * 2, y = rect.y + pad;
        int line = GetCharHeight();

        // Icon, or room for one so the names line up.
        int icon = IconSize();
        if (auto bundle = cmd::get(item.name)->Icon(16); bundle.IsOk()) {
            auto bitmap = bundle.GetBitmapFor(this);
            if (bitmap.IsOk()) {
                if (!item.enabled) bitmap = bitmap.ConvertToDisabled();
                dc.DrawBitmap(bitmap, x, y + (line - icon) / 2, true);
            }
        }
        x += icon + pad * 2;

        // Shortcut on the right.
        int right = rect.GetRight() - pad * 2;
        dc.SetFont(font);
        if (!item.hotkey.empty()) {
            auto size = dc.GetTextExtent(item.hotkey);
            dc.SetTextForeground(colours.list_dim);
            dc.DrawText(item.hotkey, right - size.x, y);
            right -= size.x + pad * 3;
        }

        // Name, matched characters bold in the accent colour.
        wxString name = item.display;
        if (item.checkable && item.checked) name = wxString::FromUTF8("✓ ") + name;
        size_t offset = name.size() - item.display.size();
        size_t next = 0;
        for (size_t i = 0; i < name.size() && x < right;) {
            bool marked = i >= offset && next < match.positions.size() && match.positions[next] == i - offset;
            size_t j = i + 1;
            if (marked) ++next;
            else while (j < name.size() && !(j >= offset && next < match.positions.size() && match.positions[next] == j - offset)) ++j;
            auto run = name.Mid(i, j - i);
            dc.SetFont(marked ? bold : font);
            dc.SetTextForeground(marked && item.enabled ? colours.accent : text_colour);
            dc.DrawText(run, x, y);
            x += dc.GetTextExtent(run).x;
            i = j;
        }

        // Help below the name.
        wxString help = item.help;
        if (!item.enabled) help = _("Not available now") + wxString::FromUTF8(" · ") + help;
        int help_x = rect.x + pad * 4 + icon;
        dc.SetFont(font);
        dc.SetTextForeground(colours.list_dim);
        dc.DrawText(wxControl::Ellipsize(help, dc, wxELLIPSIZE_END, rect.GetRight() - pad * 2 - help_x), help_x, y + line + FromDIP(1));
    }
};

CommandPalette::CommandPalette(agi::Context *c)
: wxDialog(c->parent, -1, _("Command Palette"), wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE)
, c(c)
{
    SetName("command-palette");
    auto colours = theme::UiColours();
    SetBackgroundColour(colours.list_back);

    input = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    input->SetName("command-palette-input");
    input->SetHint(_("Type a command name, pinyin or command id"));
    list = new PaletteList(this);
    list->SetBackgroundColour(colours.list_back);
    empty = new wxStaticText(this, -1, _("No matching commands"));
    empty->SetName("command-palette-empty");
    empty->SetForegroundColour(colours.list_dim);
    auto hint = new wxStaticText(this, -1, wxString::FromUTF8("↑↓ ") + _("Select") + wxString::FromUTF8("   Enter ") + _("Run") + wxString::FromUTF8("   Esc ") + _("Close"));
    hint->SetForegroundColour(colours.list_dim);
    hint->SetFont(GetFont().Smaller());

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(input, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
    sizer->Add(list, wxSizerFlags(1).Expand());
    sizer->Add(empty, wxSizerFlags().Border(wxALL, FromDIP(12)));
    sizer->Add(hint, wxSizerFlags().Right().Border(wxALL, FromDIP(6)));
    SetSizer(sizer);

    // Every command, as it is now: names and availability change with the project.
    recent = OPT_GET("App/Command Palette/Recent")->GetListString();
    std::erase_if(recent, [](std::string const& name) { return name.empty(); });
    for (auto name_view : cmd::get_registered_commands()) {
        if (!Listed(name_view)) continue;
        std::string name(name_view);
        auto command = cmd::get(name);
        Item item;
        item.name = name;
        item.display = command->StrDisplay(c);
        if (item.display.empty()) continue;
        item.help = command->StrHelp();
        item.hotkey = HotkeyLabel(name);
        item.keys = palette::MakeKeys(Wide(item.display), name);
        items.push_back(std::move(item));
    }
    std::erase_if(recent, [&](std::string const& name) {
        return std::none_of(items.begin(), items.end(), [&](Item const& item) { return item.name == name; });
    });

    int width = FromDIP(620);
    if (auto top = wxGetTopLevelParent(c->parent)) width = std::min(width, std::max(FromDIP(320), top->GetClientSize().x - FromDIP(40)));
    list->SetMinSize(wxSize(width, list->RowHeight() * visible_rows));
    Filter();
    Fit();
    if (auto top = wxGetTopLevelParent(c->parent)) {
        auto area = top->GetScreenRect();
        auto client = top->ClientToScreen(wxPoint(0, 0));
        SetPosition(wxPoint(area.x + (area.width - GetSize().x) / 2, client.y + FromDIP(48)));
    }

    input->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Filter(); });
    input->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Accept(); });
    Bind(wxEVT_CHAR_HOOK, &CommandPalette::OnKey, this);
    list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { Accept(); });
    list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { input->SetFocus(); });
    // Close when another window is activated, like a menu.
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        event.Skip();
        if (event.GetActive()) was_active = true;
        else if (was_active && IsModal()) EndModal(wxID_CANCEL);
    });

#ifdef WITH_GTK3_APPEARANCE
    // While an input method composes (e.g. pinyin before it is committed),
    // Enter and the arrow keys belong to it.
    if (auto entry = static_cast<GtkWidget*>(input->GetHandle()); entry && GTK_IS_ENTRY(entry)) {
        g_signal_connect(entry, "preedit-changed", G_CALLBACK(+[](GtkEntry*, gchar *preedit, gpointer data) {
            static_cast<CommandPalette*>(data)->composing = preedit && *preedit;
        }), this);
    }
#endif
    input->SetFocus();
}

CommandPalette::Item& CommandPalette::Resolve(Item& item) {
    if (item.resolved) return item;
    auto command = cmd::get(item.name);
    item.enabled = (!c->workspaceTabs || c->workspaceTabs->Permits(item.name)) && command->Validate(c);
    item.checkable = command->Type() & (cmd::COMMAND_TOGGLE | cmd::COMMAND_RADIO);
    item.checked = item.checkable && command->IsActive(c);
    item.resolved = true;
    return item;
}

CommandPalette::Item const& CommandPalette::ShownItem(size_t index) {
    return Resolve(items[shown[index]]);
}

void CommandPalette::SetQuery(wxString const& query) {
    input->ChangeValue(query);
    Filter();
}

void CommandPalette::Filter() {
    auto query = palette::NormaliseQuery(Wide(input->GetValue()));
    shown.clear();
    matches.clear();
    auto recent_rank = [&](std::string const& name) {
        auto it = std::find(recent.begin(), recent.end(), name);
        return it == recent.end() ? -1 : static_cast<int>(it - recent.begin());
    };

    if (query.empty()) {
        // Recently used first, then all by name.
        for (auto const& name : recent)
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].name == name) shown.push_back(i);
        std::vector<size_t> rest;
        for (size_t i = 0; i < items.size(); ++i)
            if (recent_rank(items[i].name) < 0) rest.push_back(i);
        std::stable_sort(rest.begin(), rest.end(), [&](size_t a, size_t b) {
            return items[a].display.CmpNoCase(items[b].display) < 0;
        });
        shown.insert(shown.end(), rest.begin(), rest.end());
        matches.resize(shown.size());
    }
    else {
        std::vector<std::pair<size_t, palette::Match>> found;
        for (size_t i = 0; i < items.size(); ++i) {
            auto match = palette::BestMatch(query, items[i].keys);
            // A word of the help text, as a last resort.
            if (!match && query.size() >= 2 && Wide(items[i].help.Lower()).find(query) != std::wstring::npos)
                match = palette::Match{1, {}};
            if (!match) continue;
            if (int rank = recent_rank(items[i].name); rank >= 0) match->score += 6 + static_cast<int>(max_recent) - rank;
            found.emplace_back(i, std::move(*match));
        }
        std::stable_sort(found.begin(), found.end(), [&](auto const& a, auto const& b) {
            if (a.second.score != b.second.score) return a.second.score > b.second.score;
            auto const& x = items[a.first].display, &y = items[b.first].display;
            if (x.size() != y.size()) return x.size() < y.size();
            return x.CmpNoCase(y) < 0;
        });
        for (auto& [index, match] : found) {
            shown.push_back(index);
            matches.push_back(std::move(match));
        }
    }

    list->SetItemCount(shown.size());
    list->SetSelection(shown.empty() ? wxNOT_FOUND : 0);
    list->Refresh();
    GetSizer()->Show(list, !shown.empty());
    GetSizer()->Show(empty, shown.empty());
    if (shown.empty()) empty->SetMinSize(list->GetMinSize() - wxSize(FromDIP(24), FromDIP(24)));
    Layout();
}

void CommandPalette::Move(int by) {
    if (shown.empty()) return;
    int count = static_cast<int>(shown.size());
    int index = list->GetSelection() + by;
    if (by == 1 || by == -1) index = (index + count) % count; // arrows wrap around
    else index = std::clamp(index, 0, count - 1);
    Select(index);
}

void CommandPalette::OnKey(wxKeyEvent& event) {
    if (composing) { event.Skip(); return; }
    switch (event.GetKeyCode()) {
        case WXK_UP: case WXK_NUMPAD_UP: Move(-1); return;
        case WXK_DOWN: case WXK_NUMPAD_DOWN: Move(1); return;
        case WXK_PAGEUP: case WXK_NUMPAD_PAGEUP: Move(-(visible_rows - 1)); return;
        case WXK_PAGEDOWN: case WXK_NUMPAD_PAGEDOWN: Move(visible_rows - 1); return;
        case WXK_RETURN: case WXK_NUMPAD_ENTER: Accept(); return;
        case WXK_ESCAPE:
            if (IsModal()) EndModal(wxID_CANCEL);
            else Hide();
            return;
        default: event.Skip();
    }
}

std::vector<std::string> CommandPalette::Shown() const {
    std::vector<std::string> names;
    for (auto index : shown) names.push_back(items[index].name);
    return names;
}

int CommandPalette::Selection() const {
    return list->GetSelection();
}

void CommandPalette::Select(int index) {
    if (index < 0 || index >= static_cast<int>(shown.size())) return;
    list->SetSelection(index);
    list->RefreshAll();
}

bool CommandPalette::Accept() {
    int index = list->GetSelection();
    if (index < 0 || index >= static_cast<int>(shown.size())) return false;
    auto const& item = ShownItem(index);
    if (!item.enabled) {
        wxBell();
        return false;
    }
    chosen = item.name;
    Remember(chosen);
    if (IsModal()) EndModal(wxID_OK);
    else Hide();
    return true;
}

void CommandPalette::Remember(std::string const& name) {
    auto recent = OPT_GET("App/Command Palette/Recent")->GetListString();
    std::erase_if(recent, [&](std::string const& entry) { return entry == name || entry.empty(); });
    recent.insert(recent.begin(), name);
    if (recent.size() > max_recent) recent.resize(max_recent);
    OPT_SET("App/Command Palette/Recent")->SetListString(std::move(recent));
}

std::string ShowCommandPalette(agi::Context *c) {
    CommandPalette palette(c);
    palette.ShowModal();
    return palette.Chosen();
}
