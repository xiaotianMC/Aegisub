// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// C2-C diff editor: compare the open subtitles with a subtitle file side by
// side, decide on each difference, and write the decisions to the project as
// one undoable change (docs/subtitle-compare-ui.md). It opens as a tab of the
// main window (workspace_tabs.h): the comparison session it owns freezes every
// other edit while it is open, and nothing reaches the project until "Write
// to Project".
#include "dialogs.h"
#include "utils.h"

#include "charset_detect.h"
#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "project.h"
#include "subs_controller.h"
#include "subtitle_compare.h"
#include "subtitle_compare_view.h"
#include "subtitle_format.h"
#include "theme.h"
#include "workspace_tabs.h"

#include <libaegisub/charset.h>
#include <libaegisub/exception.h>

#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/renderer.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <map>
#include <optional>
#include <span>

namespace {
namespace model = agi::ass::compare;
namespace view = subtitle_compare_view;
using view::Side;
using view::Change;
using view::Decision;

wxString field_label(model::Field field) {
    switch (field) {
        case model::Text: return _("Text");
        case model::Start: return _("Start");
        case model::End: return _("End");
        case model::Layer: return _("Layer");
        case model::Style: return _("Style");
        case model::Actor: return _("Actor");
        case model::MarginL: return _("Left margin");
        case model::MarginR: return _("Right margin");
        case model::MarginV: return _("Vertical margin");
        case model::Effect: return _("Effect");
        case model::Comment: return _("Comment");
        case model::Extra: return _("Extra data");
        default: return {};
    }
}

wxString ambiguity_label(model::Ambiguity ambiguity) {
    switch (ambiguity) {
        case model::Ambiguity::Duplicate: return _("several identical candidates");
        case model::Ambiguity::Crossing: return _("moved: the order changed");
        case model::Ambiguity::ConflictingEvidence: return _("time and text point to different lines");
        case model::Ambiguity::NoReliableMatch: return _("no reliable match");
        default: return {};
    }
}

wxString decision_label(Decision d) {
    switch (d) {
        case Decision::Accepted: return _("Right side");
        case Decision::Rejected: return _("Left side");
        case Decision::Partial: return _("Partial");
        default: return _("Pending");
    }
}

// Fields shown as columns; the others appear as chips after the text when they changed.
bool is_column(model::Field f) { return f == model::Text || f == model::Start || f == model::End || f == model::Style || f == model::Actor; }

wxString one_line(std::string const& text, size_t limit = 400) {
    auto s = to_wx(text);
    s.Replace("\n", " ");
    if (s.size() > limit) s = s.Left(limit) + wxString::FromUTF8("…");
    return s;
}

wxString describe(subtitle_apply::Report const& r) {
    wxString out;
    if (r.Ok()) {
        out = fmt_tl("Writing will change the open subtitles as one undoable step: %d lines modified, %d added, %d removed.",
            (int)r.modified, (int)r.added, (int)r.deleted);
        for (auto const& name : r.imported_styles) out += "\n" + fmt_tl("Style \"%s\" will be copied from the compared file.", name);
        for (auto const& name : r.missing_styles) out += "\n" + fmt_tl("Style \"%s\" is not defined in either file.", name);
        if (!r.imported_keys.empty()) {
            wxString keys;
            for (auto const& k : r.imported_keys) keys += (keys.empty() ? "" : ", ") + to_wx(k);
            out += "\n" + fmt_tl("Extra data will be copied from the compared file: %s", keys);
        }
        return out;
    }
    out = _("These decisions cannot be written:");
    for (auto const& refusal : r.refusals) {
        out += "\n";
        switch (refusal.problem) {
            case subtitle_apply::Problem::PartialBinding:
                out += fmt_tl("Bound lines must be applied together (group %s).", refusal.detail); break;
            case subtitle_apply::Problem::StyleConflict:
                out += fmt_tl("Style \"%s\" is defined differently in the open subtitles and in the compared file.", refusal.detail); break;
            case subtitle_apply::Problem::StyleAmbiguous:
                out += fmt_tl("Style \"%s\" is defined more than once.", refusal.detail); break;
            case subtitle_apply::Problem::InvalidExtradata:
                out += _("The extra data of the subtitles cannot be read."); break;
            default:
                out += _("Invalid selection."); break;
        }
    }
    return out;
}

/// One line for the foot: what writing will do, or the first reason it cannot.
wxString summarize(subtitle_apply::Report const& r) {
    if (!r.Ok()) {
        auto full = describe(r);
        auto lines = wxSplit(full, '\n');
        wxString out = lines.size() > 1 ? lines[1] : full;
        if (lines.size() > 2) out += fmt_tl(" (+%d more)", (int)lines.size() - 2);
        return fmt_tl("Cannot write: %s", out);
    }
    wxString out = fmt_tl("Writing changes %d lines, adds %d, removes %d", (int)r.modified, (int)r.added, (int)r.deleted);
    if (!r.imported_styles.empty()) out += wxString::FromUTF8(" · ") + fmt_tl("copies %d styles", (int)r.imported_styles.size());
    if (!r.missing_styles.empty()) out += wxString::FromUTF8(" · ") + fmt_tl("%d styles missing", (int)r.missing_styles.size());
    if (!r.imported_keys.empty()) out += wxString::FromUTF8(" · ") + _("copies extra data");
    return out;
}

// ---- colours ----
// Each kind of thing has a colour of its own: the line colours say what
// changed (added green, removed red, modified blue); a decision is told by
// which side it keeps, in the text colour, never by the change colours; the
// current difference is outlined in the text colour, as the accent would
// vanish against the blue of a modified line; the accent is for what can be
// clicked and for the keyboard.
struct Palette {
    wxColour bg, text, dim, gutter, gutter_text, border, accent;
    wxColour ins_line, ins_char, del_line, del_char, mod_line, mod_mark;
    wxColour filler, hatch, fold_bg, fold_text, hover;
    wxColour ins_mark, del_mark, warn, current;
};

wxColour mix(wxColour const& a, wxColour const& b, double t) {
    auto m = [t](unsigned char x, unsigned char y) { return (unsigned char)(x + (y - x) * t + 0.5); };
    return wxColour(m(a.Red(), b.Red()), m(a.Green(), b.Green()), m(a.Blue(), b.Blue()));
}

Palette make_palette() {
    if (theme::IsDark()) return {
        "#1e1e1e", "#d4d4d4", "#6e7681", "#252526", "#858585", "#3c3c3c", "#4daafc",
        "#1f3a28", "#2f6b40", "#3d1f22", "#7d2f35", "#1d3047", "#3b82c4",
        "#232323", "#343434", "#2a2d33", "#6cb6ff", "#2a2d2e",
        "#3fb950", "#f85149", "#d29922", "#e6e6e6"};
    return {
        "#ffffff", "#1f2328", "#8c959f", "#f6f8fa", "#6e7781", "#d0d7de", "#0969da",
        "#e6ffec", "#abf2bc", "#ffebe9", "#ffc1bf", "#dcebff", "#54aeff",
        "#f6f8fa", "#d8dee4", "#eef1f5", "#0969da", "#f3f4f6",
        "#1a7f37", "#cf222e", "#9a6700", "#1f2328"};
}

enum class Part { None, Content, Check, Accept, Reject, Reset, Fold, Overview };

/// A row of the window: a layout row of the model (an entry or a fold), or one
/// changed field of the current difference, shown in detail right under it.
struct Row {
    size_t entry = 0;
    size_t hidden = 0;
    std::optional<model::Field> field;
    bool IsFold() const { return hidden > 0; }
    bool IsDetail() const { return field.has_value(); }
};

/// "+0.6s", "−120ms": how much a time moved from the left side to the right.
wxString format_delta(int ms) {
    wxString sign = ms < 0 ? wxString::FromUTF8("−") : wxString("+");
    int a = std::abs(ms);
    if (a < 1000) return sign + wxString::Format("%dms", a);
    if (a < 60000) return sign + wxString::Format("%.1fs", a / 1000.0);
    return sign + wxString::Format("%d:%04.1f", a / 60000, (a % 60000) / 1000.0);
}

wxString category_label(view::Category c) {
    switch (c) {
        case view::Category::Time: return _("Time only");
        case view::Category::Text: return _("Text only");
        case view::Category::StyleActor: return _("Style/actor only");
        case view::Category::OtherFields: return _("Other fields only");
        case view::Category::Multiple: return _("Several fields");
        case view::Category::Inserted: return _("Inserted");
        case view::Category::Removed: return _("Removed");
    }
    return {};
}

wxString group_label(view::Category c) {
    switch (c) {
        case view::Category::Time: return _("Times");
        case view::Category::Text: return _("Text");
        case view::Category::StyleActor: return _("Style and actor");
        default: return _("Other fields");
    }
}

class ComparePanel;

// One side of the aligned view. Both panes show the same rows; the line
// numbers are in the gutter between them (DiffGutter). The right pane also
// carries the check boxes, the per-difference actions and the overview ruler.
class DiffPane final : public wxWindow {
    ComparePanel& ed;
    Side side;
    wxScrollBar *vbar, *hbar;
    int top = 0, xoff = 0;
    Part hover_part = Part::None;
    int tip_row = -1;
    Part tip_part = Part::None;
    bool dragging_overview = false;

    struct Geometry {
        int width, height;       // drawing area (without scroll bars)
        int content_x, content_right, overview_x;
    };
    Geometry Geo() const;
    int RowAt(int y) const;
    Part PartAt(int row, wxPoint pt) const;
    /// The bar of buttons over the current difference, and its buttons.
    wxRect BarRect(Geometry const& g, int y) const;
    wxRect ButtonRect(Geometry const& g, int y, Part part) const;

    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouse(wxMouseEvent& event);
    void OnKey(wxKeyEvent& event);
    void OnScroll(wxScrollEvent&);
    void DrawRow(wxDC& dc, Geometry const& g, int row, int y);
    void DrawLine(wxDC& dc, Geometry const& g, size_t entry, int y);
    void DrawDetail(wxDC& dc, Geometry const& g, int row, int y);
    /// The text of @p entry on this side from @p x, changed tokens highlighted
    /// if @p highlight. Returns the x after it.
    int DrawText(wxDC& dc, size_t entry, int x, int y, bool highlight, bool lost);
    void DrawDelta(wxDC& dc, size_t entry, model::Field f, int x, int y);
    void DrawActions(wxDC& dc, Geometry const& g, size_t entry, int row, int y);
    void DrawOverview(wxDC& dc, Geometry const& g);
    /// Repaint this pane and the gutter, which follows its scroll position.
    void Redraw();
    wxWindow* header = nullptr;
public:
    DiffPane(wxWindow* parent, ComparePanel& ed, Side side);
    Side GetSide() const { return side; }
    /// The column header above this pane, which follows its sideways scrolling.
    void SetHeader(wxWindow* w) { header = w; }
    /// Client x where the first column's text starts, and where the columns end.
    int ColumnsX() const { return RowsX() + Geo().content_x + 6 - xoff; }
    int ColumnsLeft() const { return RowsX() + Geo().content_x; }
    int ColumnsRight() const { return RowsX() + Geo().content_right; }
    int Top() const { return top; }
    /// Client x where the row area starts (after the left pane's scroll bar).
    int RowsX() const { return side == Side::Left ? vbar->GetSize().GetWidth() : 0; }
    /// Height of the row area (without the horizontal scroll bar).
    int ViewHeight() const { return Geo().height; }
    int XOffset() const { return xoff; }
    int VisibleRows() const;
    /// Clamp and apply a scroll position; @p notify tells the editor (for sync).
    void SetView(int new_top, int new_x, bool notify = false);
    /// Scroll so that rows @p first to @p last are visible, if they were not.
    void Reveal(int first, int last = -1);
    void UpdateBars();
};

// The strip between the panes, as in Meld: each side's line numbers next to
// its pane, a band joining the two sides of every difference (bent when the
// panes are scrolled apart), and arrows that take one side. » carries the left
// side over (Reject), « the right side (Accept); clicking a chosen arrow again
// resets the difference to pending. On the detail rows of the current
// difference the arrows choose one field.
class DiffGutter final : public wxWindow {
    ComparePanel& ed;
    int hover_row = -1;
    Part hover_part = Part::None;
    Side hover_side = Side::Left;
    int tip_row = -1;
    Part tip_part = Part::None;

    struct Columns { int left_number, band, right_number, right_arrow, width; };
    Columns Cols() const;
    /// Gutter y of the first visible row of @p side's pane.
    int Offset(Side side) const;
    int RowAt(Side side, int y) const;
    void OnPaint(wxPaintEvent&);
    void OnMouse(wxMouseEvent& event);
    void DrawStrip(wxDC& dc, Side side, int row, int y);
public:
    DiffGutter(wxWindow* parent, ComparePanel& ed);
    int Width() const;
};

// The names of the columns above a pane, lined up with its cells and
// scrolled sideways with it.
class DiffHeader final : public wxWindow {
    ComparePanel& ed;
    DiffPane* pane;
    void OnPaint(wxPaintEvent&);
public:
    DiffHeader(wxWindow* parent, ComparePanel& ed, DiffPane* pane);
    wxSize DoGetBestClientSize() const override;
};

class ComparePanel final : public ComparisonPage {
    friend class DiffPane;
    friend class DiffGutter;
    friend class DiffHeader;
    agi::Context *c;
    SubtitleComparison comparison;
    view::State state;
    Palette pal;
    agi::signal::Connection theme_changed;
    wxString file_name, project_name;

    // what is shown
    view::Filter filter;
    std::vector<size_t> shown;          // differences shown under the filter
    bool show_all = false, sync = true;
    std::set<size_t> expanded;
    std::vector<Row> layout;
    std::vector<int> row_of;            // entry -> its row (the fold row for hidden entries)
    std::optional<size_t> current;      // the current difference (an entry)
    int hot = -1;                       // row under the pointer, in any pane or the gutter
    std::optional<model::Field> field_cursor; // the keyboard's field among the current difference's detail rows

