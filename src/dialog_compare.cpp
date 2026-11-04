// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// C2-C diff editor: compare the open subtitles with a subtitle file side by
// side, decide on each difference, and write the decisions to the project as
// one undoable change (docs/subtitle-compare-ui.md). Modal: while it is open
// the comparison session freezes every other edit anyway, and nothing reaches
// the project until "Write to Project".
#include "dialogs.h"

#include "charset_detect.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "project.h"
#include "subs_controller.h"
#include "subtitle_compare.h"
#include "subtitle_compare_view.h"
#include "subtitle_format.h"

#include <libaegisub/charset.h>
#include <libaegisub/exception.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/renderer.h>
#include <wx/scrolbar.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>

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
        case Decision::Accepted: return _("Accepted");
        case Decision::Rejected: return _("Rejected");
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

// ---- colours ----
struct Palette {
    wxColour bg, text, dim, gutter, gutter_text, border, accent;
    wxColour ins_line, ins_char, del_line, del_char, mod_mark;
    wxColour filler, hatch, fold_bg, fold_text, hover;
    wxColour accepted, rejected, partial;
};

wxColour mix(wxColour const& a, wxColour const& b, double t) {
    auto m = [t](unsigned char x, unsigned char y) { return (unsigned char)(x + (y - x) * t + 0.5); };
    return wxColour(m(a.Red(), b.Red()), m(a.Green(), b.Green()), m(a.Blue(), b.Blue()));
}

Palette make_palette() {
    if (wxSystemSettings::GetAppearance().IsDark()) return {
        "#1e1e1e", "#d4d4d4", "#6e7681", "#252526", "#858585", "#3c3c3c", "#4daafc",
        "#1f3a28", "#2f6b40", "#3d1f22", "#7d2f35", "#3b82c4",
        "#232323", "#343434", "#1c2b3a", "#6cb6ff", "#2a2d2e",
        "#3fb950", "#f85149", "#d29922"};
    return {
        "#ffffff", "#1f2328", "#8c959f", "#f6f8fa", "#6e7781", "#d0d7de", "#0969da",
        "#e6ffec", "#abf2bc", "#ffebe9", "#ffc1bf", "#54aeff",
        "#f6f8fa", "#d8dee4", "#ddf4ff", "#0969da", "#f3f4f6",
        "#1a7f37", "#cf222e", "#9a6700"};
}

enum class Part { None, Content, Check, Accept, Reject, Reset, Fold, Overview };

class DialogCompare;

// One side of the aligned view. Both panes show the same layout rows; the
// right pane also carries the check boxes, the per-difference actions and the
// overview ruler.
class DiffPane final : public wxWindow {
    DialogCompare& ed;
    Side side;
    wxScrollBar *vbar, *hbar;
    int top = 0, xoff = 0;
    int hover_row = -1;
    Part hover_part = Part::None;
    int tip_row = -1;
    bool dragging_overview = false;

    struct Geometry {
        int width, height;       // drawing area (without scroll bars)
        int check_x, number_x, marker_x, content_x, content_right, actions_x, overview_x;
    };
    Geometry Geo() const;
    int RowAt(int y) const;
    Part PartAt(int row, wxPoint pt) const;
    wxRect ButtonRect(Geometry const& g, int y, Part part) const;

    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouse(wxMouseEvent& event);
    void OnKey(wxKeyEvent& event);
    void OnScroll(wxScrollEvent&);
    void DrawRow(wxDC& dc, Geometry const& g, int row, int y);
    void DrawLine(wxDC& dc, Geometry const& g, size_t entry, int y);
    void DrawActions(wxDC& dc, Geometry const& g, size_t entry, int row, int y);
    void DrawOverview(wxDC& dc, Geometry const& g);
public:
    DiffPane(wxWindow* parent, DialogCompare& ed, Side side);
    int Top() const { return top; }
    int XOffset() const { return xoff; }
    int VisibleRows() const;
    /// Clamp and apply a scroll position; @p notify tells the editor (for sync).
    void SetView(int new_top, int new_x, bool notify = false);
    /// Scroll so that @p row is visible, centring it if it was not.
    void Reveal(int row);
    void UpdateBars();
};

class DialogCompare final : public wxDialog {
    friend class DiffPane;
    agi::Context *c;
    SubtitleComparison& comparison;
    view::State state;
    Palette pal;
    wxString file_name, project_name;

    // layout
    bool show_all = false, sync = true;
    std::set<size_t> expanded;
    std::vector<view::LayoutRow> layout;
    std::vector<int> row_of;            // entry -> layout row (a fold row for hidden entries)
    std::optional<size_t> current;      // index into state.Diffs()

    // metrics
    int row_h = 22, char_w = 7, number_w = 40, marker_w = 16, check_w = 24, time_w = 80, style_w = 80, actor_w = 0;
    int actions_w = 120, overview_w = 12, content_w = 400, button_w = 54, reset_w = 30;
    wxFont small_font;

    DiffPane *left, *right;
    wxStaticText *left_title, *right_title, *counter, *stats, *checked_count;
    wxButton *prev, *next, *accept_current, *reject_current, *undo, *redo;
    wxButton *accept_checked, *reject_checked, *accept_all, *reject_all, *finish;
    wxCheckBox *check_all, *sync_box;
    wxRadioButton *diffs_only, *everything;
    wxPanel *fields_panel;
    std::vector<std::pair<model::Field, wxCheckBox*>> field_boxes;
    std::optional<size_t> fields_entry;
    wxTextCtrl *report;

    void Measure();
    void RebuildLayout();
    void Changed();
    void UpdateTitles();
    void UpdateFields();
    std::optional<size_t> CurrentEntry() const;
    void Go(int delta);
    bool ConfirmBatch(std::vector<size_t> const& entries, Decision d, bool all);
    void Batch(std::vector<size_t> const& entries, Decision d, bool all);
    void OnFinish();
    void OnCancel();
public:
    DialogCompare(agi::Context *c, SubtitleComparison& comparison, wxString const& name);

