// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "command_palette.h"

#include "command/command.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "include/aegisub/menu.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "project.h"
#include "selection_controller.h"
#include "subtitle_project_access.h"
#include "video_controller.h"
#include "options.h"
#include "theme.h"
#include "workspace_tabs.h"

#include <algorithm>
#include <climits>
#include <map>
#include <memory>

#include <libaegisub/ass/time.h>

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

std::vector<size_t> *Match::Marks(Field field) {
    switch (field) {
        case Field::Name: return &positions;
        case Field::Path: return &path_positions;
        case Field::Original: return &original_positions;
        default: return nullptr;
    }
}

std::vector<std::wstring> Tokenise(std::wstring_view query) {
    std::vector<std::wstring> words;
    std::wstring word;
    for (wchar_t ch : query) {
        if (std::iswspace(ch)) {
            if (!word.empty()) words.push_back(std::move(word));
            word.clear();
        }
        else word += static_cast<wchar_t>(std::towlower(ch));
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

namespace {
/// Keys for one text: as written, and when it has Chinese characters its
/// pinyin and pinyin initials, @p pinyin_weight lower.
void AddTextKeys(std::vector<SearchKey>& keys, std::wstring_view name, Field field, int weight, int pinyin_weight) {
    // As written. Words start after separators, at capitals after lower case
    // letters, and at every Chinese character.
    SearchKey plain;
    plain.weight = weight;
    plain.field = field;
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
    if (!has_han) return;

    // Pinyin, each syllable a word, and pinyin initials: "打开字幕" is found
    // by "dakaizimu" and by "dkzm".
    SearchKey full, initials;
    full.weight = initials.weight = pinyin_weight;
    full.field = initials.field = field;
    full.pinyin = initials.pinyin = true;
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

bool SameIgnoringCase(std::wstring_view a, std::wstring_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}

void Merge(Match& into, Match from) {
    into.score += from.score;
    for (auto field : {Field::Name, Field::Path, Field::Original}) {
        auto to = into.Marks(field);
        auto const& more = *from.Marks(field);
        to->insert(to->end(), more.begin(), more.end());
        std::sort(to->begin(), to->end());
        to->erase(std::unique(to->begin(), to->end()), to->end());
    }
}
}

std::vector<SearchKey> MakeTextKeys(std::wstring_view text) {
    std::vector<SearchKey> keys;
    AddTextKeys(keys, text, Field::Name, 0, -1);
    return keys;
}

std::vector<SearchKey> MakeKeys(std::wstring_view name, std::string_view id, std::wstring_view original, std::wstring_view path) {
    std::vector<SearchKey> keys;
    AddTextKeys(keys, name, Field::Name, 0, -1);
    // The English name, for those who know the commands by it (or from a
    // guide written in English): a little below the name.
    if (!original.empty() && !SameIgnoringCase(original, name))
        AddTextKeys(keys, original, Field::Original, -3, -4);
    // The menu the command is in: "timing shift" finds Shift Times in Timing.
    if (!path.empty())
        AddTextKeys(keys, path, Field::Path, -8, -9);

    // The command id, e.g. "subtitle/open/video": found but not drawn.
    SearchKey command;
    command.weight = -6;
    command.field = Field::Hidden;
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
    if (auto marks = match.Marks(key.field); marks && !key.shown.empty()) {
        for (int i = static_cast<int>(m) - 1, j = end; i >= 0 && j >= 0; j = from[i * n + j], --i)
            marks->push_back(key.shown[j]);
        std::sort(marks->begin(), marks->end());
        marks->erase(std::unique(marks->begin(), marks->end()), marks->end());
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

std::optional<Match> RunMatch(std::wstring_view query, SearchKey const& key) {
    size_t m = query.size();
    if (m == 0) return Match{};
    std::optional<Match> best;
    for (size_t at = key.text.find(query); at != std::wstring::npos; at = key.text.find(query, at + 1)) {
        if (key.pinyin && !key.starts[at]) continue;
        // As FuzzyMatch scores a run: 16 a character, 8 more after the
        // first, 8 more at a word start (12 at the very start)
        int score = static_cast<int>(16 + 24 * (m - 1)) + (key.starts[at] ? (at == 0 ? 12 : 8) : 0) + key.weight;
        if (best && score <= best->score) continue;
        Match match;
        match.score = score;
        if (auto marks = match.Marks(key.field); marks && !key.shown.empty()) {
            for (size_t i = at; i < at + m; ++i) marks->push_back(key.shown[i]);
            marks->erase(std::unique(marks->begin(), marks->end()), marks->end());
        }
        best = std::move(match);
    }
    return best;
}

namespace {
std::optional<Match> Best(std::wstring_view query, std::vector<SearchKey> const& keys, bool runs) {
    if (!runs) return BestMatch(query, keys);
    std::optional<Match> best;
    for (auto const& key : keys)
        if (auto match = RunMatch(query, key); match && (!best || match->score > best->score))
            best = std::move(match);
    return best;
}
}

GotoQuery ParseGoto(std::wstring_view typed) {
    GotoQuery result;
    // What a Chinese input method types: "：" for ':', full-width digits
    std::wstring ascii(typed);
    for (auto& ch : ascii) {
        if (ch >= 0xff01 && ch <= 0xff5e) ch = static_cast<wchar_t>(ch - 0xfee0);
        else if (ch == L'\u3002') ch = L'.';
    }
    std::wstring_view text = ascii;
    while (!text.empty() && std::iswspace(text.front())) text.remove_prefix(1);
    while (!text.empty() && std::iswspace(text.back())) text.remove_suffix(1);
    if (text.empty()) return result;
    bool digits = false, separators = false;
    for (wchar_t ch : text) {
        if (ch >= L'0' && ch <= L'9') digits = true;
        else if (ch == L':' || ch == L'.' || ch == L',') separators = true;
        else return result;
    }
    if (!digits) return result;
    if (!separators) {
        if (text.size() > 9) return result;
        result.line = std::stoi(std::wstring(text));
        return result;
    }
    result.time = static_cast<int>(agi::Time(std::string(text.begin(), text.end())));
    return result;
}

std::optional<Match> MatchQuery(std::wstring_view query, std::vector<SearchKey> const& keys, bool runs) {
    auto whole = Best(NormaliseQuery(query), keys, runs);
    auto words = Tokenise(query);
    if (words.size() < 2) return whole;
    Match each;
    for (auto const& word : words) {
        auto match = Best(word, keys, runs);
        if (!match) return whole;
        Merge(each, *match);
    }
    if (whole && whole->score >= each.score) return whole;
    return each;
}

std::wstring CanonicalHotkey(std::wstring_view text, bool typed) {
    static constexpr std::wstring_view order[] = {L"ctrl", L"alt", L"shift", L"meta", L"cmd", L"super"};
    auto modifier = [](std::wstring const& part) -> int {
        std::wstring_view name = part;
        if (name == L"control") name = L"ctrl";
        else if (name == L"option") name = L"alt";
        else if (name == L"command") name = L"cmd";
        else if (name == L"win") name = L"super";
        for (size_t i = 0; i < std::size(order); ++i)
            if (order[i] == name) return static_cast<int>(i);
        return -1;
    };

    std::vector<std::wstring> parts;
    std::wstring part;
    bool trailing = false;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];
        bool joiner = ch == L'-' || ch == L'+';
        // ...unless it is the key itself, last after another one ("Ctrl--").
        if (joiner && i + 1 == text.size() && i > 0 && (text[i - 1] == L'-' || text[i - 1] == L'+'))
            joiner = false;
        if (joiner || std::iswspace(ch)) {
            if (!part.empty()) parts.push_back(std::move(part));
            part.clear();
            trailing = trailing || joiner;
        }
        else {
            part += static_cast<wchar_t>(std::towlower(ch));
            trailing = false;
        }
    }
    if (!part.empty()) parts.push_back(std::move(part));

    std::vector<bool> mods(std::size(order));
    std::vector<std::wstring> keys;
    for (auto const& p : parts) {
        if (int index = modifier(p); index >= 0) mods[index] = true;
        else keys.push_back(p);
    }
    bool any_mod = std::find(mods.begin(), mods.end(), true) != mods.end();
    if (keys.size() > 1) return {};
    if (typed) {
        if (keys.empty() && !(trailing && any_mod)) return {};
        if (!keys.empty() && !any_mod) {
            auto const& key = keys[0];
            bool function = key.size() >= 2 && key.size() <= 3 && key[0] == L'f'
                && std::all_of(key.begin() + 1, key.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
            if (!function) return {};
        }
    }
    std::wstring result;
    for (size_t i = 0; i < mods.size(); ++i)
        if (mods[i]) { result += order[i]; result += L'-'; }
    if (!keys.empty()) result += keys[0];
    return result;
}
}

namespace {
constexpr size_t max_recent = 12;
constexpr int visible_rows = 10;
/// At most this many lines found by '@' are listed.
constexpr size_t max_lines = 500;

bool IsPrefix(wchar_t ch, wchar_t ascii) {
    // Also as a Chinese input method types it: "＠", "："
    return ch == ascii || ch == static_cast<wchar_t>(ascii + 0xfee0);
}

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

/// Every shortcut of the command, as shown and in CanonicalHotkey form.
std::vector<std::pair<wxString, std::wstring>> Hotkeys(std::string const& name) {
    std::vector<std::pair<wxString, std::wstring>> result;
    for (auto context : {"Default", "Always", "Subtitle Edit Box", "Subtitle Grid", "Audio", "Video", "Translation Assistant"}) {
        for (auto const& key : hotkey::get_hotkey_strs(context, name)) {
            auto label = to_wx(key);
            auto canonical = palette::CanonicalHotkey(label.ToStdWstring(), false);
            if (std::none_of(result.begin(), result.end(), [&](auto const& known) { return known.second == canonical; }))
                result.emplace_back(label, canonical);
        }
    }
    return result;
}

/// Whether the shortcut @p canonical is what @p query (CanonicalHotkey with
/// typed) asks for; 2 for the very shortcut, 1 for one with all the
/// modifiers of a query that is only modifiers ("ctrl-shift-"), 0 for neither.
int HotkeyHit(std::wstring const& query, std::wstring const& canonical) {
    if (query == canonical) return 2;
    if (query.empty() || query.back() != L'-') return 0;
    auto has = [&](std::wstring_view part) {
        for (size_t start = 0, end; (end = canonical.find(L'-', start)) != std::wstring::npos; start = end + 1)
            if (std::wstring_view(canonical).substr(start, end - start) == part) return true;
        return false;
    };
    for (size_t start = 0, end; (end = query.find(L'-', start)) != std::wstring::npos; start = end + 1)
        if (!has(std::wstring_view(query).substr(start, end - start))) return 0;
    return 1;
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

    /// Draw @p text from @p x, the characters at @p marks (counted from
    /// @p offset) bold in the accent colour; stops at @p right. Where it ends.
    int DrawMarked(wxDC& dc, wxString const& text, std::vector<size_t> const& marks, size_t offset,
        int x, int y, int right, wxColour const& colour, bool enabled) const
    {
        auto font = GetFont();
        auto bold = font.Bold();
        auto accent = theme::UiColours().accent;
        auto marked_at = [&](size_t i, size_t next) {
            return i >= offset && next < marks.size() && marks[next] == i - offset;
        };
        size_t next = 0;
        for (size_t i = 0; i < text.size() && x < right;) {
            bool marked = marked_at(i, next);
            size_t j = i + 1;
            if (marked) ++next;
            else while (j < text.size() && !marked_at(j, next)) ++j;
            auto run = text.Mid(i, j - i);
            dc.SetFont(marked ? bold : font);
            if (!marked && x + dc.GetTextExtent(run).x > right)
                run = wxControl::Ellipsize(run, dc, wxELLIPSIZE_END, right - x);
            dc.SetTextForeground(marked && enabled ? accent : colour);
            dc.DrawText(run, x, y);
            x += dc.GetTextExtent(run).x;
            i = j;
        }
        dc.SetFont(font);
        return x;
    }
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

    /// A row of the '@' and ':' modes: the text with what matched marked
    /// and the start time on the right; line number, style and actor below.
    void DrawTarget(wxDC& dc, wxRect const& rect, CommandPalette::Target const& target) const {
        auto colours = theme::UiColours();
        int pad = Pad(), y = rect.y + pad, line = GetCharHeight();
        int x = rect.x + pad * 4 + IconSize();
        int right = rect.GetRight() - pad * 2;
        dc.SetFont(GetFont());
        if (!target.start.empty()) {
            auto size = dc.GetTextExtent(target.start);
            dc.SetTextForeground(colours.list_dim);
            dc.DrawText(target.start, right - size.x, y);
            right -= size.x + pad * 3;
        }
        auto text_colour = target.enabled && !target.comment ? colours.list_text : colours.list_dim;
        DrawMarked(dc, target.text, target.match.positions, 0, x, y, right, text_colour, target.enabled);
        wxString detail = target.detail;
        if (!target.enabled) detail = _("Not available now") + (detail.empty() ? wxString() : wxString::FromUTF8(" · ") + detail);
        dc.SetFont(GetFont());
        dc.SetTextForeground(colours.list_dim);
        int detail_right = rect.GetRight() - pad * 2;
        dc.DrawText(wxControl::Ellipsize(detail, dc, wxELLIPSIZE_END, detail_right - x), x, y + line + FromDIP(1));
    }

    void OnDrawItem(wxDC& dc, wxRect const& rect, size_t n) const override {
        if (palette->mode != CommandPalette::Mode::Commands) {
            if (n < palette->targets.size()) DrawTarget(dc, rect, palette->targets[n]);
            return;
        }
        auto const& item = palette->ShownItem(n);
        auto const& match = palette->ShownMatch(n);
        auto colours = theme::UiColours();
        auto text_colour = item.enabled ? colours.list_text : colours.list_dim;
        auto font = GetFont();
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
        // The shortcut that was typed, when the row was found by one.
        wxString hotkey = match.hotkey.empty() ? item.hotkey : wxString(match.hotkey);
        if (!hotkey.empty()) {
            auto size = dc.GetTextExtent(hotkey);
            dc.SetTextForeground(!match.hotkey.empty() && item.enabled ? colours.accent : colours.list_dim);
            dc.DrawText(hotkey, right - size.x, y);
            right -= size.x + pad * 3;
        }

        // Name, matched characters bold in the accent colour.
        wxString name = item.display;
        if (item.checkable && item.checked) name = wxString::FromUTF8("✓ ") + name;
        x = DrawMarked(dc, name, match.positions, name.size() - item.display.size(), x, y, right, text_colour, item.enabled);

        // The menu it is in, dim, after the name: tells apart commands of
        // the same name and shows what "timing shift" matched.
        if (!item.path.empty() && x + pad * 3 < right)
            DrawMarked(dc, item.path, match.path_positions, 0, x + pad * 3, y, right, colours.list_dim, item.enabled);

        // Below: the English name when that is what matched, then the help.
        int help_x = rect.x + pad * 4 + icon, help_y = y + line + FromDIP(1);
        int help_right = rect.GetRight() - pad * 2;
        if (!match.original_positions.empty() && !item.original.empty()) {
            help_x = DrawMarked(dc, item.original, match.original_positions, 0, help_x, help_y, help_right, colours.list_dim, item.enabled);
            if (help_x < help_right) {
                auto dot = wxString::FromUTF8(" · ");
                dc.SetTextForeground(colours.list_dim);
                dc.DrawText(dot, help_x, help_y);
                help_x += dc.GetTextExtent(dot).x;
            }
        }
        wxString help = item.help;
        if (!item.enabled) help = _("Not available now") + wxString::FromUTF8(" · ") + help;
        dc.SetFont(font);
        dc.SetTextForeground(colours.list_dim);
        if (help_x < help_right)
            dc.DrawText(wxControl::Ellipsize(help, dc, wxELLIPSIZE_END, help_right - help_x), help_x, help_y);
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
    input->SetHint(_("Type a command name, pinyin, menu, shortcut or command id"));
    list = new PaletteList(this);
    list->SetBackgroundColour(colours.list_back);
    empty = new wxStaticText(this, -1, _("No matching commands"));
    empty->SetName("command-palette-empty");
    empty->SetForegroundColour(colours.list_dim);
    auto hint = new wxStaticText(this, -1, wxString::FromUTF8("↑↓ ") + _("Select") + wxString::FromUTF8("   Enter ") + _("Run") + wxString::FromUTF8("   Esc ") + _("Close"));
    hint->SetForegroundColour(colours.list_dim);
    hint->SetFont(GetFont().Smaller());
    modes_hint = new wxStaticText(this, -1, wxString::FromUTF8("@ ") + _("Find in subtitles") + wxString::FromUTF8("   : ") + _("Go to line or time"));
    modes_hint->SetForegroundColour(colours.list_dim);
    modes_hint->SetFont(GetFont().Smaller());
    auto footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(modes_hint, wxSizerFlags().Center());
    footer->AddStretchSpacer();
    footer->Add(hint, wxSizerFlags().Center());

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(input, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
    sizer->Add(list, wxSizerFlags(1).Expand());
    sizer->Add(empty, wxSizerFlags().Border(wxALL, FromDIP(12)));
    sizer->Add(footer, wxSizerFlags().Expand().Border(wxALL, FromDIP(6)).Border(wxLEFT | wxRIGHT, FromDIP(10)));
    SetSizer(sizer);

    // Every command, as it is now: names and availability change with the project.
    recent = OPT_GET("App/Command Palette/Recent")->GetListString();
    std::erase_if(recent, [](std::string const& name) { return name.empty(); });
    auto paths = menu::CommandPaths();
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
        item.hotkeys = Hotkeys(name);
        item.original = command->StrDisplayOriginal();
        if (item.original.IsSameAs(item.display, false)) item.original.clear();
        if (auto path = paths.find(name); path != paths.end()) item.path = path->second;
        item.keys = palette::MakeKeys(Wide(item.display), name, Wide(item.original), Wide(item.path));
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
    auto typed = Wide(input->GetValue());
    shown.clear();
    matches.clear();
    targets.clear();
    mode = Mode::Commands;
    if (!typed.empty() && IsPrefix(typed[0], L'@')) {
        mode = Mode::Lines;
        FilterLines(std::wstring_view(typed).substr(1));
        return;
    }
    if (!typed.empty() && IsPrefix(typed[0], L':')) {
        mode = Mode::Goto;
        FilterGoto(std::wstring_view(typed).substr(1));
        return;
    }
    auto query = palette::NormaliseQuery(typed);
    auto hotkey_query = palette::CanonicalHotkey(typed, true);
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
            auto match = palette::MatchQuery(typed, items[i].keys);
            // A word of the help text, as a last resort.
            if (!match && query.size() >= 2 && Wide(items[i].help.Lower()).find(query) != std::wstring::npos) {
                match.emplace();
                match->score = 1;
            }
            // A shortcut: "ctrl+s" and "f6" put that command first,
            // "ctrl+shift+" lists the commands with both.
            for (auto const& [label, canonical] : items[i].hotkeys) {
                int hit = HotkeyHit(hotkey_query, canonical);
                if (!hit) continue;
                if (!match) match = palette::Match{};
                match->score += hit == 2 ? 1000 : 200;
                match->hotkey = Wide(label);
                break;
            }
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

    ShowRows(shown.size(), _("No matching commands"));
}

void CommandPalette::ShowRows(size_t count, wxString const& none) {
    list->SetItemCount(count);
    list->SetSelection(count == 0 ? wxNOT_FOUND : 0);
    list->Refresh();
    GetSizer()->Show(list, count != 0);
    GetSizer()->Show(empty, count == 0);
    if (count == 0) {
        empty->SetLabel(none);
        empty->SetMinSize(list->GetMinSize() - wxSize(FromDIP(24), FromDIP(24)));
    }
    Layout();
}

wxString CommandPalette::EmptyMessage() const {
    return GetSizer()->IsShown(empty) ? empty->GetLabel() : wxString();
}

void CommandPalette::ReadLines() {
    if (lines_read) return;
    lines_read = true;
    int row = 0;
    for (auto const& event : c->subtitleAccess->Read().Events) {
        Line line;
        line.id = event.Id;
        line.row = ++row;
        line.start = event.Start;
        line.comment = event.Comment;
        line.text = to_wx(event.GetStrippedText());
        for (auto code : {"\\N", "\\n", "\\h"}) line.text.Replace(code, " ");
        line.style = to_wx(event.Style.get());
        line.actor = to_wx(event.Actor.get());
        line.keys = palette::MakeTextKeys(Wide(line.text));
        lines.push_back(std::move(line));
    }
}

CommandPalette::Target CommandPalette::LineTarget(Line const& line) const {
    Target target;
    target.line_id = line.id;
    target.text = line.text;
    target.start = to_wx(agi::Time(line.start).GetAssFormatted());
    target.detail = wxString::Format("#%d", line.row);
    if (line.comment) target.detail += wxString::FromUTF8(" · ") + _("Comment");
    target.detail += wxString::FromUTF8(" · ") + line.style;
    if (!line.actor.empty()) target.detail += wxString::FromUTF8(" · ") + line.actor;
    target.comment = line.comment;
    target.enabled = !c->workspaceTabs || c->workspaceTabs->Permits("grid/line/next");
    return target;
}

void CommandPalette::FilterLines(std::wstring_view query) {
    if (palette::NormaliseQuery(query).empty()) {
        ShowRows(0, _("Type text to find in the subtitles: words in any order, pinyin or its initials"));
        return;
    }
    ReadLines();
    size_t found = 0;
    for (auto const& line : lines) {
        auto match = palette::MatchQuery(query, line.keys, true);
        if (!match) continue;
        if (++found > max_lines) continue;
        auto target = LineTarget(line);
        target.match = std::move(*match);
        targets.push_back(std::move(target));
    }
    ShowRows(targets.size(), _("No matching lines"));
    if (found > max_lines) {
        // Say so at the end of the list rather than silently cut it
        Target more;
        more.text = wxString::Format(_("%zu more lines match: type more to narrow them down"), found - max_lines);
        more.enabled = false;
        more.comment = true;
        targets.push_back(std::move(more));
        list->SetItemCount(targets.size());
        list->Refresh();
    }
}

void CommandPalette::FilterGoto(std::wstring_view query) {
    auto wanted = palette::ParseGoto(query);
    if (wanted.line > 0) {
        ReadLines();
        if (wanted.line <= static_cast<int>(lines.size()))
            targets.push_back(LineTarget(lines[wanted.line - 1]));
        ShowRows(targets.size(), wxString::Format(_("There is no line %d: the subtitles have %zu"), wanted.line, lines.size()));
        return;
    }
    if (wanted.time < 0) {
        ShowRows(0, _("Type a line number, or a time such as 1:23.45 or 0:01:23"));
        return;
    }

    // The video to that time, and the line on screen then (or the next one)
    bool video = c->project && c->project->VideoProvider();
    Target seek;
    seek.time = wanted.time;
    seek.text = wxString::Format(_("Seek video to %s"), to_wx(agi::Time(wanted.time).GetAssFormatted()));
    seek.detail = video ? wxString() : _("No video is open");
    seek.enabled = video && (!c->workspaceTabs || c->workspaceTabs->Permits("video/jump"));
    targets.push_back(std::move(seek));
    ReadLines();
    Line const *at = nullptr;
    // Of the lines on screen then, the one that started last
    int on_screen = 0, latest = -1;
    for (auto const& event : c->subtitleAccess->Read().Events) {
        if (!event.Comment && event.Start <= wanted.time && wanted.time < event.End && static_cast<int>(event.Start) > latest) {
            latest = event.Start;
            on_screen = event.Id;
        }
    }
    for (auto const& line : lines)
        if (on_screen && line.id == on_screen) at = &line;
    if (!at) {
        for (auto const& line : lines)
            if (!line.comment && line.start >= wanted.time && (!at || line.start < at->start)) at = &line;
    }
    if (at) targets.push_back(LineTarget(*at));
    // The line first when there is no video to seek
    if (!video && targets.size() == 2) std::swap(targets[0], targets[1]);
    ShowRows(targets.size(), wxString());
}

void CommandPalette::GoToChosen() {
    if (!chosen_target) return;
    auto const& target = *chosen_target;
    if (target.line_id) {
        for (auto const& event : c->subtitleAccess->Read().Events) {
            if (event.Id != target.line_id) continue;
            c->selectionController->SetSelectionAndActiveIDs({target.line_id}, target.line_id);
            break;
        }
    }
    if (target.time >= 0 && c->project && c->project->VideoProvider())
        c->videoController->JumpToTime(target.time);
}

void CommandPalette::Move(int by) {
    int count = static_cast<int>(list->GetItemCount());
    if (count == 0) return;
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
    if (index < 0 || index >= static_cast<int>(list->GetItemCount())) return;
    list->SetSelection(index);
    list->RefreshAll();
}

bool CommandPalette::Accept() {
    int index = list->GetSelection();
    if (mode != Mode::Commands) {
        if (index < 0 || index >= static_cast<int>(targets.size())) return false;
        if (!targets[index].enabled || (!targets[index].line_id && targets[index].time < 0)) {
            wxBell();
            return false;
        }
        chosen_target = targets[index];
        if (IsModal()) EndModal(wxID_OK);
        else Hide();
        return true;
    }
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
    palette.GoToChosen();
    return palette.Chosen();
}