    // metrics
    int row_h = 22, char_w = 7, number_w = 40, arrow_w = 18, band_w = 28, time_w = 80, style_w = 80, actor_w = 0;
    int actions_w = 120 /* the current difference's bar */, overview_w = 12, content_w = 400, button_w = 54, reset_w = 30, label_w = 80, delta_w = 0;
    wxFont small_font, bold_font, small_bold_font;

    DiffPane *left, *right;
    DiffGutter *gutter;
    wxStaticText *left_title, *right_title, *counter, *stats, *checked_count;
    FlatButton *prev, *next, *accept_current, *reject_current, *undo, *redo;
    FlatButton *accept_checked, *reject_checked, *accept_all, *reject_all, *finish;
    FlatCheck *check_all, *sync_box;
    FlatButton *filter_button;
    std::vector<std::pair<view::Category, FlatCheck*>> ignore_boxes;
    bool filters_open = false;
    wxBoxSizer *main_sizer, *chips, *select_group, *all_group;
    wxStaticText *left_role, *right_role;
    wxString chips_key;
    FlatSwitch *view_switch;
    std::vector<std::pair<std::optional<view::Category>, FlatButton*>> category_buttons;
    wxBoxSizer *filter_bar;
    wxStaticText *report;
    wxStaticText *hint;
    wxString detail_hint;
    /// The changed fields of the current difference that have detail rows.
    std::vector<model::Field> DetailFields() const;

    void Measure();
    /// Rebuild the rows (after the filter, folds or the current difference
    /// changed), keeping @p anchor (default: the top row's entry) where it is.
    void Relayout(std::optional<size_t> anchor = std::nullopt);
    void ApplyFilter();
    void Changed();
    void UpdateTitles();
    void UpdateFilterBar();
    std::optional<size_t> CurrentIndex() const;
    void Go(int delta);
    bool ConfirmBatch(std::vector<size_t> const& entries, Decision d, bool all);
    void Batch(std::vector<size_t> const& entries, Decision d, bool all);
    void OnFinish();
    /// Handle the window's own shortcuts; false for keys left to the focused control.
    bool OnKey(wxKeyEvent& event);
    /// Release the comparison and remove the tab.
    void End(bool applied);
public:
    ComparePanel(wxWindow* parent, agi::Context *c, SubtitleComparison&& session, wxString const& name);
    bool TryClose(bool ask) override;
    void Started() override;

    // used by the panes and the gutter
    std::optional<size_t> CurrentEntry() const;
    bool IsCurrent(size_t entry) const { return current && *current == entry && ShownDiff(entry); }
    /// Whether @p entry is shown as a difference (under the filter).
    bool ShownDiff(size_t entry) const { return state.Visible(entry, filter); }
    /// The changed fields shown for @p entry (0 unless a shown modified line).
    model::Fields ShownFields(size_t entry) const { return ShownDiff(entry) ? state.Shown(entry, filter.ignored) : 0; }
    /// Fields that decisions in the window may touch (not the ignored ones).
    model::Fields Scope() const { return model::All & ~filter.ignored; }
    /// Check boxes are shown on every difference once one is checked.
    bool Selecting() const { return !state.CheckedEntries().empty(); }
    DiffPane* PaneFor(Side s) const { return s == Side::Left ? left : right; }
    void SetHot(int row);
    void Select(size_t entry, Side from);
    void Decide(size_t entry, Decision d);
    void SetField(size_t entry, model::Field field, Side winner);
    /// Put the keyboard's field on @p field (a detail row of the current difference).
    void SetFieldCursor(std::optional<model::Field> field);
    /// Move the keyboard's field by @p delta detail rows (from none: the first or last).
    void MoveFieldCursor(int delta);
    /// Take @p side for the keyboard's field, or for the whole current difference.
    void ChooseSide(Side side);
    void ToggleCheck(size_t entry);
    void ExpandFold(int row);
    void Scrolled(Side from);
    void ContextMenu(DiffPane* pane, size_t entry, wxPoint pos);
    wxString Tooltip(int row, Side side) const;
    /// Line colour of a difference on one side: modified lines blue on both
    /// sides, added lines green, removed red; lighter where the side is empty
    /// or the difference is decided, darker for the current one.
    wxColour RowColour(size_t entry, Side side) const;
    wxColour EdgeColour(size_t entry) const;
    /// Whether row @p row begins (ends) a run of differences of one kind.
    bool ChunkStart(int row) const;
    bool ChunkEnd(int row) const;
    /// Whether @p row is the last row of its entry (the entry or its last detail row).
    bool GroupEnd(int row) const { return row + 1 >= (int)layout.size() || !layout[row + 1].IsDetail(); }
    void RefreshGutter() { gutter->Refresh(false); }
    /// What clicking @p part of @p row does (empty for plain content); @p in_gutter for the arrows.
    wxString Help(Part part, bool in_gutter, int row) const;
    /// Show Help() in the hint line under the panes, or the default hint.
    void ShowHelp(Part part, bool in_gutter, int row);
};

// ---- DiffPane ----
DiffPane::DiffPane(wxWindow* parent, ComparePanel& ed, Side side)
: wxWindow(parent, -1, wxDefaultPosition, DipSize(300, 200), wxWANTS_CHARS | wxBORDER_THEME)
, ed(ed)
, side(side)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetName(side == Side::Left ? "compare-left" : "compare-right");
    vbar = new wxScrollBar(this, -1, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
    hbar = new wxScrollBar(this, -1, wxDefaultPosition, wxDefaultSize, wxSB_HORIZONTAL);
    Bind(wxEVT_PAINT, &DiffPane::OnPaint, this);
    Bind(wxEVT_SIZE, &DiffPane::OnSize, this);
    for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_RIGHT_DOWN, wxEVT_MOTION, wxEVT_LEAVE_WINDOW, wxEVT_MOUSEWHEEL})
        Bind(type, &DiffPane::OnMouse, this);
    Bind(wxEVT_KEY_DOWN, &DiffPane::OnKey, this);
    for (auto bar : {vbar, hbar})
        for (auto type : {wxEVT_SCROLL_TOP, wxEVT_SCROLL_BOTTOM, wxEVT_SCROLL_LINEUP, wxEVT_SCROLL_LINEDOWN,
                wxEVT_SCROLL_PAGEUP, wxEVT_SCROLL_PAGEDOWN, wxEVT_SCROLL_THUMBTRACK, wxEVT_SCROLL_THUMBRELEASE, wxEVT_SCROLL_CHANGED})
            bar->Bind(type, &DiffPane::OnScroll, this);
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(false); e.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(false); e.Skip(); });
}

DiffPane::Geometry DiffPane::Geo() const {
    Geometry g;
    auto size = GetClientSize();
    g.width = std::max(0, size.GetWidth() - vbar->GetSize().GetWidth());
    g.height = std::max(0, size.GetHeight() - hbar->GetSize().GetHeight());
    // Both sides show their lines alike; the right one also has the
    // overview ruler at its edge.
    g.content_x = 0;
    g.overview_x = side == Side::Right ? g.width - ed.overview_w : g.width;
    g.content_right = g.overview_x;
    return g;
}

int DiffPane::VisibleRows() const { return std::max(1, Geo().height / ed.row_h); }

void DiffPane::UpdateBars() {
    auto g = Geo();
    int rows = (int)ed.layout.size(), vis = std::max(1, g.height / ed.row_h);
    top = std::max(0, std::min(top, rows - vis));
    vbar->SetScrollbar(top, vis, std::max(rows, 1), std::max(1, vis - 1));
    int view_w = std::max(1, g.content_right - g.content_x);
    xoff = std::max(0, std::min(xoff, ed.content_w - view_w));
    hbar->SetScrollbar(xoff, view_w, std::max(ed.content_w, view_w), view_w);
}

void DiffPane::SetView(int new_top, int new_x, bool notify) {
    int old_top = top, old_x = xoff;
    top = new_top; xoff = new_x;
    UpdateBars();
    if (top != old_top || xoff != old_x) {
        Redraw();
        if (notify) ed.Scrolled(side);
    }
}

void DiffPane::Reveal(int first, int last) {
    if (first < 0) return;
    if (last < first) last = first;
    int vis = VisibleRows();
    if (first >= top && last < top + vis) return;
    if (last - first + 1 >= vis) SetView(first, xoff);
    else SetView(std::min(first - vis / 3, last - vis + 1 + vis / 3), xoff);
}

void DiffPane::OnSize(wxSizeEvent&) {
    auto size = GetClientSize();
    int vw = vbar->GetBestSize().GetWidth(), hh = hbar->GetBestSize().GetHeight();
    // The left pane's vertical bar is on its outer edge, so the rows run on into the gutter.
    bool r = side == Side::Right;
    vbar->SetSize(r ? size.GetWidth() - vw : 0, 0, vw, std::max(0, size.GetHeight() - hh));
    hbar->SetSize(r ? 0 : vw, size.GetHeight() - hh, std::max(0, size.GetWidth() - vw), hh);
    UpdateBars();
    Redraw();
}

void DiffPane::Redraw() {
    Refresh(false);
    if (header) header->Refresh(false);
    ed.RefreshGutter();
}

void DiffPane::OnScroll(wxScrollEvent& event) {
    if (event.GetEventObject() == vbar) SetView(vbar->GetThumbPosition(), xoff, true);
    else SetView(top, hbar->GetThumbPosition(), true);
}

int DiffPane::RowAt(int y) const {
    if (y < 0 || y >= Geo().height) return -1;
    int row = top + y / ed.row_h;
    return row < (int)ed.layout.size() ? row : -1;
}

wxRect DiffPane::BarRect(Geometry const& g, int y) const {
    // At the right edge of what is in view, whatever the sideways scrolling.
    return {g.content_right - ed.actions_w - 6, y + 1, ed.actions_w, ed.row_h - 2};
}

wxRect DiffPane::ButtonRect(Geometry const& g, int y, Part part) const {
    auto bar = BarRect(g, y);
    int h = bar.height - 6;
    switch (part) {
        case Part::Reject: return {bar.x + 3, bar.y + 3, ed.button_w, h};
        case Part::Accept: return {bar.x + 3 + ed.button_w + 3, bar.y + 3, ed.button_w, h};
        case Part::Reset: return {bar.GetRight() - 2 - ed.reset_w, bar.y + 3, ed.reset_w, h};
        default: return {};
    }
}

Part DiffPane::PartAt(int row, wxPoint pt) const {
    auto g = Geo();
    if (side == Side::Right && pt.x >= g.overview_x && pt.x < g.width) return Part::Overview;
    if (row < 0) return Part::None;
    auto const& lr = ed.layout[row];
    if (lr.IsFold()) return Part::Fold;
    if (lr.IsDetail()) return Part::Content;
    if (side == Side::Right && ed.IsCurrent(lr.entry)) {
        int y = (row - top) * ed.row_h;
        static constexpr Part pending_parts[] = {Part::Reject, Part::Accept}, decided_parts[] = {Part::Reset};
        bool pending = ed.state.GetDecision(lr.entry) == Decision::Pending;
        for (auto part : pending ? std::span<Part const>(pending_parts) : std::span<Part const>(decided_parts))
            if (ButtonRect(g, y, part).Contains(pt)) return part;
    }
    return Part::Content;
}

void DiffPane::OnMouse(wxMouseEvent& event) {
    auto client_pt = event.GetPosition();
    auto pt = client_pt - wxPoint(RowsX(), 0);
    auto g = Geo();
    if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
        int lines = event.GetWheelRotation() / std::max(1, event.GetWheelDelta()) * std::max(1, event.GetLinesPerAction());
        if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL || event.ShiftDown()) SetView(top, xoff + lines * ed.char_w * 4, true);
        else SetView(top - lines, xoff, true);
        return;
    }
    if (event.GetEventType() == wxEVT_LEAVE_WINDOW) {
        if (hover_part != Part::None) { hover_part = Part::None; Refresh(false); }
        ed.SetHot(-1);
        ed.ShowHelp(Part::None, false, -1);
        return;
    }
    auto overview_jump = [&] {
        int rows = (int)ed.layout.size();
        if (!rows || g.height <= 0) return;
        int row = std::max(0, std::min(rows - 1, int((long long)pt.y * rows / g.height)));
        SetView(row - VisibleRows() / 2, xoff, true);
    };
    if (event.GetEventType() == wxEVT_LEFT_UP) {
        if (dragging_overview && HasCapture()) ReleaseMouse();
        dragging_overview = false;
        return;
    }
    int row = RowAt(pt.y);
    Part part = PartAt(row, pt);
    if (event.GetEventType() == wxEVT_MOTION) {
        if (dragging_overview) { overview_jump(); return; }
        if (part != hover_part) { hover_part = part; Refresh(false); }
        ed.SetHot(part == Part::Overview ? -1 : row);
        // Buttons explain themselves; elsewhere the tooltip shows the line.
        auto help = ed.Help(part, false, row);
        Part tip = help.empty() ? Part::Content : part;
        if (row != tip_row || tip != tip_part) {
            tip_row = row; tip_part = tip;
            if (!help.empty()) SetToolTip(help);
            else if (row >= 0 && !ed.layout[row].IsFold()) SetToolTip(ed.Tooltip(row, side));
            else UnsetToolTip();
        }
        ed.ShowHelp(part, false, row);
        return;
    }
    if (event.GetEventType() == wxEVT_LEFT_DOWN || event.GetEventType() == wxEVT_LEFT_DCLICK) {
        SetFocus();
        if (part == Part::Overview) {
            dragging_overview = true;
            if (!HasCapture()) CaptureMouse();
            overview_jump();
            return;
        }
        if (row < 0) return;
        auto const& lr = ed.layout[row];
        switch (part) {
            case Part::Fold: ed.ExpandFold(row); return;
            case Part::Accept: ed.Decide(lr.entry, Decision::Accepted); return;
            case Part::Reject: ed.Decide(lr.entry, Decision::Rejected); return;
            case Part::Reset: ed.Decide(lr.entry, Decision::Pending); return;
            default:
                if (lr.IsDetail()) ed.SetFieldCursor(*lr.field);
                else ed.Select(lr.entry, side);
                return;
        }
    }
    if (event.GetEventType() == wxEVT_RIGHT_DOWN) {
        SetFocus();
        if (row < 0 || ed.layout[row].IsFold()) return;
        auto entry = ed.layout[row].entry;
        if (!ed.ShownDiff(entry)) return;
        if (!ed.layout[row].IsDetail()) ed.Select(entry, side);
        ed.ContextMenu(this, entry, client_pt);
    }
}