    // used by the panes
    view::State const& State() const { return state; }
    Palette const& Colours() const { return pal; }
    std::vector<view::LayoutRow> const& Layout() const { return layout; }
    std::optional<size_t> Current() const { return CurrentEntry(); }
    DiffPane* PaneFor(Side s) const { return s == Side::Left ? left : right; }
    void Select(size_t entry, Side from);
    void Decide(size_t entry, Decision d);
    void ToggleCheck(size_t entry);
    void ExpandFold(int row);
    void Scrolled(Side from);
    void ContextMenu(DiffPane* pane, size_t entry, wxPoint pos);
    wxString Tooltip(size_t entry, Side side) const;
};

// ---- DiffPane ----
DiffPane::DiffPane(wxWindow* parent, DialogCompare& ed, Side side)
: wxWindow(parent, -1, wxDefaultPosition, wxSize(300, 200), wxWANTS_CHARS | wxBORDER_THEME)
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
    bool r = side == Side::Right;
    g.check_x = 0;
    g.number_x = r ? ed.check_w : 0;
    g.marker_x = g.number_x + ed.number_w;
    g.content_x = g.marker_x + ed.marker_w;
    g.overview_x = r ? g.width - ed.overview_w : g.width;
    g.actions_x = r ? g.overview_x - ed.actions_w : g.width;
    g.content_right = std::max(g.content_x, g.actions_x);
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
        Refresh(false);
        if (notify) ed.Scrolled(side);
    }
}

void DiffPane::Reveal(int row) {
    if (row < 0) return;
    int vis = VisibleRows();
    if (row >= top && row < top + vis) return;
    SetView(row - vis / 3, xoff);
}

void DiffPane::OnSize(wxSizeEvent&) {
    auto size = GetClientSize();
    int vw = vbar->GetBestSize().GetWidth(), hh = hbar->GetBestSize().GetHeight();
    vbar->SetSize(size.GetWidth() - vw, 0, vw, std::max(0, size.GetHeight() - hh));
    hbar->SetSize(0, size.GetHeight() - hh, std::max(0, size.GetWidth() - vw), hh);
    UpdateBars();
    Refresh(false);
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

wxRect DiffPane::ButtonRect(Geometry const& g, int y, Part part) const {
    int h = ed.row_h - 6, x = g.actions_x + 4;
    switch (part) {
        case Part::Accept: return {x, y + 3, ed.button_w, h};
        case Part::Reject: return {x + ed.button_w + 4, y + 3, ed.button_w, h};
        case Part::Reset: return {g.actions_x + ed.actions_w - ed.reset_w - 4, y + 3, ed.reset_w, h};
        default: return {};
    }
}

Part DiffPane::PartAt(int row, wxPoint pt) const {
    auto g = Geo();
    if (side == Side::Right && pt.x >= g.overview_x && pt.x < g.width) return Part::Overview;
    if (row < 0) return Part::None;
    auto const& lr = ed.layout[row];
    if (lr.IsFold()) return Part::Fold;
    auto const& e = ed.state.Entries()[lr.entry];
    if (side == Side::Right && e.IsDiff()) {
        if (pt.x < g.number_x) return Part::Check;
        int y = (row - top) * ed.row_h;
        bool pending = ed.state.GetDecision(lr.entry) == Decision::Pending;
        for (auto part : pending ? std::initializer_list<Part>{Part::Accept, Part::Reject} : std::initializer_list<Part>{Part::Reset})
            if (ButtonRect(g, y, part).Contains(pt)) return part;
    }
    return Part::Content;
}

void DiffPane::OnMouse(wxMouseEvent& event) {
    auto pt = event.GetPosition();
    auto g = Geo();
    if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
        int lines = event.GetWheelRotation() / std::max(1, event.GetWheelDelta()) * std::max(1, event.GetLinesPerAction());
        if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL || event.ShiftDown()) SetView(top, xoff + lines * ed.char_w * 4, true);
        else SetView(top - lines, xoff, true);
        return;
    }
    if (event.GetEventType() == wxEVT_LEAVE_WINDOW) {
        if (hover_row != -1 || hover_part != Part::None) { hover_row = -1; hover_part = Part::None; Refresh(false); }
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
        if (row != hover_row || part != hover_part) {
            hover_row = row; hover_part = part;
            Refresh(false);
        }
        if (row != tip_row) {
            tip_row = row;
            if (row >= 0 && !ed.layout[row].IsFold()) SetToolTip(ed.Tooltip(ed.layout[row].entry, side));
            else UnsetToolTip();
        }
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
            case Part::Check: ed.ToggleCheck(lr.entry); return;
            case Part::Accept: ed.Decide(lr.entry, Decision::Accepted); return;
            case Part::Reject: ed.Decide(lr.entry, Decision::Rejected); return;
            case Part::Reset: ed.Decide(lr.entry, Decision::Pending); return;
            default: ed.Select(lr.entry, side); return;
        }
    }
    if (event.GetEventType() == wxEVT_RIGHT_DOWN) {
        SetFocus();
        if (row < 0 || ed.layout[row].IsFold()) return;
        auto entry = ed.layout[row].entry;
        if (!ed.state.Entries()[entry].IsDiff()) return;
        ed.Select(entry, side);
        ed.ContextMenu(this, entry, pt);
    }
}

void DiffPane::OnKey(wxKeyEvent& event) {
    int vis = VisibleRows();
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
            if (auto entry = ed.Current()) ed.ToggleCheck(*entry);
            break;
        default: event.Skip();
    }
}