void DiffPane::OnKey(wxKeyEvent& event) {
    int vis = VisibleRows();
    // Ctrl+arrows decide: up and down pick a field of the current difference,
    // left and right take that side (of the field, or of the whole line).
    if (event.GetModifiers() == wxMOD_CONTROL) {
        switch (event.GetKeyCode()) {
            case WXK_UP: ed.MoveFieldCursor(-1); return;
            case WXK_DOWN: ed.MoveFieldCursor(1); return;
            case WXK_LEFT: ed.ChooseSide(Side::Left); return;
            case WXK_RIGHT: ed.ChooseSide(Side::Right); return;
            default: break;
        }
    }
    switch (event.GetKeyCode()) {
        case WXK_UP: SetView(top - 1, xoff, true); break;
        case WXK_DOWN: SetView(top + 1, xoff, true); break;
        case WXK_PAGEUP: SetView(top - vis, xoff, true); break;
        case WXK_PAGEDOWN: SetView(top + vis, xoff, true); break;
        case WXK_HOME: SetView(0, xoff, true); break;
        case WXK_END: SetView((int)ed.layout.size(), xoff, true); break;
        case WXK_LEFT: SetView(top, xoff - ed.char_w * 4, true); break;
        case WXK_RIGHT: SetView(top, xoff + ed.char_w * 4, true); break;
        case WXK_SPACE:
            if (auto entry = ed.CurrentEntry()) ed.ToggleCheck(*entry);
            break;
        default: event.Skip();
    }
}

void DiffPane::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetDeviceOrigin(RowsX(), 0);
    auto const& p = ed.pal;
    auto g = Geo();
    dc.SetBackground(wxBrush(p.bg));
    dc.Clear();
    dc.SetFont(GetFont());
    int rows = (int)ed.layout.size();
    for (int row = top, y = 0; row < rows && y < g.height; ++row, y += ed.row_h)
        DrawRow(dc, g, row, y);
    dc.SetPen(wxPen(p.border));
    if (side == Side::Right) {
        dc.DrawLine(g.overview_x, 0, g.overview_x, g.height);
        DrawOverview(dc, g);
    }
    if (HasFocus()) {
        dc.SetPen(wxPen(p.accent));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(0, 0, g.width, g.height);
    }
}

void DiffPane::DrawRow(wxDC& dc, Geometry const& g, int row, int y) {
    auto const& p = ed.pal;
    auto const& lr = ed.layout[row];
    wxDCClipper clip(dc, wxRect(0, y, g.width, ed.row_h));
    bool hot = row == ed.hot;
    if (lr.IsFold()) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(hot ? mix(p.fold_bg, p.fold_text, 0.12) : p.fold_bg));
        dc.DrawRectangle(0, y, g.width, ed.row_h);
        dc.SetTextForeground(p.fold_text);
        auto label = wxString::FromUTF8("⋯  ") + fmt_tl("%d hidden lines", (int)lr.hidden) + wxString::FromUTF8("  ·  ") + _("click to expand");
        auto extent = dc.GetTextExtent(label);
        dc.DrawText(label, g.content_x + 8, y + (ed.row_h - extent.GetHeight()) / 2);
        return;
    }
    if (lr.IsDetail()) { DrawDetail(dc, g, row, y); return; }
    size_t entry = lr.entry;
    auto const& line = ed.state.Entries()[entry].Get(side);
    bool is_current = ed.IsCurrent(entry);
    bool diff = ed.ShownDiff(entry);

    // background: the line colour of the difference, hatched where this side is empty
    wxColour bg = diff ? ed.RowColour(entry, side) : hot ? p.hover : p.bg;
    if (diff && hot && !is_current) bg = mix(bg, p.text, 0.04);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(bg));
    dc.DrawRectangle(0, y, g.width, ed.row_h);
    if (!line) {
        dc.SetBrush(wxBrush(diff ? mix(ed.EdgeColour(entry), bg, 0.55) : p.hatch, wxBRUSHSTYLE_BDIAGONAL_HATCH));
        dc.DrawRectangle(g.content_x, y, g.content_right - g.content_x, ed.row_h);
    }
    if (line) DrawLine(dc, g, entry, y);
    // edges of each run of differences of one kind, carried on by the gutter's bands
    if (diff) {
        dc.SetPen(wxPen(ed.EdgeColour(entry)));
        if (ed.ChunkStart(row)) dc.DrawLine(0, y, g.width, y);
        if (ed.ChunkEnd(row)) dc.DrawLine(0, y + ed.row_h - 1, g.width, y + ed.row_h - 1);
    }
    if (is_current) {
        dc.SetPen(wxPen(p.current));
        dc.DrawLine(0, y, g.width, y);
        if (ed.GroupEnd(row)) dc.DrawLine(0, y + ed.row_h - 1, g.width, y + ed.row_h - 1);
        if (side == Side::Right) DrawActions(dc, g, entry, row, y);
    }
}

int DiffPane::DrawText(wxDC& dc, size_t entry, int x, int y, bool highlight, bool lost) {
    auto const& p = ed.pal;
    auto const& e = ed.state.Entries()[entry];
    auto const& text = e.Get(side)->Value(model::Text);
    auto const& spans = side == Side::Left ? e.text.left : e.text.right;
    wxColour hl = side == Side::Right ? p.ins_char : p.del_char;
    int text_y = y + (ed.row_h - dc.GetCharHeight()) / 2;
    size_t pos = 0;
    auto draw = [&](size_t a, size_t b, bool mark) {
        if (a >= b) return;
        auto part = to_wx(text.substr(a, b - a));
        part.Replace("\n", " ");
        int w = dc.GetTextExtent(part).GetWidth();
        if (mark) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(lost ? mix(hl, p.bg, 0.5) : hl));
            dc.DrawRectangle(x, y + 3, std::max(w, 3), ed.row_h - 6);
        }
        dc.SetTextForeground(lost ? p.dim : p.text);
        dc.wxDC::DrawText(part, x, text_y);
        x += w;
    };
    if (highlight) {
        for (auto const& span : spans) {
            draw(pos, std::min(span.begin, text.size()), false);
            draw(std::min(span.begin, text.size()), std::min(span.end, text.size()), true);
            pos = std::min(span.end, text.size());
        }
    }
    draw(pos, std::min(text.size(), size_t(4000)), false);
    return x;
}

void DiffPane::DrawDelta(wxDC& dc, size_t entry, model::Field f, int x, int y) {
    // On the right side only: how far the time moved from the left side.
    auto const& e = ed.state.Entries()[entry];
    if (side != Side::Right || !e.left || !e.right || (f != model::Start && f != model::End)) return;
    int d = f == model::Start ? e.right->start - e.left->start : e.right->end - e.left->end;
    if (!d) return;
    auto const& p = ed.pal;
    dc.SetFont(ed.small_font);
    // Small adjustments matter less: they are drawn fainter.
    dc.SetTextForeground(std::abs(d) < 100 ? mix(p.gutter_text, p.bg, 0.35) : p.mod_mark);
    auto label = format_delta(d);
    dc.wxDC::DrawText(label, x, y + (ed.row_h - dc.GetTextExtent(label).GetHeight()) / 2);
    dc.SetFont(GetFont());
}

void DiffPane::DrawLine(wxDC& dc, Geometry const& g, size_t entry, int y) {
    auto const& p = ed.pal;
    auto const& e = ed.state.Entries()[entry];
    auto const& line = *e.Get(side);
    wxDCClipper clip(dc, wxRect(g.content_x, y, std::max(0, g.content_right - g.content_x), ed.row_h));
    int x = g.content_x + 6 - xoff;
    int text_y = y + (ed.row_h - dc.GetCharHeight()) / 2;
    model::Fields shown = ed.ShownFields(entry);
    bool diff = ed.ShownDiff(entry);
    wxColour mark = p.mod_mark;
    // A losing field (or a whole losing line) is dimmed and struck through.
    auto lost = [&](model::Field f) {
        if (!diff) return false;
        auto winner = ed.state.Winner(entry, f);
        return winner && *winner != side && (e.change != Change::Modified || (shown & f));
    };
    auto strike = [&](int x0, int x1) {
        dc.SetPen(wxPen(p.dim));
        dc.DrawLine(x0, y + ed.row_h / 2, x1, y + ed.row_h / 2);
    };
    // Changed columns other than the text: bold with a thin underline in the
    // modified colour, so that only the text carries filled highlights.
    auto cell = [&](model::Field f, int width) {
        auto value = one_line(line.Value(f), 200);
        bool changed = shown & f;
        {
            wxDCClipper inner(dc, wxRect(x, y, std::max(0, width - 4), ed.row_h));
            dc.SetFont(changed ? ed.bold_font : GetFont());
            dc.SetTextForeground(lost(f) ? p.dim : changed ? p.text : p.gutter_text);
            dc.wxDC::DrawText(value, x, text_y);
            int w = std::min(width - 8, dc.GetTextExtent(value).GetWidth());
            dc.SetFont(GetFont());
            if (lost(f) && !value.empty()) strike(x, x + w);
            else if (changed && !value.empty()) {
                dc.SetPen(wxPen(mark, 2));
                dc.DrawLine(x, y + ed.row_h - 5, x + w, y + ed.row_h - 5);
            }
            if (changed) DrawDelta(dc, entry, f, x + w + 5, y);
        }
        x += width;
    };
    // The right side has room after each time for how far it moved.
    int time_w = ed.time_w + (side == Side::Right ? ed.delta_w : 0);
    cell(model::Start, time_w);
    cell(model::End, time_w);
    cell(model::Style, ed.style_w);
    if (ed.actor_w) cell(model::Actor, ed.actor_w);

    int text_x = x;
    x = DrawText(dc, entry, x, y, shown & model::Text, lost(model::Text));
    if (lost(model::Text) && x > text_x) strike(text_x, x);

    // The other changed fields as one quiet chip: the field and its value when
    // it is the only one, otherwise how many (the tooltip and the detail rows
    // list them).
    dc.SetFont(ed.small_font);
    int chip_y = y + 4, chip_h = ed.row_h - 8;
    auto chip = [&](wxString const& label, wxColour const& fore, bool struck) {
        auto w = dc.GetTextExtent(label).GetWidth() + 10;
        x += 8;
        dc.SetPen(wxPen(p.border));
        dc.SetBrush(wxBrush(p.gutter));
        dc.DrawRoundedRectangle(x, chip_y, w, chip_h, 3);
        dc.SetTextForeground(fore);
        dc.wxDC::DrawText(label, x + 5, chip_y + (chip_h - dc.GetCharHeight()) / 2);
        if (struck) strike(x + 3, x + w - 3);
        x += w;
    };
    std::vector<model::Field> others;
    for (auto f : view::FieldOrder())
        if (!is_column(f) && (shown & f)) others.push_back(f);
    bool all_lost = !others.empty() && std::all_of(others.begin(), others.end(), lost);
    if (others.size() == 1) {
        auto f = others[0];
        wxString label = f == model::Comment ? (line.comment ? _("Comment") : _("Not a comment"))
            : field_label(f) + ": " + one_line(line.Value(f), 60);
        chip(label, all_lost ? p.dim : p.text, all_lost);
    }
    else if (others.size() > 1) chip(fmt_tl("+%d fields", (int)others.size()), all_lost ? p.dim : p.text, all_lost);
    if (line.comment && !(shown & model::Comment)) chip(_("Comment"), p.gutter_text, false);
    dc.SetFont(GetFont());
}

void DiffPane::DrawDetail(wxDC& dc, Geometry const& g, int row, int y) {
    // One changed field of the current difference, under its line: the field's
    // name, then this side's value in full (the gutter's arrows choose a side).
    auto const& p = ed.pal;
    auto const& lr = ed.layout[row];
    size_t entry = lr.entry;
    auto f = *lr.field;
    auto const& e = ed.state.Entries()[entry];
    auto const& line = *e.Get(side);
    wxColour bg = mix(ed.RowColour(entry, side), p.bg, 0.45);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(bg));
    dc.DrawRectangle(0, y, g.width, ed.row_h);
    // a guide joining the detail rows to their line
    dc.SetBrush(wxBrush(p.current));
    dc.DrawRectangle(g.content_x + 8, y, 2, ed.row_h);

    auto winner = ed.state.Winner(entry, f);
    bool lost = winner && *winner != side;
    {
        wxDCClipper clip(dc, wxRect(g.content_x, y, std::max(0, g.content_right - g.content_x), ed.row_h));
        int x = g.content_x + 18 - xoff;
        int text_y = y + (ed.row_h - dc.GetCharHeight()) / 2;
        dc.SetFont(ed.small_bold_font);
        dc.SetTextForeground(p.gutter_text);
        auto label = field_label(f);
        dc.wxDC::DrawText(label, x, y + (ed.row_h - dc.GetTextExtent(label).GetHeight()) / 2);
        dc.SetFont(GetFont());
        x += ed.label_w;
        if (f == model::Text) {
            int end = DrawText(dc, entry, x, y, true, lost);
            if (lost && end > x) { dc.SetPen(wxPen(p.dim)); dc.DrawLine(x, y + ed.row_h / 2, end, y + ed.row_h / 2); }
        }
        else {
            wxString value = f == model::Comment ? (line.comment ? _("Comment") : _("Not a comment")) : one_line(line.Value(f), 400);
            if (value.empty()) value = _("(empty)");
            int w = dc.GetTextExtent(value).GetWidth();
            wxColour hl = side == Side::Right ? p.ins_char : p.del_char;
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(lost ? mix(hl, p.bg, 0.5) : hl));
            dc.DrawRoundedRectangle(x - 3, y + 3, w + 6, ed.row_h - 6, 2);
            dc.SetTextForeground(lost ? p.dim : p.text);
            dc.wxDC::DrawText(value, x, text_y);
            if (lost) { dc.SetPen(wxPen(p.dim)); dc.DrawLine(x, y + ed.row_h / 2, x + w, y + ed.row_h / 2); }
            DrawDelta(dc, entry, f, x + w + 8, y);
        }
    }
    if (ed.GroupEnd(row)) {
        dc.SetPen(wxPen(p.current));
        dc.DrawLine(0, y + ed.row_h - 1, g.width, y + ed.row_h - 1);
    }
    if (ed.field_cursor == f) {
        // the keyboard's field: a frame, solid while this pane has the focus
        dc.SetPen(wxPen(p.accent, HasFocus() ? 2 : 1, HasFocus() ? wxPENSTYLE_SOLID : wxPENSTYLE_SHORT_DASH));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(g.content_x + 1, y + 1, g.content_right - g.content_x - 2, ed.row_h - 2);
    }
}

void DiffPane::DrawActions(wxDC& dc, Geometry const& g, size_t entry, int row, int y) {
    // A bar over the end of the current difference, in the colour of the
    // page with a frame and a soft edge under it, so it reads as lying on
    // the line: the two sides to choose while pending, else the side kept
    // and a reset.
    auto const& p = ed.pal;
    if (!ed.ShownDiff(entry) || g.content_right - g.content_x < ed.actions_w + 12) return;
    auto bar = BarRect(g, y);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(mix(p.bg, p.text, 0.12)));
    dc.DrawRoundedRectangle(bar.x + 1, bar.y + 2, bar.width, bar.height, 5);
    dc.SetPen(wxPen(p.border));
    dc.SetBrush(wxBrush(p.bg));
    dc.DrawRoundedRectangle(bar, 5);
    Decision d = ed.state.GetDecision(entry);
    dc.SetFont(ed.small_font);
    // Plain fields until pointed at, then the accent: they choose a side,
    // which is neither right nor wrong.
    auto button = [&](Part part, wxString const& label) {
        auto r = ButtonRect(g, y, part);
        bool hot = row == ed.hot && hover_part == part;
        dc.SetPen(hot ? wxPen(p.accent) : *wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(hot ? p.accent : mix(p.bg, p.text, 0.06)));
        dc.DrawRoundedRectangle(r, 3);
        dc.SetTextForeground(hot ? *wxWHITE : p.text);
        auto extent = dc.GetTextExtent(label);
        dc.wxDC::DrawText(label, r.x + (r.width - extent.GetWidth()) / 2, r.y + (r.height - extent.GetHeight()) / 2);
    };
    if (d == Decision::Pending) {
        button(Part::Reject, _("Use Left"));
        button(Part::Accept, _("Use Right"));
    }
    else {
        auto label = decision_label(d);
        dc.SetTextForeground(p.text);
        dc.wxDC::DrawText(label, bar.x + 10, bar.y + (bar.height - dc.GetTextExtent(label).GetHeight()) / 2);
        button(Part::Reset, wxString::FromUTF8("↺"));
    }
    dc.SetFont(GetFont());
}

void DiffPane::DrawOverview(wxDC& dc, Geometry const& g) {
    auto const& p = ed.pal;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(p.gutter));
    dc.DrawRectangle(g.overview_x, 0, ed.overview_w, g.height);
    int rows = (int)ed.layout.size();
    if (!rows || g.height <= 0) return;
    double scale = double(g.height) / rows;
    int mark_h = std::max(2, (int)scale);
    for (int r = 0; r < rows; ++r) {
        auto const& lr = ed.layout[r];
        if (lr.IsFold() || lr.IsDetail() || !ed.ShownDiff(lr.entry)) continue;
        auto const& e = ed.state.Entries()[lr.entry];
        wxColour c = e.change == Change::Modified ? p.mod_mark : e.change == Change::Inserted ? p.ins_mark : p.del_mark;
        if (ed.state.GetDecision(lr.entry) != Decision::Pending) c = mix(c, p.gutter, 0.6);
        dc.SetBrush(wxBrush(c));
        dc.DrawRectangle(g.overview_x + 2, int(r * scale), ed.overview_w - 4, mark_h);
    }
    dc.SetPen(wxPen(p.gutter_text));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(g.overview_x, int(top * scale), ed.overview_w, std::max(4, int(VisibleRows() * scale)));
}

// ---- DiffHeader ----
DiffHeader::DiffHeader(wxWindow* parent, ComparePanel& ed, DiffPane* pane)
: wxWindow(parent, -1)
, ed(ed)
, pane(pane)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetName(pane->GetSide() == Side::Left ? "compare-left-header" : "compare-right-header");
    Bind(wxEVT_PAINT, &DiffHeader::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent&) { Refresh(false); });
}

wxSize DiffHeader::DoGetBestClientSize() const {
    // Measured from the font itself: this is asked before the editor measures its fonts.
    wxClientDC dc(const_cast<DiffHeader*>(this));
    dc.SetFont(GetFont().Smaller());
    return wxSize(-1, dc.GetCharHeight() + FromDIP(8));
}

void DiffHeader::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    auto const& p = ed.pal;
    auto size = GetClientSize();
    dc.SetBackground(wxBrush(p.gutter));
    dc.Clear();
    dc.SetPen(wxPen(p.border));
    dc.DrawLine(0, size.GetHeight() - 1, size.GetWidth(), size.GetHeight() - 1);
    dc.SetFont(ed.small_font);
    dc.SetTextForeground(p.gutter_text);
    wxDCClipper clip(dc, wxRect(pane->ColumnsLeft(), 0, std::max(0, pane->ColumnsRight() - pane->ColumnsLeft()), size.GetHeight()));
    int x = pane->ColumnsX(), y = (size.GetHeight() - dc.GetCharHeight()) / 2;
    int time_w = ed.time_w + (pane->GetSide() == Side::Right ? ed.delta_w : 0);
    auto column = [&](model::Field f, int width) { dc.DrawText(field_label(f), x, y); x += width; };
    column(model::Start, time_w);
    column(model::End, time_w);
    column(model::Style, ed.style_w);
    if (ed.actor_w) column(model::Actor, ed.actor_w);
    column(model::Text, 0);
}

// ---- DiffGutter ----
DiffGutter::DiffGutter(wxWindow* parent, ComparePanel& ed)
: wxWindow(parent, -1, wxDefaultPosition, DipSize(80, 200))
, ed(ed)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetName("compare-gutter");
    Bind(wxEVT_PAINT, &DiffGutter::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent&) { Refresh(false); });
    for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_DCLICK, wxEVT_MOTION, wxEVT_LEAVE_WINDOW, wxEVT_MOUSEWHEEL})
        Bind(type, &DiffGutter::OnMouse, this);
}

int DiffGutter::Width() const { return ed.arrow_w * 2 + ed.number_w * 2 + ed.band_w; }

DiffGutter::Columns DiffGutter::Cols() const {
    Columns c;
    c.left_number = ed.arrow_w;
    c.band = c.left_number + ed.number_w;
    c.right_number = c.band + ed.band_w;
    c.right_arrow = c.right_number + ed.number_w;
    c.width = c.right_arrow + ed.arrow_w;
    return c;
}

int DiffGutter::Offset(Side side) const {
    return ed.PaneFor(side)->ClientToScreen(wxPoint(0, 0)).y - ClientToScreen(wxPoint(0, 0)).y;
}

int DiffGutter::RowAt(Side side, int y) const {
    auto pane = ed.PaneFor(side);
    y -= Offset(side);
    if (y < 0 || y >= pane->ViewHeight()) return -1;
    int row = pane->Top() + y / ed.row_h;
    return row < (int)ed.layout.size() ? row : -1;
}

void DiffGutter::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    auto const& p = ed.pal;
    auto c = Cols();
    int height = GetClientSize().GetHeight(), rh = ed.row_h;
    dc.SetBackground(wxBrush(p.gutter));
    dc.Clear();
    dc.SetFont(GetFont());
    int rows = (int)ed.layout.size();
    if (!rows) return;
    auto pane_rows = [&](Side s) {
        auto pane = ed.PaneFor(s);
        return std::make_pair(pane->Top(), std::min(rows, pane->Top() + pane->ViewHeight() / rh + 2));
    };
    auto [left_first, left_last] = pane_rows(Side::Left);
    auto [right_first, right_last] = pane_rows(Side::Right);
    int off_l = Offset(Side::Left), off_r = Offset(Side::Right);
    auto y_of = [&](Side s, int row) { return (s == Side::Left ? off_l : off_r) + (row - ed.PaneFor(s)->Top()) * rh; };

    // Bands between the two number columns: one curve per row edge, so they
    // join rows that the panes show at different heights.
    auto curve = [&](int ya, int yb, std::vector<wxPoint>& out, bool reverse) {
        constexpr int steps = 10;
        for (int i = 0; i <= steps; ++i) {
            double t = double(reverse ? steps - i : i) / steps, s = t * t * (3 - 2 * t);
            out.emplace_back(c.band + int(t * ed.band_w + 0.5), int(ya + (yb - ya) * s + 0.5));
        }
    };
    {
        int top = std::min(off_l, off_r);
        int bottom = std::max(off_l + ed.left->ViewHeight(), off_r + ed.right->ViewHeight());
        wxDCClipper clip(dc, wxRect(c.band, top, ed.band_w, std::max(0, bottom - top)));
        for (int row = std::min(left_first, right_first); row < std::max(left_last, right_last); ++row) {
            auto const& lr = ed.layout[row];
            bool detail = lr.IsDetail();
            bool diff = !lr.IsFold() && !detail && ed.ShownDiff(lr.entry);
            if (!diff && !detail && !lr.IsFold()) continue;
            int yl = y_of(Side::Left, row), yr = y_of(Side::Right, row);
            std::vector<wxPoint> shape;
            curve(yl, yr, shape, false);
            curve(yl + rh, yr + rh, shape, true);
            wxColour fill = p.fold_bg, edge = p.border;
            bool is_current = !lr.IsFold() && ed.IsCurrent(lr.entry);
            if (diff || detail) {
                auto const& e = ed.state.Entries()[lr.entry];
                fill = ed.RowColour(lr.entry, e.change == Change::Removed ? Side::Left : Side::Right);
                if (detail) fill = mix(fill, p.bg, 0.45);
                edge = ed.EdgeColour(lr.entry);
            }
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(fill));
            dc.DrawPolygon((int)shape.size(), shape.data());
            if (lr.IsFold()) {
                dc.SetPen(wxPen(edge));
                dc.DrawLines(11, shape.data());
                dc.DrawLines(11, shape.data() + 11);
                continue;
            }
            dc.SetPen(wxPen(is_current ? p.current : edge));
            if (is_current ? !detail : ed.ChunkStart(row)) dc.DrawLines(11, shape.data());
            if (is_current ? ed.GroupEnd(row) : ed.ChunkEnd(row)) dc.DrawLines(11, shape.data() + 11);
            if (!diff) continue;
            // Real doubts about the match: the rows cross, or several candidates.
            auto const& e = ed.state.Entries()[lr.entry];
            wxString mark;
            if (e.ambiguity == model::Ambiguity::Crossing) mark = wxString::FromUTF8("⇅");
            else if (e.ambiguity == model::Ambiguity::Duplicate || e.ambiguity == model::Ambiguity::ConflictingEvidence) mark = "?";
            if (!mark.empty()) {
                auto extent = dc.GetTextExtent(mark);
                dc.SetTextForeground(p.warn);
                dc.DrawText(mark, c.band + (ed.band_w - extent.GetWidth()) / 2, (yl + yr + rh - extent.GetHeight()) / 2);
            }
        }
    }
    for (auto s : {Side::Left, Side::Right}) {
        auto [first, last] = s == Side::Left ? std::make_pair(left_first, left_last) : std::make_pair(right_first, right_last);
        int off = s == Side::Left ? off_l : off_r;
        int x = s == Side::Left ? 0 : c.right_number;
        wxDCClipper clip(dc, wxRect(x, off, ed.arrow_w + ed.number_w, std::max(0, std::min(height - off, ed.PaneFor(s)->ViewHeight()))));
        for (int row = first; row < last; ++row) DrawStrip(dc, s, row, y_of(s, row));
    }
    dc.SetPen(wxPen(p.border));
    dc.DrawLine(c.band, 0, c.band, height);
    dc.DrawLine(c.right_number - 1, 0, c.right_number - 1, height);
}