void DiffPane::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    auto const& p = ed.pal;
    auto g = Geo();
    dc.SetBackground(wxBrush(p.bg));
    dc.Clear();
    dc.SetFont(GetFont());
    int rows = (int)ed.layout.size();
    for (int row = top, y = 0; row < rows && y < g.height; ++row, y += ed.row_h)
        DrawRow(dc, g, row, y);
    // gutter separator
    dc.SetPen(wxPen(p.border));
    dc.DrawLine(g.content_x - 1, 0, g.content_x - 1, g.height);
    if (side == Side::Right) {
        dc.DrawLine(g.actions_x, 0, g.actions_x, g.height);
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
    if (lr.IsFold()) {
        bool hot = row == hover_row;
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(hot ? mix(p.fold_bg, p.fold_text, 0.12) : p.fold_bg));
        dc.DrawRectangle(0, y, g.width, ed.row_h);
        dc.SetTextForeground(p.fold_text);
        auto label = wxString::FromUTF8("⋯  ") + fmt_tl("%d hidden lines", (int)lr.hidden) + wxString::FromUTF8("  ·  ") + _("click to expand");
        auto extent = dc.GetTextExtent(label);
        dc.DrawText(label, g.content_x + 8, y + (ed.row_h - extent.GetHeight()) / 2);
        return;
    }
    size_t entry = lr.entry;
    auto const& e = ed.state.Entries()[entry];
    auto const& line = e.Get(side);
    auto current = ed.CurrentEntry();
    bool is_current = current && *current == entry;
    Decision d = ed.state.GetDecision(entry);

    // background
    wxColour bg = p.bg;
    if (e.IsDiff() && line) {
        bg = side == Side::Right ? p.ins_line : p.del_line;
        if (d != Decision::Pending) bg = mix(bg, p.bg, 0.55);
    }
    else if (row == hover_row) bg = p.hover;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(bg));
    dc.DrawRectangle(0, y, g.width, ed.row_h);
    if (!line) {
        dc.SetBrush(wxBrush(p.filler));
        dc.DrawRectangle(g.content_x, y, g.content_right - g.content_x, ed.row_h);
        dc.SetBrush(wxBrush(p.hatch, wxBRUSHSTYLE_BDIAGONAL_HATCH));
        dc.DrawRectangle(g.content_x, y, g.content_right - g.content_x, ed.row_h);
    }
    // gutter
    dc.SetBrush(wxBrush(e.IsDiff() && line ? mix(bg, p.gutter, 0.35) : p.gutter));
    dc.DrawRectangle(0, y, g.content_x - 1, ed.row_h);
    int text_y = y + (ed.row_h - dc.GetCharHeight()) / 2;
    if (line) {
        dc.SetTextForeground(is_current ? p.accent : p.gutter_text);
        auto number = wxString::Format("%zu", line->number);
        dc.DrawText(number, g.marker_x - 4 - dc.GetTextExtent(number).GetWidth(), text_y);
        if (e.IsDiff()) {
            wxString mark = e.change == Change::Modified ? "~" : side == Side::Right ? "+" : wxString::FromUTF8("−");
            // Plain inserts and removals carry NoReliableMatch; only real doubts get a mark.
            if (e.ambiguity == model::Ambiguity::Crossing) mark = wxString::FromUTF8("⇅");
            else if (e.ambiguity == model::Ambiguity::Duplicate || e.ambiguity == model::Ambiguity::ConflictingEvidence) mark = "?";
            dc.SetTextForeground(side == Side::Right ? p.accepted : p.rejected);
            dc.DrawText(mark, g.marker_x + 3, text_y);
        }
    }
    if (side == Side::Right && e.IsDiff()) {
        int s = std::min(16, ed.row_h - 4);
        wxRendererNative::Get().DrawCheckBox(this, dc, wxRect((ed.check_w - s) / 2, y + (ed.row_h - s) / 2, s, s),
            ed.state.Checked(entry) ? wxCONTROL_CHECKED : 0);
    }
    if (line) DrawLine(dc, g, entry, y);
    if (side == Side::Right) DrawActions(dc, g, entry, row, y);
    if (is_current) {
        dc.SetPen(wxPen(p.accent));
        dc.DrawLine(0, y, g.width, y);
        dc.DrawLine(0, y + ed.row_h - 1, g.width, y + ed.row_h - 1);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(p.accent));
        dc.DrawRectangle(side == Side::Right ? ed.check_w - 3 : 0, y, 3, ed.row_h);
    }
}

void DiffPane::DrawLine(wxDC& dc, Geometry const& g, size_t entry, int y) {
    auto const& p = ed.pal;
    auto const& e = ed.state.Entries()[entry];
    auto const& line = *e.Get(side);
    wxDCClipper clip(dc, wxRect(g.content_x, y, std::max(0, g.content_right - g.content_x), ed.row_h));
    int x = g.content_x + 6 - xoff;
    int text_y = y + (ed.row_h - dc.GetCharHeight()) / 2;
    wxColour hl = side == Side::Right ? p.ins_char : p.del_char;
    bool modified = e.change == Change::Modified;
    // A losing field (or a whole losing line) is dimmed and struck through.
    auto lost = [&](model::Field f) {
        auto winner = ed.state.Winner(entry, f);
        return winner && *winner != side && (!modified || (e.changed & f));
    };
    auto strike = [&](int x0, int x1) {
        dc.SetPen(wxPen(p.dim));
        dc.DrawLine(x0, y + ed.row_h / 2, x1, y + ed.row_h / 2);
    };
    auto cell = [&](model::Field f, int width) {
        auto value = one_line(line.Value(f), 200);
        bool changed = modified && (e.changed & f);
        if (changed) {
            auto w = std::min(width - 6, dc.GetTextExtent(value).GetWidth() + 6);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(lost(f) ? mix(hl, p.bg, 0.5) : hl));
            dc.DrawRoundedRectangle(x - 3, y + 3, std::max(w, 8), ed.row_h - 6, 2);
        }
        {
            wxDCClipper inner(dc, wxRect(x, y, std::max(0, width - 8), ed.row_h));
            dc.SetTextForeground(lost(f) ? p.dim : f == model::Text || changed ? p.text : p.gutter_text);
            dc.DrawText(value, x, text_y);
            if (lost(f) && !value.empty()) strike(x, x + std::min(width - 8, dc.GetTextExtent(value).GetWidth()));
        }
        x += width;
    };
    cell(model::Start, ed.time_w);
    cell(model::End, ed.time_w);
    cell(model::Style, ed.style_w);
    if (ed.actor_w) cell(model::Actor, ed.actor_w);

    // Text, with the changed tokens highlighted.
    auto const& text = line.Value(model::Text);
    auto const& spans = side == Side::Left ? e.text.left : e.text.right;
    bool text_lost = lost(model::Text);
    int text_x = x;
    size_t pos = 0;
    auto draw = [&](size_t a, size_t b, bool highlight) {
        if (a >= b) return;
        auto part = to_wx(text.substr(a, b - a));
        part.Replace("\n", " ");
        int w = dc.GetTextExtent(part).GetWidth();
        if (highlight) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(text_lost ? mix(hl, p.bg, 0.5) : hl));
            dc.DrawRectangle(x, y + 3, std::max(w, 3), ed.row_h - 6);
        }
        dc.SetTextForeground(text_lost ? p.dim : p.text);
        dc.DrawText(part, x, text_y);
        x += w;
    };
    if (modified && (e.changed & model::Text)) {
        for (auto const& span : spans) {
            draw(pos, std::min(span.begin, text.size()), false);
            draw(std::min(span.begin, text.size()), std::min(span.end, text.size()), true);
            pos = std::min(span.end, text.size());
        }
    }
    draw(pos, std::min(text.size(), size_t(4000)), false);
    if (text_lost && x > text_x) strike(text_x, x);

    // Other changed fields as chips, and the comment flag for context.
    dc.SetFont(ed.small_font);
    int chip_y = y + 4, chip_h = ed.row_h - 8;
    auto chip = [&](wxString const& label, wxColour const& back, wxColour const& fore, bool struck) {
        auto w = dc.GetTextExtent(label).GetWidth() + 10;
        x += 8;
        dc.SetPen(wxPen(mix(back, fore, 0.25)));
        dc.SetBrush(wxBrush(back));
        dc.DrawRoundedRectangle(x, chip_y, w, chip_h, 3);
        dc.SetTextForeground(fore);
        dc.DrawText(label, x + 5, chip_y + (chip_h - dc.GetCharHeight()) / 2);
        if (struck) strike(x + 3, x + w - 3);
        x += w;
    };
    for (auto f : view::FieldOrder()) {
        if (is_column(f) || !modified || !(e.changed & f)) continue;
        wxString label = field_label(f);
        if (f == model::Comment) label = line.comment ? _("Comment") : _("Not a comment");
        else label += ": " + one_line(line.Value(f), 60);
        chip(label, lost(f) ? mix(hl, p.bg, 0.5) : hl, lost(f) ? p.dim : p.text, lost(f));
    }
    if (line.comment && !(modified && (e.changed & model::Comment)))
        chip(_("Comment"), p.gutter, p.gutter_text, false);
    dc.SetFont(GetFont());
}