void DiffGutter::DrawStrip(wxDC& dc, Side side, int row, int y) {
    auto const& p = ed.pal;
    auto c = Cols();
    int rh = ed.row_h;
    bool left = side == Side::Left;
    int x0 = left ? 0 : c.right_number, w = ed.arrow_w + ed.number_w;
    int number_x = left ? c.left_number : c.right_number, arrow_x = left ? 0 : c.right_arrow;
    auto const& lr = ed.layout[row];
    dc.SetPen(*wxTRANSPARENT_PEN);
    if (lr.IsFold()) {
        dc.SetBrush(wxBrush(p.fold_bg));
        dc.DrawRectangle(x0, y, w, rh);
        dc.SetPen(wxPen(p.border));
        dc.DrawLine(x0, y, x0 + w, y);
        dc.DrawLine(x0, y + rh - 1, x0 + w, y + rh - 1);
        return;
    }
    auto const& e = ed.state.Entries()[lr.entry];
    bool is_current = ed.IsCurrent(lr.entry);
    bool detail = lr.IsDetail();
    bool diff = ed.ShownDiff(lr.entry);
    if (diff) {
        wxColour bg = ed.RowColour(lr.entry, side);
        dc.SetBrush(wxBrush(detail ? mix(bg, p.bg, 0.45) : bg));
        dc.DrawRectangle(x0, y, w, rh);
        dc.SetPen(wxPen(is_current ? p.current : ed.EdgeColour(lr.entry)));
        if (is_current ? !detail : ed.ChunkStart(row)) dc.DrawLine(x0, y, x0 + w, y);
        if (is_current ? ed.GroupEnd(row) : ed.ChunkEnd(row)) dc.DrawLine(x0, y + rh - 1, x0 + w, y + rh - 1);
    }
    else if (row == ed.hot) {
        dc.SetBrush(wxBrush(p.hover));
        dc.DrawRectangle(x0, y, w, rh);
    }
    int text_y = y + (rh - dc.GetCharHeight()) / 2;
    // The right number is a check box where it is pointed at, checked, or
    // while a selection is being made.
    bool hovering_check = row == hover_row && hover_part == Part::Check;
    if (!left && diff && !detail && (ed.state.Checked(lr.entry) || ed.Selecting() || hovering_check)) {
        int s = std::min(FromDIP(14), rh - 8);
        flat::DrawCheck(dc, this, wxRect(number_x + (ed.number_w - s) / 2, y + (rh - s) / 2, s, s),
            ed.state.Checked(lr.entry) ? 1 : 0, flat::Colours(this));
    }
    else if (!detail && e.Get(side)) {
        auto number = wxString::Format("%zu", e.Get(side)->number);
        dc.SetTextForeground(diff || is_current ? p.text : p.gutter_text);
        int nx = left ? number_x + ed.number_w - 5 - dc.GetTextExtent(number).GetWidth() : number_x + 5;
        dc.DrawText(number, nx, text_y);
    }
    if (!diff) return;
    // The arrows appear on the pointed and the current line (and on its
    // detail rows); elsewhere only the side a decided line keeps, quieter.
    bool active = row == ed.hot || is_current;
    if (!active && ed.state.GetDecision(lr.entry) == Decision::Pending) return;
    // » takes the left side (Reject), « the right side (Accept).
    Part part = left ? Part::Reject : Part::Accept;
    bool chosen, partial = false;
    if (detail) {
        auto winner = ed.state.Winner(lr.entry, *lr.field);
        chosen = winner && *winner == side;
    }
    else {
        Decision d = ed.state.GetDecision(lr.entry);
        chosen = d == (left ? Decision::Rejected : Decision::Accepted);
        partial = d == Decision::Partial;
    }
    if (!active && !chosen && !partial) return;
    bool hot = row == hover_row && hover_part == part && hover_side == side;
    wxColour colour = hot ? p.accent : active ? p.current : mix(p.current, p.bg, 0.35);
    wxRect box(arrow_x + 2, y + 3, ed.arrow_w - 4, rh - 6);
    if (chosen || hot || partial) {
        dc.SetPen(wxPen(partial && !chosen && !hot ? p.border : colour));
        dc.SetBrush(wxBrush(chosen ? colour : hot ? mix(colour, p.bg, 0.85) : p.bg));
        dc.DrawRoundedRectangle(box, 3);
    }
    wxString glyph = wxString::FromUTF8(left ? "»" : "«");
    auto extent = dc.GetTextExtent(glyph);
    dc.SetTextForeground(chosen ? p.bg : hot ? colour : p.gutter_text);
    dc.DrawText(glyph, box.x + (box.width - extent.GetWidth()) / 2, box.y + (box.height - extent.GetHeight()) / 2);
}

void DiffGutter::OnMouse(wxMouseEvent& event) {
    auto type = event.GetEventType();
    if (type == wxEVT_MOUSEWHEEL) {
        int lines = event.GetWheelRotation() / std::max(1, event.GetWheelDelta()) * std::max(1, event.GetLinesPerAction());
        for (auto pane : {ed.left, ed.right}) pane->SetView(pane->Top() - lines, pane->XOffset());
        return;
    }
    if (type == wxEVT_LEAVE_WINDOW) {
        if (hover_row != -1) { hover_row = -1; hover_part = Part::None; Refresh(false); }
        ed.SetHot(-1);
        ed.ShowHelp(Part::None, true, -1);
        return;
    }
    auto pt = event.GetPosition();
    auto c = Cols();
    Side side = pt.x < c.band + ed.band_w / 2 ? Side::Left : Side::Right;
    int row = RowAt(side, pt.y);
    Part part = Part::None;
    if (row >= 0) {
        auto const& lr = ed.layout[row];
        bool diff = !lr.IsFold() && ed.ShownDiff(lr.entry);
        if (lr.IsFold()) part = Part::Fold;
        else if (diff && side == Side::Left && pt.x < c.left_number) part = Part::Reject;
        else if (diff && side == Side::Right && pt.x >= c.right_arrow) part = Part::Accept;
        else if (diff && side == Side::Right && !lr.IsDetail() && pt.x >= c.right_number) part = Part::Check;
        else part = Part::Content;
    }
    if (type == wxEVT_MOTION) {
        if (row != hover_row || part != hover_part || side != hover_side) {
            hover_row = row; hover_part = part; hover_side = side;
            Refresh(false);
        }
        ed.SetHot(row);
        if (row != tip_row || part != tip_part) {
            tip_row = row; tip_part = part;
            if (part == Part::Reject || part == Part::Accept || part == Part::Fold || part == Part::Check) SetToolTip(ed.Help(part, true, row));
            else if (part == Part::Content) SetToolTip(ed.Tooltip(row, side));
            else UnsetToolTip();
        }
        ed.ShowHelp(part, true, row);
        return;
    }
    if (row < 0) return;
    auto const& lr = ed.layout[row];
    switch (part) {
        case Part::Fold: ed.ExpandFold(row); return;
        case Part::Check: ed.ToggleCheck(lr.entry); return;
        case Part::Reject:
        case Part::Accept: {
            if (lr.IsDetail()) {
                ed.SetFieldCursor(*lr.field);
                ed.SetField(lr.entry, *lr.field, part == Part::Accept ? Side::Right : Side::Left);
                return;
            }
            Decision want = part == Part::Accept ? Decision::Accepted : Decision::Rejected;
            ed.Decide(lr.entry, ed.state.GetDecision(lr.entry) == want ? Decision::Pending : want);
            return;
        }
        default:
            if (lr.IsDetail()) ed.SetFieldCursor(*lr.field);
            else ed.Select(lr.entry, side);
            return;
    }
}

// ---- ComparePanel ----
enum {
    ID_PREV = wxID_HIGHEST + 1, ID_NEXT, ID_ACCEPT_CURRENT, ID_REJECT_CURRENT, ID_UNDO, ID_REDO,
    ID_MENU_ACCEPT, ID_MENU_REJECT, ID_MENU_RESET, ID_MENU_CHECK, ID_MENU_FIELD,
};

ComparePanel::ComparePanel(wxWindow* parent, agi::Context *c, SubtitleComparison&& session, wxString const& name)
: ComparisonPage(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
, c(c)
, comparison(std::move(session))
, state(comparison.Current(), comparison.Baseline(), comparison.Fields(), comparison.Items())
, pal(make_palette())
, file_name(name)
{
    auto project_file = c->subsController->Filename();
    project_name = project_file.empty() ? _("Untitled") : wxString(project_file.filename().wstring());

    // Three bands round the panes: the toolbar, the pane headers, the foot.
    // Toolbar: where you are, deciding the current difference, what is
    // shown, how far the decisions have come, undo.
    auto bar = new wxBoxSizer(wxHORIZONTAL);
    theme::UseSurface(this);
    int gap = FromDIP(4);
    auto make = [&](int id, wxString const& label, wxString const& tip, char const* name, FlatGlyph glyph = FlatGlyph::None) {
        auto b = new FlatButton(this, label, id);
        b->SetGlyph(glyph);
        if (!tip.empty()) b->SetToolTip(tip);
        b->SetName(name);
        b->SetLeastSize(wxSize(-1, FromDIP(26)));
        return b;
    };
    auto add = [&](int id, wxString const& label, wxString const& tip, char const* name, FlatGlyph glyph = FlatGlyph::None) {
        auto b = make(id, label, tip, name, glyph);
        bar->Add(b, wxSizerFlags().Center().Border(wxRIGHT, gap));
        return b;
    };
    auto separator = [&] { bar->AddSpacer(3 * gap); };
    prev = make(ID_PREV, {}, _("Previous difference (Shift+F7)"), "compare-prev", FlatGlyph::Up);
    next = make(ID_NEXT, {}, _("Next difference (F7)"), "compare-next", FlatGlyph::Down);
    counter = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxALIGN_CENTRE_HORIZONTAL);
    counter->SetName("compare-counter");
    counter->SetFont(GetFont().Bold());
    counter->SetMinSize(wxSize(counter->GetTextExtent("9999 / 9999").GetWidth() + FromDIP(12), -1));
    bar->Add(prev, wxSizerFlags().Center());
    bar->Add(counter, wxSizerFlags().Center());
    bar->Add(next, wxSizerFlags().Center());
    separator();
    reject_current = add(ID_REJECT_CURRENT, _("Use &Left"), _("Take the left side of the current difference (Ctrl+Left)"), "compare-reject-current");
    accept_current = add(ID_ACCEPT_CURRENT, _("Use &Right"), _("Keep the right side of the current difference (Ctrl+Right)"), "compare-accept-current");
    separator();
    view_switch = new FlatSwitch(this, {_("Differences only"), _("Everything")});
    view_switch->SetName("compare-view");
    view_switch->SetItemToolTip(0, _("Fold long runs of identical lines, keeping context around each difference"));
    view_switch->SetItemToolTip(1, _("Show every line"));
    bar->Add(view_switch, wxSizerFlags().Center().Border(wxRIGHT, 2 * gap));
    filter_button = add(wxID_ANY, _("Filter"), _("Show one kind of difference, leave fields out of the comparison, sync scrolling"), "compare-filter", FlatGlyph::Filter);
    // What the filters do while their row is closed, each removable on its own.
    chips = new wxBoxSizer(wxHORIZONTAL);
    bar->Add(chips, wxSizerFlags().Center());
    // The counts take what room is left, shortened with an ellipsis when
    // the chips need it (the whole of them in the tooltip).
    stats = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END | wxALIGN_RIGHT);
    stats->SetForegroundColour(flat::Colours(this).dim);
    stats->SetName("compare-stats");
    bar->Add(stats, wxSizerFlags(1).Center().Border(wxLEFT | wxRIGHT, 3 * gap));
    undo = add(ID_UNDO, {}, _("Undo the last decision (Ctrl+Z)"), "compare-undo", FlatGlyph::Undo);
    redo = make(ID_REDO, {}, _("Redo (Ctrl+Y)"), "compare-redo", FlatGlyph::Redo);
    bar->Add(redo, wxSizerFlags().Center());

    // The filters' row, under the toolbar while the Filter button is on: one
    // category at a time, the chosen one in the accent colour, field groups
    // to leave out, and the scrolling.
    filter_bar = new wxBoxSizer(wxHORIZONTAL);
    filter_bar->Add(flat::DimLabel(this, flat::WithoutColon(_("Show:"))), wxSizerFlags().Center().Border(wxRIGHT, 2 * gap));
    auto add_category = [&](std::optional<view::Category> category) {
        auto b = make(wxID_ANY, "", category ? _("Show only this kind of difference; Use Left for All and Use Right for All then apply to these") : _("Show every kind of difference"),
            (wxString("compare-category-") + (category ? view::CategoryName(*category) : "all")).c_str());
        b->Bind(wxEVT_BUTTON, [this, category](wxCommandEvent&) { filter.category = category; ApplyFilter(); });
        filter_bar->Add(b, wxSizerFlags().Center().Border(wxRIGHT, gap));
        category_buttons.emplace_back(category, b);
    };
    add_category(std::nullopt);
    for (auto category : view::CategoryOrder()) add_category(category);
    filter_bar->AddSpacer(4 * gap);
    filter_bar->Add(flat::DimLabel(this, flat::WithoutColon(_("Ignore:"))), wxSizerFlags().Center().Border(wxRIGHT, 2 * gap));
    for (auto group : {view::Category::Time, view::Category::Text, view::Category::StyleActor, view::Category::OtherFields}) {
        auto box = new FlatCheck(this, group_label(group));
        box->SetName(wxString("compare-ignore-") + view::CategoryName(group));
        box->SetToolTip(_("Leave these fields out: they are not highlighted, not counted, and decisions here leave them as they are"));
        box->Bind(wxEVT_CHECKBOX, [this, group, box](wxCommandEvent&) {
            if (box->GetValue()) filter.ignored |= view::CategoryFields(group);
            else filter.ignored &= ~view::CategoryFields(group);
            ApplyFilter();
        });
        filter_bar->Add(box, wxSizerFlags().Center().Border(wxRIGHT, 3 * gap));
        ignore_boxes.emplace_back(group, box);
    }
    filter_bar->AddStretchSpacer();
    sync_box = new FlatCheck(this, _("Sync scrolling"));
    sync_box->SetName("compare-sync"); sync_box->SetValue(true);
    sync_box->SetToolTip(_("Scroll both sides together. When off, each side scrolls on its own and selecting a line locates it on the other side."));
    filter_bar->Add(sync_box, wxSizerFlags().Center());

    // Pane headers: each side's role and file, the swap between them.
    auto swap = make(wxID_ANY, {}, _("Swap Sides") + ": " +
        _("Show the open subtitles on the other side. Decisions keep their effect on the open subtitles."), "compare-swap", FlatGlyph::Swap);
    left_role = flat::DimLabel(this, _("Base version"));
    right_role = flat::DimLabel(this, _("Current version"));
    left_title = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
    right_title = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
    left_title->SetName("compare-left-title"); right_title->SetName("compare-right-title");
    auto bold = GetFont().Bold();
    left_title->SetFont(bold); right_title->SetFont(bold);
    left = new DiffPane(this, *this, Side::Left);
    right = new DiffPane(this, *this, Side::Right);
    gutter = new DiffGutter(this, *this);
    auto left_header = new DiffHeader(this, *this, left);
    auto right_header = new DiffHeader(this, *this, right);
    left->SetHeader(left_header);
    right->SetHeader(right_header);
    int title_h = swap->GetBestSize().GetHeight() + 2 * gap;
    auto title_row = [&](wxStaticText* role, wxStaticText* title) {
        auto row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(role, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, 2 * gap));
        row->Add(title, wxSizerFlags(1).Center());
        row->SetMinSize(wxSize(-1, title_h));
        return row;
    };

    // The foot: deciding many at once, what writing will do, and the
    // buttons that end the comparison. Hovering over the panes' controls
    // tells what they do where the summary is.
    check_all = new FlatCheck(this, _("Select all differences"));
    check_all->SetName("compare-check-all");
    checked_count = new wxStaticText(this, -1, "");
    checked_count->SetName("compare-checked-count");
    checked_count->SetFont(GetFont().Bold());
    reject_checked = make(wxID_ANY, _("Use Left for Selected"), {}, "compare-reject-checked");
    accept_checked = make(wxID_ANY, _("Use Right for Selected"), {}, "compare-accept-checked");
    reject_all = make(wxID_ANY, _("Use Left for All"), _("Take the left side of every difference shown"), "compare-reject-all");
    accept_all = make(wxID_ANY, _("Use Right for All"), _("Keep the right side of every difference shown"), "compare-accept-all");
    // Selected ones or all of them: one pair of buttons at a time.
    select_group = new wxBoxSizer(wxHORIZONTAL);
    select_group->Add(checked_count, wxSizerFlags().Center().Border(wxRIGHT, 3 * gap));
    select_group->Add(reject_checked, wxSizerFlags().Center().Border(wxRIGHT, gap));
    select_group->Add(accept_checked, wxSizerFlags().Center());
    all_group = new wxBoxSizer(wxHORIZONTAL);
    all_group->Add(reject_all, wxSizerFlags().Center().Border(wxRIGHT, gap));
    all_group->Add(accept_all, wxSizerFlags().Center());

    report = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    report->SetName("compare-report");
    hint = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    hint->SetName("compare-hint");
    hint->SetForegroundColour(pal.accent);
    hint->Hide();
    detail_hint = _("This line changed several fields, listed under it: Ctrl+Up/Down picks a field, Ctrl+Left takes the left side, Ctrl+Right keeps the right side.");

    // Cancel, then the button the page is for in the accent colour (first
    // on Windows), as flat::DialogButtons lays out a dialog's.
    finish = new FlatButton(this, "", wxID_OK, true);
    auto cancel = new FlatButton(this, "", wxID_CANCEL);
    for (auto b : {finish, cancel}) b->SetLeastSize(wxSize(FromDIP(76), FromDIP(28)));
    cancel->SetToolTip(_("End the comparison without writing to the subtitles"));
    finish->SetToolTip(_("Write the decisions to the open subtitles as one undoable change and end the comparison"));

    auto foot = new wxBoxSizer(wxHORIZONTAL);
    foot->Add(check_all, wxSizerFlags().Center().Border(wxRIGHT, 3 * gap));
    foot->Add(select_group, wxSizerFlags().Center());
    foot->Add(all_group, wxSizerFlags().Center());
    foot->AddSpacer(5 * gap);
    foot->Add(report, wxSizerFlags(1).Center());
    foot->Add(hint, wxSizerFlags(1).Center());
    foot->AddSpacer(3 * gap);
#ifdef __WXMSW__
    foot->Add(finish, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(6)));
    foot->Add(cancel, wxSizerFlags().Center());
#else
    foot->Add(cancel, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(6)));
    foot->Add(finish, wxSizerFlags().Center());
#endif

    auto left_column = new wxBoxSizer(wxVERTICAL);
    left_column->Add(title_row(left_role, left_title), wxSizerFlags().Expand());
    left_column->Add(left_header, wxSizerFlags().Expand());
    left_column->Add(left, wxSizerFlags(1).Expand());
    auto right_column = new wxBoxSizer(wxVERTICAL);
    right_column->Add(title_row(right_role, right_title), wxSizerFlags().Expand());
    right_column->Add(right_header, wxSizerFlags().Expand());
    right_column->Add(right, wxSizerFlags(1).Expand());
    // The left column's width is set on resize so both sides show as much text.
    // The gutter starts level with the panes, below the headers; the swap
    // sits over it, between the files it swaps.
    auto gutter_column = new wxBoxSizer(wxVERTICAL);
    auto swap_row = new wxBoxSizer(wxHORIZONTAL);
    swap_row->Add(swap, wxSizerFlags().Center());
    swap_row->SetMinSize(wxSize(-1, title_h));
    gutter_column->Add(swap_row, wxSizerFlags().Center());
    gutter_column->AddSpacer(left_header->GetBestSize().GetHeight());
    gutter_column->Add(gutter, wxSizerFlags(1).Expand());
    auto panes = new wxBoxSizer(wxHORIZONTAL);
    panes->Add(left_column, wxSizerFlags().Expand());
    panes->Add(gutter_column, wxSizerFlags().Expand());
    panes->Add(right_column, wxSizerFlags(1).Expand());

    main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(bar, wxSizerFlags().Expand().Border(wxALL, 8));
    main_sizer->Add(filter_bar, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
    main_sizer->Add(panes, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, 8));
    main_sizer->Add(foot, wxSizerFlags().Expand().Border(wxALL, 8));
    SetName("compare-page");
    SetSizer(main_sizer);
    filter_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        filters_open = !filters_open;
        UpdateFilterBar();
        Layout();
    });

    Measure();
    // The right pane also holds the overview ruler and room for how far
    // times moved: it gets that much more width than the left one.
    left->SetMinSize(DipSize(320, 200));
    right->SetMinSize(wxSize(320 + overview_w + 2 * delta_w, 200));
    gutter->SetMinSize(wxSize(gutter->Width(), 200));
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        int panes = GetClientSize().GetWidth() - 16 - gutter->Width();
        int extra = overview_w + 2 * delta_w;
        left->SetMinSize(wxSize(std::max(320, (panes - extra) / 2), 200));
        event.Skip(); // the default handler lays the sizers out
    });
    shown = state.VisibleDiffs(filter);
    if (!shown.empty()) current = shown.front();
    Relayout();


    // Events
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Go(-1); }, ID_PREV);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Go(1); }, ID_NEXT);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (auto e = CurrentEntry()) Decide(*e, Decision::Accepted); }, ID_ACCEPT_CURRENT);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (auto e = CurrentEntry()) Decide(*e, Decision::Rejected); }, ID_REJECT_CURRENT);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (state.Undo()) Changed(); }, ID_UNDO);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (state.Redo()) Changed(); }, ID_REDO);

    view_switch->on_choose = [this](int index) {
        show_all = index == 1;
        if (!show_all) expanded.clear();
        Relayout(current);
    };
    sync_box->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        sync = sync_box->GetValue();
        if (sync) left->SetView(right->Top(), right->XOffset());
    });
    swap->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        state.SetProjectSide(view::Other(state.ProjectSide()));
        ApplyFilter(); // inserted and removed lines trade categories
    });
    check_all->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        for (auto e : shown) state.Check(e, check_all->GetValue());
        Changed();
    });
    auto checked_shown = [this] {
        std::vector<size_t> out;
        for (auto e : state.CheckedEntries()) if (ShownDiff(e)) out.push_back(e);
        return out;
    };
    accept_checked->Bind(wxEVT_BUTTON, [this, checked_shown](wxCommandEvent&) { Batch(checked_shown(), Decision::Accepted, false); });
    reject_checked->Bind(wxEVT_BUTTON, [this, checked_shown](wxCommandEvent&) { Batch(checked_shown(), Decision::Rejected, false); });
    accept_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Batch(shown, Decision::Accepted, true); });
    reject_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Batch(shown, Decision::Rejected, true); });
    finish->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnFinish(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { TryClose(true); }, wxID_CANCEL);
    // Keys reach this tab before the main window: its hotkeys and menu
    // shortcuts would act on the locked subtitles. Keys the window does not
    // use go on to the focused control, and no further.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (!OnKey(event)) event.DoAllowNextEvent();
    });

    // Follow the appearance while open.
    theme_changed = theme::AddChangeListener([this] {
        pal = make_palette();
        hint->SetForegroundColour(pal.accent);
        UpdateTitles();
        for (auto w : {static_cast<wxWindow*>(left), static_cast<wxWindow*>(right), static_cast<wxWindow*>(gutter)}) w->Refresh(false);
        for (auto w : GetChildren()) if (dynamic_cast<DiffHeader*>(w)) w->Refresh(false);
    });

    Changed();
}

bool ComparePanel::OnKey(wxKeyEvent& event) {
    int key = event.GetKeyCode(), mods = event.GetModifiers();
    if (key == WXK_ESCAPE && mods == wxMOD_NONE && field_cursor) { SetFieldCursor(std::nullopt); return true; }
    if ((key == WXK_F7 && mods == wxMOD_NONE) || (key == WXK_DOWN && mods == wxMOD_ALT)) { Go(1); return true; }
    if ((key == WXK_F7 && mods == wxMOD_SHIFT) || (key == WXK_UP && mods == wxMOD_ALT)) { Go(-1); return true; }
    if (mods == wxMOD_ALT && flat::PressMnemonic(this, key)) return true;
    if (key == 'Z' && mods == wxMOD_CONTROL) { if (state.Undo()) Changed(); return true; }
    if ((key == 'Y' && mods == wxMOD_CONTROL) || (key == 'Z' && mods == (wxMOD_CONTROL | wxMOD_SHIFT))) {
        if (state.Redo()) Changed();
        return true;
    }
    return false;
}

void ComparePanel::Started() {
    Layout();
    if (auto e = CurrentEntry()) {
        int row = row_of[*e];
        int last = row;
        while (!GroupEnd(last)) ++last;
        right->Reveal(row, last);
        left->SetView(right->Top(), right->XOffset());
    }
    right->SetFocus();
}

void ComparePanel::Measure() {
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    char_w = std::max(1, dc.GetCharWidth());
    int char_h = dc.GetCharHeight();
    row_h = std::max(26, char_h + 12);
    small_font = GetFont().Smaller();
    bold_font = GetFont().Bold();
    small_bold_font = small_font.Bold();
    size_t max_number = 1;
    int style = dc.GetTextExtent(_("Style")).GetWidth(), actor = 0, text = 0;
    bool time_changes = false;
    auto estimate = [&](std::string const& s) {
        int w = 0;
        for (size_t i = 0; i < s.size();) {
            unsigned char ch = s[i];
            size_t n = ch < 0x80 ? 1 : (ch >> 5) == 6 ? 2 : (ch >> 4) == 14 ? 3 : (ch >> 3) == 30 ? 4 : 1;
            w += ch < 0x80 ? char_w : char_w * 2;
            i += n;
        }
        return w;
    };
    std::set<std::string> styles, actors;
    for (auto const& e : state.Entries()) {
        time_changes |= (e.changed & (model::Start | model::End)) != 0;
        for (auto const* line : {e.left ? &*e.left : nullptr, e.right ? &*e.right : nullptr}) {
            if (!line) continue;
            max_number = std::max(max_number, line->number);
            styles.insert(line->Value(model::Style));
            if (!line->Value(model::Actor).empty()) actors.insert(line->Value(model::Actor));
            int w = estimate(line->Value(model::Text));
            for (auto f : view::FieldOrder())
                if (!is_column(f) && (e.changed & f)) w += estimate(line->Value(f)) + char_w * 16;
            text = std::max(text, w);
        }
    }
    for (auto const& s : styles) style = std::max(style, dc.GetTextExtent(to_wx(s)).GetWidth());
    for (auto const& a : actors) actor = std::max(actor, dc.GetTextExtent(to_wx(a)).GetWidth());
    number_w = dc.GetTextExtent(wxString::Format("%zu", max_number)).GetWidth() + 10;
    arrow_w = std::max(18, dc.GetTextExtent(wxString::FromUTF8("»")).GetWidth() + 10);
    band_w = std::max(24, dc.GetTextExtent(wxString::FromUTF8("⇅")).GetWidth() + 12);
    dc.SetFont(bold_font);
    time_w = dc.GetTextExtent("0:00:00.000").GetWidth() + 14;
    dc.SetFont(small_font);
    // room for "+12.5s" after a changed time, on the right side
    delta_w = time_changes ? dc.GetTextExtent(wxString::FromUTF8("−0:00.0")).GetWidth() + 6 : 0;
    style_w = std::min(style, char_w * 22) + 16;
    actor_w = actors.empty() ? 0 : std::min(actor, char_w * 16) + 16;
    content_w = 6 + (time_w + delta_w) * 2 + style_w + actor_w + text + char_w * 12;

    button_w = std::max(dc.GetTextExtent(_("Use Left")).GetWidth(), dc.GetTextExtent(_("Use Right")).GetWidth()) + 14;
    int badge = 0;
    for (auto d : {Decision::Accepted, Decision::Rejected, Decision::Partial})
        badge = std::max(badge, dc.GetTextExtent(decision_label(d)).GetWidth());
    reset_w = dc.GetTextExtent(wxString::FromUTF8("↺")).GetWidth() + 16;
    actions_w = std::max(button_w * 2 + 9, badge + reset_w + 20);
    dc.SetFont(small_bold_font);
    label_w = 0;
    for (auto f : view::FieldOrder()) label_w = std::max(label_w, dc.GetTextExtent(field_label(f)).GetWidth());
    label_w += 14;
}