void DiffPane::DrawActions(wxDC& dc, Geometry const& g, size_t entry, int row, int y) {
    auto const& p = ed.pal;
    auto const& e = ed.state.Entries()[entry];
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(p.bg));
    dc.DrawRectangle(g.actions_x + 1, y, ed.actions_w - 1, ed.row_h);
    if (!e.IsDiff()) return;
    Decision d = ed.state.GetDecision(entry);
    dc.SetFont(ed.small_font);
    auto button = [&](Part part, wxString const& label, wxColour const& colour) {
        auto r = ButtonRect(g, y, part);
        bool hot = row == hover_row && hover_part == part;
        dc.SetPen(wxPen(colour));
        dc.SetBrush(wxBrush(hot ? colour : mix(colour, p.bg, 0.9)));
        dc.DrawRoundedRectangle(r, 3);
        dc.SetTextForeground(hot ? *wxWHITE : colour);
        auto extent = dc.GetTextExtent(label);
        dc.DrawText(label, r.x + (r.width - extent.GetWidth()) / 2, r.y + (r.height - extent.GetHeight()) / 2);
    };
    if (d == Decision::Pending) {
        button(Part::Accept, wxString::FromUTF8("✓ ") + _("Accept"), p.accepted);
        button(Part::Reject, wxString::FromUTF8("✕ ") + _("Reject"), p.rejected);
    }
    else {
        wxColour colour = d == Decision::Accepted ? p.accepted : d == Decision::Rejected ? p.rejected : p.partial;
        wxString label = (d == Decision::Accepted ? wxString::FromUTF8("✓ ") : d == Decision::Rejected ? wxString::FromUTF8("✕ ") : wxString::FromUTF8("◐ ")) + decision_label(d);
        dc.SetTextForeground(colour);
        auto extent = dc.GetTextExtent(label);
        dc.DrawText(label, g.actions_x + 8, y + (ed.row_h - extent.GetHeight()) / 2);
        button(Part::Reset, wxString::FromUTF8("↺"), p.gutter_text);
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
        if (lr.IsFold()) continue;
        auto const& e = ed.state.Entries()[lr.entry];
        if (!e.IsDiff()) continue;
        wxColour c = e.change == Change::Modified ? p.mod_mark : e.change == Change::Inserted ? p.accepted : p.rejected;
        if (ed.state.GetDecision(lr.entry) != Decision::Pending) c = mix(c, p.gutter, 0.6);
        dc.SetBrush(wxBrush(c));
        dc.DrawRectangle(g.overview_x + 2, int(r * scale), ed.overview_w - 4, mark_h);
    }
    dc.SetPen(wxPen(p.gutter_text));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(g.overview_x, int(top * scale), ed.overview_w, std::max(4, int(VisibleRows() * scale)));
}

// ---- DialogCompare ----
enum {
    ID_PREV = wxID_HIGHEST + 1, ID_NEXT, ID_ACCEPT_CURRENT, ID_REJECT_CURRENT, ID_UNDO, ID_REDO, ID_REDO_ALT,
    ID_MENU_ACCEPT, ID_MENU_REJECT, ID_MENU_RESET, ID_MENU_CHECK, ID_MENU_FIELD,
};