void ComparePanel::Relayout(std::optional<size_t> anchor) {
    // Where the anchor entry is on screen now, in each pane.
    if (!anchor && !layout.empty()) anchor = layout[std::min<size_t>(right->Top(), layout.size() - 1)].entry;
    bool had = !layout.empty() && anchor && *anchor < row_of.size();
    int off_r = had ? row_of[*anchor] - right->Top() : 0;
    int off_l = had ? row_of[*anchor] - left->Top() : 0;

    layout.clear();
    auto cur = CurrentEntry();
    for (auto const& lr : state.Layout(show_all, expanded, 3, filter)) {
        layout.push_back({lr.entry, lr.hidden, std::nullopt});
        // The current difference shows each changed field on a row of its own
        // when several changed, so they can be read and chosen one by one.
        if (lr.IsFold() || !cur || *cur != lr.entry) continue;
        auto fields = ShownFields(lr.entry);
        int count = 0;
        for (auto f : view::FieldOrder()) count += (fields & f) != 0;
        if (count < 2) continue;
        for (auto f : view::FieldOrder())
            if (fields & f) layout.push_back({lr.entry, 0, f});
    }
    // The keyboard's field belongs to the current difference's detail rows.
    if (field_cursor && std::none_of(layout.begin(), layout.end(), [&](Row const& r) { return r.IsDetail() && r.field == field_cursor; }))
        field_cursor.reset();
    row_of.assign(state.Entries().size(), 0);
    for (size_t r = 0; r < layout.size(); ++r) {
        auto const& lr = layout[r];
        if (lr.IsDetail()) continue;
        for (size_t k = 0; k < std::max<size_t>(1, lr.hidden); ++k) row_of[lr.entry + k] = (int)r;
    }
    int top_r = anchor ? row_of[*anchor] - off_r : 0;
    int top_l = anchor ? row_of[*anchor] - off_l : 0;
    right->SetView(top_r, right->XOffset());
    left->SetView(sync ? top_r : top_l, left->XOffset());
    left->UpdateBars(); right->UpdateBars();
    left->Refresh(false); right->Refresh(false); gutter->Refresh(false);
}

void ComparePanel::ApplyFilter() {
    shown = state.VisibleDiffs(filter);
    // A category that no longer has anything to show falls back to all.
    if (filter.category && shown.empty()) {
        filter.category.reset();
        shown = state.VisibleDiffs(filter);
    }
    if (current && !ShownDiff(*current)) {
        auto it = std::lower_bound(shown.begin(), shown.end(), *current);
        if (it == shown.end()) current = shown.empty() ? std::nullopt : std::optional<size_t>(shown.back());
        else current = *it;
    }
    else if (!current && !shown.empty()) current = shown.front();
    Relayout(current);
    if (auto e = CurrentEntry()) { right->Reveal(row_of[*e]); if (sync) left->SetView(right->Top(), right->XOffset()); }
    Changed();
}

std::optional<size_t> ComparePanel::CurrentEntry() const {
    if (!current || !ShownDiff(*current)) return std::nullopt;
    return current;
}

std::optional<size_t> ComparePanel::CurrentIndex() const {
    auto e = CurrentEntry();
    if (!e) return std::nullopt;
    auto it = std::lower_bound(shown.begin(), shown.end(), *e);
    if (it == shown.end() || *it != *e) return std::nullopt;
    return size_t(it - shown.begin());
}

void ComparePanel::SetHot(int row) {
    if (row == hot) return;
    hot = row;
    left->Refresh(false); right->Refresh(false); gutter->Refresh(false);
}

wxColour ComparePanel::RowColour(size_t entry, Side side) const {
    auto const& e = state.Entries()[entry];
    wxColour c = e.change == Change::Modified ? pal.mod_line : e.change == Change::Inserted ? pal.ins_line
        : e.change == Change::Removed ? pal.del_line : pal.bg;
    if (!ShownDiff(entry)) return pal.bg;
    if (!e.Get(side)) c = mix(c, pal.bg, 0.45);
    if (state.GetDecision(entry) != Decision::Pending) c = mix(c, pal.bg, 0.55);
    // the current one a shade deeper in its own colour
    if (IsCurrent(entry)) {
        wxColour mark = e.change == Change::Modified ? pal.mod_mark : e.change == Change::Inserted ? pal.ins_mark : pal.del_mark;
        c = mix(c, mark, 0.18);
    }
    return c;
}

wxColour ComparePanel::EdgeColour(size_t entry) const {
    auto const& e = state.Entries()[entry];
    wxColour c = e.change == Change::Modified ? pal.mod_mark : e.change == Change::Inserted ? pal.ins_mark : pal.del_mark;
    return mix(c, pal.bg, state.GetDecision(entry) == Decision::Pending ? 0.35 : 0.7);
}

wxString ComparePanel::Help(Part part, bool in_gutter, int row) const {
    bool detail = row >= 0 && row < (int)layout.size() && layout[row].IsDetail();
    auto keys = [](char const* k) { return wxString::FromUTF8(k); };
    switch (part) {
        case Part::Accept:
            if (detail) return keys("« ") + fmt_tl("%s: keep the right side", field_label(*layout[row].field)) + keys(" (Ctrl+→)");
            return (in_gutter ? keys("« ") + _("Keep the right side. Click again to reset.")
                : _("Use Right: keep the right side of this difference. Nothing is written until Write to Project.")) + keys(" (Ctrl+→)");
        case Part::Reject:
            if (detail) return keys("» ") + fmt_tl("%s: take the left side", field_label(*layout[row].field)) + keys(" (Ctrl+←)");
            return (in_gutter ? keys("» ") + _("Take the left side. Click again to reset.")
                : _("Use Left: take the left side of this difference. Nothing is written until Write to Project.")) + keys(" (Ctrl+←)");
        case Part::Reset: return wxString::FromUTF8("↺ ") + _("Reset this difference to pending");
        case Part::Check: return _("Select this difference to decide it with the other selected ones (Space)");
        case Part::Overview: return _("Overview of all differences: click or drag to jump");
        case Part::Fold: return _("Click to show the hidden lines");
        default: return {};
    }
}

void ComparePanel::ShowHelp(Part part, bool in_gutter, int row) {
    // What the pointed control does takes the summary's place in the foot
    // for as long as it is pointed at; with detail rows open, the keys that
    // decide them stay there while nothing is.
    auto text = Help(part, in_gutter, row);
    if (text.empty() && !DetailFields().empty()) text = detail_hint;
    bool show = !text.empty();
    if (hint->GetLabelText() == text && hint->IsShown() == show) return;
    hint->SetLabelText(text);
    if (hint->IsShown() != show) {
        hint->Show(show);
        report->Show(!show);
        hint->GetContainingSizer()->Layout();
    }
}

bool ComparePanel::ChunkStart(int row) const {
    // Runs of shown differences of one kind; folds and detail rows break them.
    auto key = [this](int r) {
        auto const& lr = layout[r];
        if (lr.IsFold()) return -2;
        if (lr.IsDetail()) return -3;
        return ShownDiff(lr.entry) ? (int)state.Entries()[lr.entry].change : -1;
    };
    return row <= 0 || key(row - 1) != key(row) || key(row) < 0;
}

bool ComparePanel::ChunkEnd(int row) const {
    return row + 1 >= (int)layout.size() || ChunkStart(row + 1);
}

void ComparePanel::UpdateTitles() {
    // The file's name, and on the open subtitles' side what writing changes.
    auto project_tag = wxString::FromUTF8("  ·  ") + _("target project");
    bool project_right = state.ProjectSide() == Side::Right;
    left_title->SetLabel(project_right ? file_name : project_name + project_tag);
    right_title->SetLabel(project_right ? project_name + project_tag : file_name);
    left_title->SetForegroundColour(project_right ? wxNullColour : pal.accent);
    right_title->SetForegroundColour(project_right ? pal.accent : wxNullColour);
}

void ComparePanel::UpdateFilterBar() {
    std::map<view::Category, int> counts;
    int all = 0;
    for (auto e : state.Diffs()) {
        if (auto category = state.CategoryOf(e, filter.ignored)) { ++counts[*category]; ++all; }
    }
    bool changed = false;
    // Categories with nothing to show are left out. Showing a button shows
    // it whatever its row, so the row is shown or hidden first and the
    // buttons only while it is open.
    if (main_sizer->IsShown(filter_bar) != filters_open) changed = true;
    main_sizer->Show(filter_bar, filters_open);
    for (auto& [category, button] : category_buttons) {
        int n = category ? counts[*category] : all;
        auto label = (category ? category_label(*category) : _("All")) + wxString::Format(" %d", n);
        if (button->GetLabel() != label) { button->SetLabel(label); changed = true; }
        button->SetAccent(filter.category == category);
        bool show = filters_open && (!category || n > 0 || filter.category == category);
        if (button->IsShown() != show) { button->Show(show); changed = true; }
    }
    filter_button->SetAccent(filters_open);
    // While the filters' row is closed, what it sets shows next to its
    // button, each with a click to take it off.
    std::vector<std::pair<wxString, std::function<void()>>> active;
    if (!filters_open) {
        if (filter.category) active.emplace_back(category_label(*filter.category), [this] { filter.category.reset(); });
        for (auto [group, box] : ignore_boxes) {
            if (!(filter.ignored & view::CategoryFields(group))) continue;
            active.emplace_back(fmt_tl("Ignoring %s", group_label(group)), [this, group, box] {
                filter.ignored &= ~view::CategoryFields(group);
                box->SetValue(false);
            });
        }
    }
    wxString key;
    for (auto const& a : active) key += a.first + "\n";
    if (key != chips_key) {
        chips_key = key;
        chips->Clear(true);
        for (auto& [label, clear] : active) {
            auto chip = new FlatButton(this, label + wxString::FromUTF8("  ×"));
            chip->SetToolTip(_("Take this filter off"));
            chip->SetLeastSize(wxSize(-1, FromDIP(26)));
            // Rebuilding the chips destroys this one: after its click is done.
            chip->Bind(wxEVT_BUTTON, [this, clear](wxCommandEvent&) { clear(); CallAfter([this] { ApplyFilter(); }); });
            chips->Add(chip, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(4)));
        }
        changed = true;
    }
    if (changed) Layout();
}

void ComparePanel::Changed() {
    size_t total = shown.size();
    auto index = CurrentIndex();
    // "3 / 14": where the current difference is among those shown.
    counter->SetLabel(index ? wxString::Format("%d / %d", (int)*index + 1, (int)total) : wxString::Format("%d", (int)total));
    wxString where = index ? fmt_tl("Difference %d of %d", (int)*index + 1, (int)total) : fmt_tl("%d differences", (int)total);
    if (filter.Active()) where += fmt_tl(" (%d in total)", (int)state.Diffs().size());
    counter->SetToolTip(where);
    stats->SetLabel(fmt_tl("%d pending · %d left · %d right · %d partial",
        (int)state.Count(Decision::Pending), (int)state.Count(Decision::Rejected),
        (int)state.Count(Decision::Accepted), (int)state.Count(Decision::Partial)));
    stats->SetToolTip(stats->GetLabel());
    size_t checked = 0;
    for (auto e : state.CheckedEntries()) checked += ShownDiff(e);
    checked_count->SetLabel(fmt_tl("%d selected", (int)checked));
    check_all->SetValue(total && checked == total);
    // Buttons for the selected ones while there are any, else for all shown.
    auto foot = check_all->GetContainingSizer();
    if (foot->IsShown(select_group) != (checked > 0) || foot->IsShown(all_group) != (checked == 0)) {
        foot->Show(select_group, checked > 0);
        foot->Show(all_group, checked == 0);
        foot->Layout();
    }
    accept_checked->Enable(checked > 0);
    reject_checked->Enable(checked > 0);
    accept_all->Enable(total > 0); reject_all->Enable(total > 0);
    prev->Enable(total > 0); next->Enable(total > 0);
    accept_current->Enable(CurrentEntry().has_value());
    reject_current->Enable(CurrentEntry().has_value());
    undo->Enable(state.CanUndo()); redo->Enable(state.CanRedo());
    UpdateTitles();
    UpdateFilterBar();

    auto choices = state.Choices();
    if (choices.empty()) {
        report->SetLabelText(state.Count(Decision::Pending)
            ? _("No decision changes the open subtitles yet. Pending differences leave them as they are.")
            : _("The decisions leave the open subtitles as they are."));
        report->SetToolTip(report->GetLabelText());
        report->SetForegroundColour(flat::Colours(this).dim);
        finish->SetLabel(_("&Done"));
        finish->Enable(true);
    }
    else {
        auto r = c->subsController->CheckApply(comparison, choices);
        report->SetLabelText(summarize(r));
        report->SetToolTip(describe(r));
        report->SetForegroundColour(r.Ok() ? flat::Colours(this).text : pal.del_mark);
        finish->SetLabel(fmt_tl("&Write to Project (%d)", (int)choices.size()));
        finish->Enable(r.Ok());
    }
    report->Refresh();
    finish->GetContainingSizer()->Layout();
    // The resting hint follows the current difference (keys for its detail rows).
    if (!field_cursor && hot < 0) ShowHelp(Part::None, false, -1);
    left->Refresh(false); right->Refresh(false); gutter->Refresh(false);
}