DialogCompare::DialogCompare(agi::Context *c, SubtitleComparison& comparison, wxString const& name)
: wxDialog(c->parent, -1, _("Compare Subtitles"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
, c(c)
, comparison(comparison)
, state(comparison.Current(), comparison.Baseline(), comparison.Fields(), comparison.Items())
, pal(make_palette())
, file_name(name)
{
    auto project_file = c->subsController->Filename();
    project_name = project_file.empty() ? _("Untitled") : wxString(project_file.filename().wstring());

    // Toolbar: navigation, actions on the current difference, view options, undo.
    auto bar = new wxBoxSizer(wxHORIZONTAL);
    auto add = [&](wxButton* b, wxString const& tip, char const* id) {
        b->SetToolTip(tip); b->SetName(id);
        bar->Add(b, wxSizerFlags().Center().Border(wxRIGHT, 4));
        return b;
    };
    prev = add(new wxButton(this, ID_PREV, wxString::FromUTF8("↑ ") + _("Previous"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), _("Previous difference (Shift+F7)"), "compare-prev");
    next = add(new wxButton(this, ID_NEXT, wxString::FromUTF8("↓ ") + _("Next"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), _("Next difference (F7)"), "compare-next");
    counter = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    counter->SetName("compare-counter");
    counter->SetMinSize(wxSize(GetTextExtent(_("Difference 9999 of 9999")).GetWidth() + 16, -1));
    bar->Add(counter, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, 6));
    accept_current = add(new wxButton(this, ID_ACCEPT_CURRENT, wxString::FromUTF8("✓ ") + _("&Accept Change"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), _("Keep the right side of the current difference"), "compare-accept-current");
    reject_current = add(new wxButton(this, ID_REJECT_CURRENT, wxString::FromUTF8("✕ ") + _("&Reject Change"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), _("Restore the left side of the current difference"), "compare-reject-current");
    bar->AddStretchSpacer();
    diffs_only = new wxRadioButton(this, -1, _("Differences only"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    everything = new wxRadioButton(this, -1, _("Everything"));
    diffs_only->SetName("compare-diffs-only"); everything->SetName("compare-everything");
    diffs_only->SetToolTip(_("Fold long runs of identical lines, keeping context around each difference"));
    everything->SetToolTip(_("Show every line"));
    diffs_only->SetValue(true);
    bar->Add(diffs_only, wxSizerFlags().Center().Border(wxRIGHT, 4));
    bar->Add(everything, wxSizerFlags().Center().Border(wxRIGHT, 12));
    sync_box = new wxCheckBox(this, -1, _("Sync scrolling"));
    sync_box->SetName("compare-sync"); sync_box->SetValue(true);
    sync_box->SetToolTip(_("Scroll both sides together. When off, each side scrolls on its own and selecting a line locates it on the other side."));
    bar->Add(sync_box, wxSizerFlags().Center().Border(wxRIGHT, 12));
    auto swap = add(new wxButton(this, -1, wxString::FromUTF8("⇄ ") + _("Swap Sides"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT),
        _("Show the open subtitles on the other side. Decisions keep their effect on the open subtitles."), "compare-swap");
    undo = add(new wxButton(this, ID_UNDO, wxString::FromUTF8("↶ ") + _("Undo"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), _("Undo the last decision (Ctrl+Z)"), "compare-undo");
    redo = add(new wxButton(this, ID_REDO, wxString::FromUTF8("↷ ") + _("Redo"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), _("Redo (Ctrl+Y)"), "compare-redo");

    // Pane titles and panes.
    left_title = new wxStaticText(this, -1, "");
    right_title = new wxStaticText(this, -1, "");
    left_title->SetName("compare-left-title"); right_title->SetName("compare-right-title");
    auto bold = GetFont().Bold();
    left_title->SetFont(bold); right_title->SetFont(bold);
    left = new DiffPane(this, *this, Side::Left);
    right = new DiffPane(this, *this, Side::Right);

    // Fields of the current difference.
    fields_panel = new wxPanel(this, -1);
    fields_panel->SetName("compare-fields");
    fields_panel->SetSizer(new wxBoxSizer(wxHORIZONTAL));

    // Batch actions.
    check_all = new wxCheckBox(this, -1, _("Select all differences"));
    check_all->SetName("compare-check-all");
    checked_count = new wxStaticText(this, -1, "");
    checked_count->SetName("compare-checked-count");
    accept_checked = new wxButton(this, -1, _("Accept Selected"));
    reject_checked = new wxButton(this, -1, _("Reject Selected"));
    accept_all = new wxButton(this, -1, _("Accept All Changes"));
    reject_all = new wxButton(this, -1, _("Reject All Changes"));
    accept_checked->SetName("compare-accept-checked"); reject_checked->SetName("compare-reject-checked");
    accept_all->SetName("compare-accept-all"); reject_all->SetName("compare-reject-all");
    auto batch = new wxBoxSizer(wxHORIZONTAL);
    batch->Add(check_all, wxSizerFlags().Center().Border(wxRIGHT, 8));
    batch->Add(checked_count, wxSizerFlags().Center().Border(wxRIGHT, 12));
    batch->Add(accept_checked, wxSizerFlags().Center().Border(wxRIGHT, 4));
    batch->Add(reject_checked, wxSizerFlags().Center());
    batch->AddStretchSpacer();
    stats = new wxStaticText(this, -1, "");
    stats->SetName("compare-stats");
    batch->Add(stats, wxSizerFlags().Center().Border(wxRIGHT, 12));
    batch->Add(accept_all, wxSizerFlags().Center().Border(wxRIGHT, 4));
    batch->Add(reject_all, wxSizerFlags().Center());

    report = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxSize(-1, GetCharHeight() * 4), wxTE_MULTILINE | wxTE_READONLY);
    report->SetName("compare-report");

    auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    finish = static_cast<wxButton*>(FindWindow(wxID_OK));
    finish->SetToolTip(_("Write the decisions to the open subtitles as one undoable change and end the comparison"));

    auto left_column = new wxBoxSizer(wxVERTICAL);
    left_column->Add(left_title, wxSizerFlags().Border(wxLEFT | wxBOTTOM, 4));
    left_column->Add(left, wxSizerFlags(1).Expand());
    auto right_column = new wxBoxSizer(wxVERTICAL);
    right_column->Add(right_title, wxSizerFlags().Border(wxLEFT | wxBOTTOM, 4));
    right_column->Add(right, wxSizerFlags(1).Expand());
    // The left column's width is set on resize so both sides show as much text.
    auto panes = new wxBoxSizer(wxHORIZONTAL);
    panes->Add(left_column, wxSizerFlags().Expand());
    panes->AddSpacer(8);
    panes->Add(right_column, wxSizerFlags(1).Expand());

    auto hint = new wxStaticText(this, -1, _("Accept keeps the right side, Reject restores the left side. Browsing and deciding never change the open subtitles: they change only when you click Write to Project, as one undoable step."));
    hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

    auto main = new wxBoxSizer(wxVERTICAL);
    main->Add(bar, wxSizerFlags().Expand().Border(wxALL, 8));
    main->Add(panes, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 8));
    main->Add(fields_panel, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, 8));
    main->Add(batch, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, 8));
    main->Add(hint, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, 8));
    main->Add(report, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, 8));
    main->Add(buttons, wxSizerFlags().Expand().Border(wxALL, 8));
    SetSizer(main);

    Measure();
    // The right pane also holds the check boxes, actions and overview ruler:
    // it gets that much more width than the left one.
    left->SetMinSize(wxSize(320, 200));
    right->SetMinSize(wxSize(320 + check_w + actions_w + overview_w, 200));
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        int panes = GetClientSize().GetWidth() - 16 - 8;
        int extra = check_w + actions_w + overview_w;
        left->SetMinSize(wxSize(std::max(320, (panes - extra) / 2), 200));
        event.Skip(); // the default handler lays the sizers out
    });
    RebuildLayout();

    wxSize want(1200, 760);
    int display = wxDisplay::GetFromWindow(c->parent);
    if (display != wxNOT_FOUND) {
        auto area = wxDisplay(display).GetClientArea();
        want = wxSize(std::min(want.GetWidth(), area.GetWidth() * 9 / 10), std::min(want.GetHeight(), area.GetHeight() * 9 / 10));
    }
    SetMinSize(wxSize(860, 520));
    SetSize(want);
    CenterOnParent();

    // Events
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Go(-1); }, ID_PREV);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Go(1); }, ID_NEXT);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (auto e = CurrentEntry()) Decide(*e, Decision::Accepted); }, ID_ACCEPT_CURRENT);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (auto e = CurrentEntry()) Decide(*e, Decision::Rejected); }, ID_REJECT_CURRENT);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (state.Undo()) Changed(); }, ID_UNDO);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (state.Redo()) Changed(); }, ID_REDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Go(-1); }, ID_PREV);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Go(1); }, ID_NEXT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { if (state.Undo()) Changed(); }, ID_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { if (state.Redo()) Changed(); }, ID_REDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { if (state.Redo()) Changed(); }, ID_REDO_ALT);
    wxAcceleratorEntry keys[] = {
        {wxACCEL_NORMAL, WXK_F7, ID_NEXT}, {wxACCEL_SHIFT, WXK_F7, ID_PREV},
        {wxACCEL_ALT, WXK_DOWN, ID_NEXT}, {wxACCEL_ALT, WXK_UP, ID_PREV},
        {wxACCEL_CTRL, 'Z', ID_UNDO}, {wxACCEL_CTRL, 'Y', ID_REDO}, {wxACCEL_CTRL | wxACCEL_SHIFT, 'Z', ID_REDO_ALT},
    };
    SetAcceleratorTable(wxAcceleratorTable(std::size(keys), keys));

    diffs_only->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { show_all = false; expanded.clear(); RebuildLayout(); });
    everything->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { show_all = true; RebuildLayout(); });
    sync_box->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        sync = sync_box->GetValue();
        if (sync) left->SetView(right->Top(), right->XOffset());
    });
    swap->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        state.SetProjectSide(view::Other(state.ProjectSide()));
        fields_entry.reset();
        Changed();
    });
    check_all->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { state.CheckAll(check_all->GetValue()); Changed(); });
    accept_checked->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Batch(state.CheckedEntries(), Decision::Accepted, false); });
    reject_checked->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Batch(state.CheckedEntries(), Decision::Rejected, false); });
    accept_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Batch(state.Diffs(), Decision::Accepted, true); });
    reject_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Batch(state.Diffs(), Decision::Rejected, true); });
    finish->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnFinish(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnCancel(); }, wxID_CANCEL);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { OnCancel(); });

    if (!state.Diffs().empty()) current = 0;
    Changed();
    if (auto e = CurrentEntry()) { right->Reveal(row_of[*e]); left->SetView(right->Top(), right->XOffset()); }
    right->SetFocus();
}

void DialogCompare::Measure() {
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    char_w = std::max(1, dc.GetCharWidth());
    int char_h = dc.GetCharHeight();
    row_h = std::max(22, char_h + 10);
    small_font = GetFont().Smaller();
    size_t max_number = 1;
    int style = dc.GetTextExtent(_("Style")).GetWidth(), actor = 0, text = 0;
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
    number_w = dc.GetTextExtent(wxString::Format("%zu", max_number)).GetWidth() + 12;
    time_w = dc.GetTextExtent("0:00:00.000").GetWidth() + 14;
    style_w = std::min(style, char_w * 22) + 16;
    actor_w = actors.empty() ? 0 : std::min(actor, char_w * 16) + 16;
    content_w = 6 + time_w * 2 + style_w + actor_w + text + char_w * 12;

    dc.SetFont(small_font);
    button_w = std::max(dc.GetTextExtent(wxString::FromUTF8("✓ ") + _("Accept")).GetWidth(),
        dc.GetTextExtent(wxString::FromUTF8("✕ ") + _("Reject")).GetWidth()) + 14;
    int badge = 0;
    for (auto d : {Decision::Accepted, Decision::Rejected, Decision::Partial})
        badge = std::max(badge, dc.GetTextExtent(wxString::FromUTF8("✓ ") + decision_label(d)).GetWidth());
    reset_w = dc.GetTextExtent(wxString::FromUTF8("↺")).GetWidth() + 16;
    actions_w = std::max(button_w * 2 + 12, badge + reset_w + 20);
}

void DialogCompare::RebuildLayout() {
    // Keep the entry at the top of the right pane in place.
    std::optional<size_t> anchor;
    if (!layout.empty() && right->Top() < (int)layout.size()) anchor = layout[right->Top()].entry;
    layout = state.Layout(show_all, expanded);
    row_of.assign(state.Entries().size(), 0);
    for (size_t r = 0; r < layout.size(); ++r) {
        auto const& lr = layout[r];
        for (size_t k = 0; k < std::max<size_t>(1, lr.hidden); ++k) row_of[lr.entry + k] = (int)r;
    }
    int top = anchor ? row_of[*anchor] : 0;
    right->SetView(top, right->XOffset());
    left->SetView(sync ? top : (anchor ? row_of[*anchor] : left->Top()), left->XOffset());
    left->UpdateBars(); right->UpdateBars();
    left->Refresh(false); right->Refresh(false);
}