void ComparePanel::Go(int delta) {
    size_t total = shown.size();
    if (!total) return;
    auto index = CurrentIndex();
    size_t i;
    if (!index) {
        // Start from the view: the first difference at or below the top row.
        size_t top_entry = layout.empty() ? 0 : layout[std::min<size_t>(right->Top(), layout.size() - 1)].entry;
        auto it = std::lower_bound(shown.begin(), shown.end(), top_entry);
        size_t at = it == shown.end() ? total - 1 : size_t(it - shown.begin());
        i = delta > 0 ? at : (at == 0 ? total - 1 : at - 1);
    }
    else i = (*index + total + (delta > 0 ? 1 : total - 1)) % total; // wraps around
    current = shown[i];
    Relayout();
    int row = row_of[*current], last = row;
    while (!GroupEnd(last)) ++last;
    right->Reveal(row, last); left->Reveal(row, last);
    if (sync) left->SetView(right->Top(), right->XOffset());
    Changed();
}

void ComparePanel::Select(size_t entry, Side from) {
    if (ShownDiff(entry) && current != entry) {
        current = entry;
        Relayout(entry); // the clicked line stays where it is; its details open below it
        int row = row_of[entry], last = row;
        while (!GroupEnd(last)) ++last;
        PaneFor(from)->Reveal(row, last);
        if (sync) PaneFor(view::Other(from))->SetView(PaneFor(from)->Top(), PaneFor(from)->XOffset());
    }
    // Locate the line on the other side too (the only way they line up when not syncing).
    auto other = PaneFor(view::Other(from));
    auto here = PaneFor(from);
    int row = row_of[entry];
    // Put it at the same height as on the clicked side.
    if (!sync && (row < other->Top() || row >= other->Top() + other->VisibleRows()))
        other->SetView(here->Top(), other->XOffset());
    Changed();
}

void ComparePanel::Decide(size_t entry, Decision d) {
    if (ShownDiff(entry) && current != entry) {
        current = entry;
        Relayout(entry);
    }
    state.Decide({entry}, d, Scope());
    Changed();
}

void ComparePanel::SetField(size_t entry, model::Field field, Side winner) {
    if (state.SetField(entry, field, winner)) Changed();
}

std::vector<model::Field> ComparePanel::DetailFields() const {
    std::vector<model::Field> out;
    auto cur = CurrentEntry();
    if (!cur) return out;
    for (auto const& lr : layout)
        if (lr.IsDetail() && lr.entry == *cur) out.push_back(*lr.field);
    return out;
}

void ComparePanel::SetFieldCursor(std::optional<model::Field> field) {
    if (field) {
        auto fields = DetailFields();
        if (std::find(fields.begin(), fields.end(), *field) == fields.end()) field.reset();
    }
    if (field == field_cursor) return;
    field_cursor = field;
    if (field) {
        for (int r = 0; r < (int)layout.size(); ++r)
            if (layout[r].IsDetail() && layout[r].field == field) {
                right->Reveal(r); left->Reveal(r);
                if (sync) left->SetView(right->Top(), right->XOffset());
            }
        hint->SetForegroundColour(pal.accent);
        hint->SetLabelText(fmt_tl("%s: Ctrl+Left takes the left side, Ctrl+Right keeps the right side, Esc leaves the field", field_label(*field)));
    }
    else ShowHelp(Part::None, false, -1);
    left->Refresh(false); right->Refresh(false); gutter->Refresh(false);
}

void ComparePanel::MoveFieldCursor(int delta) {
    auto fields = DetailFields();
    if (fields.empty()) return;
    auto it = field_cursor ? std::find(fields.begin(), fields.end(), *field_cursor) : fields.end();
    int i;
    if (it == fields.end()) i = delta > 0 ? 0 : (int)fields.size() - 1;
    else i = std::max(0, std::min((int)fields.size() - 1, int(it - fields.begin()) + delta));
    SetFieldCursor(fields[i]);
}

void ComparePanel::ChooseSide(Side side) {
    auto cur = CurrentEntry();
    if (!cur) return;
    if (field_cursor) SetField(*cur, *field_cursor, side);
    else Decide(*cur, side == Side::Right ? Decision::Accepted : Decision::Rejected);
}

void ComparePanel::ToggleCheck(size_t entry) {
    state.Check(entry, !state.Checked(entry));
    Changed();
}

void ComparePanel::ExpandFold(int row) {
    if (row < 0 || row >= (int)layout.size() || !layout[row].IsFold()) return;
    size_t entry = layout[row].entry;
    expanded.insert(entry);
    // The hidden lines open in place: the fold's first line takes its row.
    Relayout(entry);
    Changed();
}

void ComparePanel::Scrolled(Side from) {
    if (!sync) return;
    auto src = PaneFor(from);
    PaneFor(view::Other(from))->SetView(src->Top(), src->XOffset());
}

bool ComparePanel::ConfirmBatch(std::vector<size_t> const& entries, Decision d, bool all) {
    size_t changing = state.Changing(entries, d, Scope()), decided = 0;
    for (auto e : entries) decided += state.GetDecision(e) != Decision::Pending && state.GetDecision(e) != d;
    if (!changing) {
        wxMessageBox(d == Decision::Accepted ? _("These differences already use the right side.") : _("These differences already use the left side."),
            _("Compare Subtitles"), wxOK | wxICON_INFORMATION, this);
        return false;
    }
    wxString scope = !all ? fmt_tl("the %d selected differences", (int)entries.size())
        : filter.Active() ? fmt_tl("the %d differences shown", (int)entries.size())
        : fmt_tl("all %d differences", (int)entries.size());
    wxString action = d == Decision::Accepted ? _("Use the right side") : _("Use the left side");
    wxString message = fmt_tl("%s: %s?", action, scope);
    message += "\n" + fmt_tl("%d of them will change.", (int)changing);
    if (decided) message += "\n" + fmt_tl("%d already decided the other way will be overwritten.", (int)decided);
    if (filter.ignored) {
        wxString names;
        for (auto f : view::FieldOrder())
            if (filter.ignored & f) names += (names.empty() ? "" : ", ") + field_label(f);
        message += "\n" + fmt_tl("Ignored fields keep their current choice: %s", names);
    }
    message += "\n\n" + _("This can be undone in this window. The open subtitles change only when you write to the project.");
    return wxMessageBox(message, _("Compare Subtitles"), wxOK | wxCANCEL | wxICON_QUESTION, this) == wxOK;
}

void ComparePanel::Batch(std::vector<size_t> const& entries, Decision d, bool all) {
    if (entries.empty() || !ConfirmBatch(entries, d, all)) return;
    state.Decide(entries, d, Scope());
    Changed();
}

void ComparePanel::ContextMenu(DiffPane* pane, size_t entry, wxPoint pos) {
    auto const& e = state.Entries()[entry];
    wxMenu menu;
    auto d = state.GetDecision(entry);
    menu.Append(ID_MENU_REJECT, _("Use Left"))->Enable(d != Decision::Rejected);
    menu.Append(ID_MENU_ACCEPT, _("Use Right"))->Enable(d != Decision::Accepted);
    menu.Append(ID_MENU_RESET, _("Reset to Pending"))->Enable(d != Decision::Pending);
    menu.AppendSeparator();
    menu.AppendCheckItem(ID_MENU_CHECK, _("Selected"))->Check(state.Checked(entry));
    if (e.change == Change::Modified) {
        menu.AppendSeparator();
        auto fields = ShownFields(entry);
        for (auto f : view::FieldOrder()) {
            if (!(fields & f)) continue;
            auto item = menu.AppendCheckItem(ID_MENU_FIELD + (int)std::distance(view::FieldOrder().begin(),
                std::find(view::FieldOrder().begin(), view::FieldOrder().end(), f)),
                fmt_tl("Keep right side: %s", field_label(f)));
            auto winner = state.Winner(entry, f);
            item->Check(winner && *winner == Side::Right);
        }
    }
    int id = pane->GetPopupMenuSelectionFromUser(menu, pos);
    if (id == ID_MENU_ACCEPT) Decide(entry, Decision::Accepted);
    else if (id == ID_MENU_REJECT) Decide(entry, Decision::Rejected);
    else if (id == ID_MENU_RESET) Decide(entry, Decision::Pending);
    else if (id == ID_MENU_CHECK) ToggleCheck(entry);
    else if (id >= ID_MENU_FIELD && id < ID_MENU_FIELD + (int)view::FieldOrder().size()) {
        auto f = view::FieldOrder()[id - ID_MENU_FIELD];
        auto winner = state.Winner(entry, f);
        SetField(entry, f, winner && *winner == Side::Right ? Side::Left : Side::Right);
    }
}

wxString ComparePanel::Tooltip(int row, Side side) const {
    if (row < 0 || row >= (int)layout.size() || layout[row].IsFold()) return {};
    size_t entry = layout[row].entry;
    auto const& e = state.Entries()[entry];
    auto const& line = e.Get(side);
    if (layout[row].IsDetail()) {
        auto f = *layout[row].field;
        auto value = [&](Side s) { return e.Get(s) ? one_line(e.Get(s)->Value(f), 600) : wxString(); };
        return fmt_tl("%s\nLeft: %s\nRight: %s", field_label(f), value(Side::Left), value(Side::Right));
    }
    wxString out;
    if (!line) out = side == Side::Left ? _("Not in the left version (inserted on the right)") : _("Not in the right version (removed on the right)");
    else {
        out = fmt_tl("Line %d: %s - %s, %s", (int)line->number, to_wx(line->Value(model::Start)),
            to_wx(line->Value(model::End)), to_wx(line->Value(model::Style)));
        if (!line->Value(model::Actor).empty()) out += ", " + to_wx(line->Value(model::Actor));
        out += "\n" + one_line(line->Value(model::Text), 600);
        auto fields = ShownFields(entry);
        if (fields) {
            wxString names;
            for (auto f : view::FieldOrder())
                if (fields & f) names += (names.empty() ? "" : ", ") + field_label(f);
            out += "\n" + fmt_tl("Changed: %s", names);
        }
    }
    if (ShownDiff(entry)) out += "\n" + fmt_tl("Status: %s", decision_label(state.GetDecision(entry)));
    if (e.ambiguity != model::Ambiguity::None) out += "\n" + fmt_tl("Match note: %s", ambiguity_label(e.ambiguity));
    return out;
}

void ComparePanel::OnFinish() {
    auto choices = state.Choices();
    if (choices.empty()) {
        End(false);
        return;
    }
    subtitle_apply::Report r;
    try {
        r = c->subsController->ApplyComparison(comparison, choices);
    }
    catch (SubtitleApplyRejected const& e) {
        wxMessageBox(to_wx(e.what()), _("Compare Subtitles"), wxOK | wxICON_ERROR, this);
        End(false);
        return;
    }
    if (!r.Ok()) {
        report->SetLabelText(summarize(r));
        report->SetToolTip(describe(r));
        report->SetForegroundColour(pal.del_mark);
        finish->Enable(false);
        return;
    }
    End(true);
}

void ComparePanel::End(bool applied) {
    comparison.End();
    if (c->workspaceTabs) c->workspaceTabs->ComparisonEnded(applied);
}

bool ComparePanel::TryClose(bool ask) {
    if (ask && !state.Choices().empty()) {
        auto message = fmt_tl("Close the comparison and discard %d decisions that would change the open subtitles?", (int)state.Choices().size());
        if (wxMessageBox(message, _("Compare Subtitles"), wxOK | wxCANCEL | wxICON_QUESTION, this) != wxOK) return false;
    }
    End(false);
    return true;
}
}

bool OpenCompareTab(agi::Context *c, agi::fs::path const& path) {
    auto title = _("Compare Subtitles");
    auto tabs = c->workspaceTabs;
    if (!tabs) return false;
    if (tabs->Comparison()) {
        tabs->ShowComparison();
        wxMessageBox(_("A comparison is already open. End it before comparing with another file."), title, wxOK | wxICON_INFORMATION, c->parent);
        return false;
    }
    std::string encoding;
    try {
        encoding = CharSetDetect::GetEncoding(path); // asks when detection is ambiguous
    }
    catch (agi::UserCancelException const&) { return false; }
    catch (agi::Exception const& e) {
        wxMessageBox(to_wx(e.GetMessage()), title, wxOK | wxICON_ERROR, c->parent);
        return false;
    }
    SubtitleFormat const* reader = nullptr;
    try {
        reader = SubtitleFormat::GetReader(path, encoding.c_str());
    }
    catch (agi::Exception const&) {
        wxMessageBox(_("This file is not a supported subtitle format."), title, wxOK | wxICON_ERROR, c->parent);
        return false;
    }
    subtitle_compare::Baseline baseline;
    {
        wxBusyCursor busy;
        // Readers that need a choice (TXT import, MicroDVD frame rate) ask here, on the host thread.
        baseline = subtitle_compare::ReadBaseline(*reader, path, c->project->Timecodes(), encoding);
    }
    if (baseline.state == subtitle_compare::LoadState::Cancelled) return false;
    if (baseline.state != subtitle_compare::LoadState::Completed) {
        wxMessageBox(fmt_tl("The file could not be read:\n%s", baseline.error), title, wxOK | wxICON_ERROR, c->parent);
        return false;
    }
    std::optional<SubtitleComparison> comparison;
    try {
        wxBusyCursor busy;
        comparison.emplace(c->subsController->BeginComparison(std::move(baseline.file), baseline.fields));
    }
    catch (SubtitleApplyRejected const& e) {
        wxMessageBox(to_wx(e.what()), title, wxOK | wxICON_INFORMATION, c->parent);
        return false;
    }
    bool differs = false;
    for (auto const& item : comparison->Items())
        differs |= item.kind == model::Kind::Added || item.kind == model::Kind::Deleted
            || (item.kind == model::Kind::Modified && (item.changed & comparison->Fields()));
    if (!differs) {
        wxMessageBox(_("The files have no differences that can be applied."), title, wxOK | wxICON_INFORMATION, c->parent);
        return false;
    }
    tabs->OpenComparison(new ComparePanel(tabs->Book(), c, std::move(*comparison), path.filename().wstring()), path.filename().wstring());
    return true;
}