std::optional<size_t> DialogCompare::CurrentEntry() const {
    if (!current || *current >= state.Diffs().size()) return std::nullopt;
    return state.Diffs()[*current];
}

void DialogCompare::UpdateTitles() {
    auto project_tag = wxString::FromUTF8("  ·  ") + _("target project");
    bool project_right = state.ProjectSide() == Side::Right;
    left_title->SetLabel(fmt_tl("Original (base): %s", project_right ? file_name : project_name) + (project_right ? wxString() : project_tag));
    right_title->SetLabel(fmt_tl("Modified (current): %s", project_right ? project_name : file_name) + (project_right ? project_tag : wxString()));
    left_title->SetForegroundColour(project_right ? wxNullColour : pal.accent);
    right_title->SetForegroundColour(project_right ? pal.accent : wxNullColour);
}

void DialogCompare::UpdateFields() {
    auto entry = CurrentEntry();
    bool modified = entry && state.Entries()[*entry].change == Change::Modified;
    if (fields_entry != entry || field_boxes.empty() != !modified) {
        fields_entry = entry;
        fields_panel->DestroyChildren();
        field_boxes.clear();
        auto sizer = fields_panel->GetSizer();
        sizer->Clear();
        wxString label = !entry ? _("No difference selected.")
            : modified ? _("Fields of this line (checked: keep the right side, unchecked: keep the left side, grey: pending):")
            : _("Inserted and removed lines are decided as a whole.");
        sizer->Add(new wxStaticText(fields_panel, -1, label), wxSizerFlags().Center().Border(wxRIGHT, 8));
        if (modified) {
            auto const& e = state.Entries()[*entry];
            for (auto f : view::FieldOrder()) {
                if (!(e.changed & f)) continue;
                auto box = new wxCheckBox(fields_panel, -1, field_label(f), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE);
                box->SetName(wxString("compare-field-") + view::FieldName(f));
                box->SetToolTip(fmt_tl("%s\nLeft: %s\nRight: %s", field_label(f), one_line(e.left->Value(f), 200), one_line(e.right->Value(f), 200)));
                box->Bind(wxEVT_CHECKBOX, [this, f, box](wxCommandEvent&) {
                    auto entry = CurrentEntry();
                    if (!entry) return;
                    // The user can only check or uncheck; grey (pending) is set by the model.
                    bool right_side = box->Get3StateValue() == wxCHK_CHECKED;
                    if (state.SetField(*entry, f, right_side ? Side::Right : Side::Left)) Changed();
                    else UpdateFields();
                });
                sizer->Add(box, wxSizerFlags().Center().Border(wxRIGHT, 10));
                field_boxes.emplace_back(f, box);
            }
        }
        fields_panel->Layout();
        Layout();
    }
    for (auto& [f, box] : field_boxes) {
        auto winner = state.Winner(*entry, f);
        box->Set3StateValue(!winner ? wxCHK_UNDETERMINED : *winner == Side::Right ? wxCHK_CHECKED : wxCHK_UNCHECKED);
    }
}

void DialogCompare::Changed() {
    size_t total = state.Diffs().size();
    counter->SetLabel(current ? fmt_tl("Difference %d of %d", (int)*current + 1, (int)total) : fmt_tl("%d differences", (int)total));
    stats->SetLabel(fmt_tl("%d pending, %d accepted, %d rejected, %d partial",
        (int)state.Count(Decision::Pending), (int)state.Count(Decision::Accepted),
        (int)state.Count(Decision::Rejected), (int)state.Count(Decision::Partial)));
    auto checked = state.CheckedEntries();
    checked_count->SetLabel(fmt_tl("%d selected", (int)checked.size()));
    check_all->SetValue(total && checked.size() == total);
    accept_checked->Enable(!checked.empty());
    reject_checked->Enable(!checked.empty());
    prev->Enable(total > 0); next->Enable(total > 0);
    accept_current->Enable(CurrentEntry().has_value());
    reject_current->Enable(CurrentEntry().has_value());
    undo->Enable(state.CanUndo()); redo->Enable(state.CanRedo());
    UpdateTitles();
    UpdateFields();

    auto choices = state.Choices();
    if (choices.empty()) {
        report->SetValue(state.Count(Decision::Pending)
            ? _("No decision changes the open subtitles yet. Pending differences leave them as they are.")
            : _("The decisions leave the open subtitles as they are."));
        finish->SetLabel(_("&Done"));
        finish->Enable(true);
    }
    else {
        auto r = c->subsController->CheckApply(comparison, choices);
        report->SetValue(describe(r));
        finish->SetLabel(fmt_tl("&Write to Project (%d)", (int)choices.size()));
        finish->Enable(r.Ok());
    }
    finish->GetContainingSizer()->Layout();
    left->Refresh(false); right->Refresh(false);
}

void DialogCompare::Go(int delta) {
    size_t total = state.Diffs().size();
    if (!total) return;
    if (!current) {
        // Start from the view: the first difference at or below the top row.
        size_t top_entry = layout.empty() ? 0 : layout[right->Top()].entry;
        auto it = std::lower_bound(state.Diffs().begin(), state.Diffs().end(), top_entry);
        size_t i = it == state.Diffs().end() ? total - 1 : size_t(it - state.Diffs().begin());
        current = delta > 0 ? i : (i == 0 ? total - 1 : i - 1);
    }
    else current = (*current + total + (delta > 0 ? 1 : total - 1)) % total; // wraps around
    auto row = row_of[*CurrentEntry()];
    right->Reveal(row); left->Reveal(row);
    if (sync) left->SetView(right->Top(), right->XOffset());
    Changed();
}

void DialogCompare::Select(size_t entry, Side from) {
    if (auto index = state.DiffIndex(entry)) current = *index;
    // Locate the line on the other side too (the only way they line up when not syncing).
    auto other = PaneFor(view::Other(from));
    auto here = PaneFor(from);
    int row = row_of[entry];
    // Put it at the same height as on the clicked side.
    if (!sync && (row < other->Top() || row >= other->Top() + other->VisibleRows()))
        other->SetView(here->Top(), other->XOffset());
    Changed();
}

void DialogCompare::Decide(size_t entry, Decision d) {
    if (auto index = state.DiffIndex(entry)) current = *index;
    state.Decide({entry}, d);
    Changed();
}

void DialogCompare::ToggleCheck(size_t entry) {
    state.Check(entry, !state.Checked(entry));
    Changed();
}

void DialogCompare::ExpandFold(int row) {
    if (row < 0 || row >= (int)layout.size() || !layout[row].IsFold()) return;
    expanded.insert(layout[row].entry);
    int left_top = left->Top(), right_top = right->Top();
    layout = state.Layout(show_all, expanded);
    row_of.assign(state.Entries().size(), 0);
    for (size_t r = 0; r < layout.size(); ++r)
        for (size_t k = 0; k < std::max<size_t>(1, layout[r].hidden); ++k) row_of[layout[r].entry + k] = (int)r;
    // Rows above the fold keep their positions; the hidden lines open in place.
    right->SetView(right_top, right->XOffset());
    left->SetView(left_top, left->XOffset());
    Changed();
}

void DialogCompare::Scrolled(Side from) {
    if (!sync) return;
    auto src = PaneFor(from);
    PaneFor(view::Other(from))->SetView(src->Top(), src->XOffset());
}

bool DialogCompare::ConfirmBatch(std::vector<size_t> const& entries, Decision d, bool all) {
    size_t changing = state.Changing(entries, d), decided = 0;
    for (auto e : entries) decided += state.GetDecision(e) != Decision::Pending && state.GetDecision(e) != d;
    if (!changing) {
        wxMessageBox(d == Decision::Accepted ? _("These differences are already accepted.") : _("These differences are already rejected."),
            _("Compare Subtitles"), wxOK | wxICON_INFORMATION, this);
        return false;
    }
    wxString scope = all ? fmt_tl("all %d differences", (int)entries.size()) : fmt_tl("the %d selected differences", (int)entries.size());
    wxString action = d == Decision::Accepted ? _("Accept (keep the right side)") : _("Reject (restore the left side)");
    wxString message = fmt_tl("%s: %s?", action, scope);
    message += "\n" + fmt_tl("%d of them will change.", (int)changing);
    if (decided) message += "\n" + fmt_tl("%d already decided the other way will be overwritten.", (int)decided);
    message += "\n\n" + _("This can be undone in this window. The open subtitles change only when you write to the project.");
    return wxMessageBox(message, _("Compare Subtitles"), wxOK | wxCANCEL | wxICON_QUESTION, this) == wxOK;
}

void DialogCompare::Batch(std::vector<size_t> const& entries, Decision d, bool all) {
    if (entries.empty() || !ConfirmBatch(entries, d, all)) return;
    state.Decide(entries, d);
    Changed();
}

void DialogCompare::ContextMenu(DiffPane* pane, size_t entry, wxPoint pos) {
    auto const& e = state.Entries()[entry];
    wxMenu menu;
    auto d = state.GetDecision(entry);
    menu.Append(ID_MENU_ACCEPT, wxString::FromUTF8("✓ ") + _("Accept Change"))->Enable(d != Decision::Accepted);
    menu.Append(ID_MENU_REJECT, wxString::FromUTF8("✕ ") + _("Reject Change"))->Enable(d != Decision::Rejected);
    menu.Append(ID_MENU_RESET, _("Reset to Pending"))->Enable(d != Decision::Pending);
    menu.AppendSeparator();
    menu.AppendCheckItem(ID_MENU_CHECK, _("Selected"))->Check(state.Checked(entry));
    if (e.change == Change::Modified) {
        menu.AppendSeparator();
        for (auto f : view::FieldOrder()) {
            if (!(e.changed & f)) continue;
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
        if (state.SetField(entry, f, winner && *winner == Side::Right ? Side::Left : Side::Right)) Changed();
    }
}

wxString DialogCompare::Tooltip(size_t entry, Side side) const {
    auto const& e = state.Entries()[entry];
    auto const& line = e.Get(side);
    wxString out;
    if (!line) out = side == Side::Left ? _("Not in the left version (inserted on the right)") : _("Not in the right version (removed on the right)");
    else {
        out = fmt_tl("Line %d: %s - %s, %s", (int)line->number, to_wx(line->Value(model::Start)),
            to_wx(line->Value(model::End)), to_wx(line->Value(model::Style)));
        if (!line->Value(model::Actor).empty()) out += ", " + to_wx(line->Value(model::Actor));
        out += "\n" + one_line(line->Value(model::Text), 600);
        if (e.change == Change::Modified) {
            wxString fields;
            for (auto f : view::FieldOrder())
                if (e.changed & f) fields += (fields.empty() ? "" : ", ") + field_label(f);
            out += "\n" + fmt_tl("Changed: %s", fields);
        }
    }
    if (e.IsDiff()) out += "\n" + fmt_tl("Status: %s", decision_label(state.GetDecision(entry)));
    if (e.ambiguity != model::Ambiguity::None) out += "\n" + fmt_tl("Match note: %s", ambiguity_label(e.ambiguity));
    return out;
}

void DialogCompare::OnFinish() {
    auto choices = state.Choices();
    if (choices.empty()) {
        comparison.End();
        EndModal(wxID_CANCEL);
        return;
    }
    subtitle_apply::Report r;
    try {
        r = c->subsController->ApplyComparison(comparison, choices);
    }
    catch (SubtitleApplyRejected const& e) {
        wxMessageBox(to_wx(e.what()), _("Compare Subtitles"), wxOK | wxICON_ERROR, this);
        EndModal(wxID_CANCEL);
        return;
    }
    if (!r.Ok()) { report->SetValue(describe(r)); finish->Enable(false); return; }
    EndModal(wxID_OK);
}

void DialogCompare::OnCancel() {
    if (!state.Choices().empty()) {
        auto message = fmt_tl("Close the comparison and discard %d decisions that would change the open subtitles?", (int)state.Choices().size());
        if (wxMessageBox(message, _("Compare Subtitles"), wxOK | wxCANCEL | wxICON_QUESTION, this) != wxOK) return;
    }
    EndModal(wxID_CANCEL);
}
}

bool ShowCompareDialog(agi::Context *c, agi::fs::path const& path) {
    auto title = _("Compare Subtitles");
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
    DialogCompare dialog(c, *comparison, path.filename().wstring());
    return dialog.ShowModal() == wxID_OK;
}
