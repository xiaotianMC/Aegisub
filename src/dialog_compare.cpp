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
#include "options.h"
#include "project.h"
#include "subs_controller.h"
#include "subtitle_compare.h"
#include "subtitle_compare_view.h"
#include "subtitle_format.h"
#include "tag_chips.h"
#include "tag_summary.h"
#include "theme.h"
#include "workspace_tabs.h"

#include <libaegisub/charset.h>
#include <libaegisub/exception.h>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/renderer.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/time.h>
#include <wx/utils.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>

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
// Each colour says one thing: added green, removed red, modified amber, so
// that blue stays the accent, for what can be clicked and for the keyboard.
// A modified line has no background of its own (its changed words and
// fields are marked, and the gutter's band joins its sides): only added and
// removed lines, and the current difference, are coloured whole. A decision
// is told by which side it keeps, in the text colour, never by the change
// colours; an empty side is hatched in a neutral grey.
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
        "#1f3a28", "#2f6b40", "#3d1f22", "#7d2f35", "#3a301a", "#d6a03c",
        "#232323", "#343434", "#2a2d33", "#6cb6ff", "#2a2d2e",
        "#3fb950", "#f85149", "#a371f7", "#e6e6e6"};
    return {
        "#ffffff", "#1f2328", "#8c959f", "#f6f8fa", "#6e7781", "#d0d7de", "#0969da",
        "#e6ffec", "#abf2bc", "#ffebe9", "#ffc1bf", "#fff3d6", "#c8901a",
        "#f6f8fa", "#e3e6ea", "#eef1f5", "#0969da", "#f3f4f6",
        "#1a7f37", "#cf222e", "#8250df", "#1f2328"};
}

enum class Part { None, Content, Check, Accept, Reject, Fold, Overview };

/// A row of the window: a layout row of the model (an entry or a fold), or,
/// under the current difference, one of its changed fields or the foot of
/// its card, where its two sides are chosen.
struct Row {
    size_t entry = 0;
    size_t hidden = 0;
    std::optional<model::Field> field;
    bool foot = false;
    bool IsFold() const { return hidden > 0; }
    bool IsDetail() const { return field.has_value(); }
    bool IsFoot() const { return foot; }
    /// Under the current difference's line, in its card.
    bool InCard() const { return IsDetail() || foot; }
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
    wxScrollBar *vbar;
    /// The first row in view. Long lines wrap rather than scroll sideways,
    /// so nothing does: xoff stays 0.
    int top = 0, xoff = 0;
    Part hover_part = Part::None;
    int tip_row = -1;
    Part tip_part = Part::None;
    bool dragging_overview = false;
    int drag_offset = 0; // where in the thumb the drag took hold
    const tag_summary::Piece* tip_block = nullptr;
    int wheel_rest = 0, zoom_rest = 0; // what the wheel turned short of a step, scrolling and with Ctrl

    struct Geometry {
        int width, height;       // the rows' area (without the track)
        int content_x, content_right;
        int overview_x;          // the track, in row coordinates: after the rows on the right, before them on the left
    };
    Geometry Geo() const;
    Part PartAt(int row, wxPoint pt) const;

    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouse(wxMouseEvent& event);
    void OnKey(wxKeyEvent& event);
    void OnScroll(wxScrollEvent&);
    void DrawRow(wxDC& dc, Geometry const& g, int row, int y);
    /// The frame round the keyboard's line, while either pane has the focus.
    void DrawCursor(wxDC& dc, Geometry const& g, int row, int y);
    /// Draw @p entry's line on this side; returns the x where what it drew ends.
    int DrawLine(wxDC& dc, Geometry const& g, size_t entry, int y);
    /// The current difference's card where it crosses @p row: the page round
    /// it, its shadow and its face, the line's own colour on its first row.
    void DrawCard(wxDC& dc, Geometry const& g, int row, int y);
    void DrawDetail(wxDC& dc, Geometry const& g, int row, int y);
    /// The card's foot: what is decided on the left, the keys on the right,
    /// round the buttons that choose (ComparePanel::PlaceCard).
    void DrawFoot(wxDC& dc, Geometry const& g, int row, int y);
    /// A piece of a line's text as drawn: a run of the text itself, a chip
    /// for a {…} block (tags simplified), a label standing for a drawing's
    /// commands, or where a hidden block was.
    struct TextPiece {
        enum Kind { Run, Chip, Label, Gap } kind = Run;
        size_t begin = 0, end = 0;  // bytes of the text
        int left = 0, right = 0;    // x
        bool changed = false;       // touches a changed token
        int line = 0;               // of the row's wrapped lines, from 0
        const tag_summary::Piece* block = nullptr;
        wxString label;
        /// A changed chip: the values of the tags in it that changed, written after its icons
        wxString values;
        int values_left = 0;
    };
    /// What changed in @p block of @p entry's text on this side against the
    /// block in the same place on the other side: the values of its tags
    /// that differ ("640,60"), or empty when the blocks do not pair up.
    wxString ChangedValues(size_t entry, tag_summary::Piece const& block) const;
    /// Lay the text of @p entry on this side out from @p x, as the tag mode
    /// shows it (@p full: tags as written whatever the mode), wrapping onto
    /// further lines from @p x where it reaches @p wrap_right (0: never),
    /// and after each \N.
    std::vector<TextPiece> LayText(wxDC& dc, size_t entry, int x, bool highlight, bool full, int wrap_right = 0) const;
    /// The text of @p entry on this side from @p x, changed tokens highlighted
    /// if @p highlight, struck through if @p lost, wrapped as LayText does.
    /// Returns the x after it, and in @p end_line the line it ends on.
    int DrawText(wxDC& dc, size_t entry, int x, int y, bool highlight, bool lost, bool full = false, int wrap_right = 0, int* end_line = nullptr);
    /// Where the text column starts, in row coordinates, and where its lines wrap.
    int TextX(Geometry const& g) const;
    int TextRight(Geometry const& g) const;
    /// Where a detail row's value starts, after the field's name.
    int DetailX(Geometry const& g) const;
public:
    /// The style and actor columns' widths in this pane: narrowed (their
    /// values cut short) where they would leave the text less than about
    /// half of the pane to wrap in.
    std::pair<int, int> LabelColumns() const;
private:
    /// The quiet chips after a line's text: its other changed fields, and
    /// whether it is a comment.
    struct Trail { wxString label; bool quiet, struck; };
    std::vector<Trail> Trailing(size_t entry) const;
    /// Where the chips after a text ending at @p x on @p line go: on that
    /// line while they fit, else from @p left on the next. Calls @p place
    /// with each chip's x and line, and returns the last line.
    int PlaceTrailing(wxDC& dc, std::vector<Trail> const& chips, int x, int line, int left, int right,
        std::function<void(Trail const&, int x, int line, int w)> const& place) const;
    /// The tag block under @p pt on @p row, when tags are simplified.
    const tag_summary::Piece* BlockAt(int row, wxPoint pt);
    void DrawDelta(wxDC& dc, size_t entry, model::Field f, int x, int y);
    void DrawOverview(wxDC& dc, Geometry const& g);
    wxWindow* header = nullptr;
public:
    /// Repaint this pane and the gutter, which follows its scroll position,
    /// and move the card's buttons with it.
    void Redraw();
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
    int RowsX() const;
    /// Height of the row area (without the horizontal scroll bar).
    int ViewHeight() const { return Geo().height; }
    int XOffset() const { return xoff; }
    /// The row at @p y in the rows' area, or -1.
    int RowAt(int y) const;
    /// How many rows from the top start within the view (the last perhaps in part).
    int VisibleRows() const;
    /// The last row that can be the top one, the view then reaching the end.
    int LastTop() const;
    /// How many lines @p row takes on this side once its text wraps.
    int RowLines(wxDC& dc, int row) const;
    /// Clamp and apply a scroll position; @p notify tells the editor (for sync).
    void SetView(int new_top, int new_x, bool notify = false);
    /// Scroll so that rows @p first to @p last are visible, if they were not.
    void Reveal(int first, int last = -1);
    void UpdateBars();
    /// Scroll by what the wheel turned, keeping what falls short of a step
    /// (fine wheels and touchpads turn a fraction of one at a time); with
    /// Ctrl, make the text larger or smaller.
    void Wheel(wxMouseEvent& event);
};

// The spine between the panes: each side's line numbers next to its pane and
// one lane between them. The lane marks each difference (a stroke in the
// colour of its change while pending, an arrow to the side it keeps once
// decided), joins its two sides (bent when the panes are scrolled apart),
// and on the pointed and the current line offers the two sides: ← takes the
// left side (Reject), → keeps the right (Accept), a chosen one clicked again
// resets the difference to pending. On the detail rows of the current
// difference they choose one field.
class DiffGutter final : public wxWindow {
    ComparePanel& ed;
    int hover_row = -1;
    Part hover_part = Part::None;
    Side hover_side = Side::Left;
    int tip_row = -1;
    Part tip_part = Part::None;

    struct Columns { int left_number, band, right_number, width; };
    Columns Cols() const;
    /// Gutter y of the first visible row of @p side's pane.
    int Offset(Side side) const;
    int RowAt(Side side, int y) const;
    void OnPaint(wxPaintEvent&);
    void OnMouse(wxMouseEvent& event);
    void DrawStrip(wxDC& dc, Side side, int row, int y);
    /// The mark of @p row's difference in the lane, at @p y.
    void DrawMark(wxDC& dc, int row, int y);
public:
    DiffGutter(wxWindow* parent, ComparePanel& ed);
    int Width() const;
    bool AcceptsFocus() const override { return false; }
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
    bool AcceptsFocus() const override { return false; }
};

// "target project" beside the file that writing changes: a small outlined
// label in the accent colour, rather than the whole title in it.
class TargetBadge final : public wxWindow {
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        auto size = GetClientSize();
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        auto accent = theme::UiColours().accent;
        flat::DrawOutline(dc, wxRect(size), size.GetHeight() / 2.0, 1, accent);
        dc.SetFont(GetFont());
        dc.SetTextForeground(accent);
        auto extent = dc.GetTextExtent(GetLabel());
        dc.DrawText(GetLabel(), (size.GetWidth() - extent.GetWidth()) / 2, (size.GetHeight() - extent.GetHeight()) / 2);
    }
public:
    TargetBadge(wxWindow* parent, wxString const& label, char const* name)
    : wxWindow(parent, -1)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetName(name);
        SetLabel(label);
        SetFont(GetFont().Smaller());
        Bind(wxEVT_PAINT, &TargetBadge::OnPaint, this);
    }
    wxSize DoGetBestClientSize() const override {
        wxClientDC dc(const_cast<TargetBadge*>(this));
        dc.SetFont(GetFont());
        auto extent = dc.GetTextExtent(GetLabel());
        return wxSize(extent.GetWidth() + FromDIP(14), extent.GetHeight() + FromDIP(4));
    }
    bool AcceptsFocus() const override { return false; }
};

/// A panel dropped from one of the page's buttons (the filters, the view,
/// the foot's batch actions), on the field colour with a hairline round it.
/// Its controls live on while it is closed, so what they are set to stays.
/// The button is in the accent colour while it is open; the click on the
/// button that closed it does not open it again.
class ComparePopup final : public wxPopupTransientWindow {
    FlatButton* anchor;
    bool above, align_right;
    wxLongLong closed_at = 0;
    void OnDismiss() override {
        anchor->SetAccent(false);
        closed_at = wxGetLocalTimeMillis();
        if (on_change) on_change();
    }
public:
    wxPanel* body;
    wxBoxSizer* items;
    /// Called once it has opened or closed.
    std::function<void()> on_change;
    ComparePopup(wxWindow* parent, FlatButton* button, bool above, bool align_right)
    : wxPopupTransientWindow(parent, wxBORDER_NONE)
    , anchor(button), above(above), align_right(align_right)
    {
        body = new wxPanel(this, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE);
        items = new wxBoxSizer(wxVERTICAL);
        auto outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(items, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(6)));
        body->SetSizer(outer);
        body->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxPaintDC dc(body);
            dc.SetPen(wxPen(flat::Colours(body).line));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(wxRect(body->GetClientSize()));
        });
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
            if (e.GetKeyCode() == WXK_ESCAPE) { Close(); anchor->SetFocus(); }
            else e.Skip();
        });
    }
    /// A small heading in the dim colour over a group of its items.
    void Heading(wxString const& text, bool first = false) {
        auto label = flat::DimLabel(body, text);
        label->SetFont(label->GetFont().Smaller());
        items->Add(label, wxSizerFlags().Border(wxLEFT, FromDIP(10)).Border(wxTOP, first ? 0 : FromDIP(8)));
        items->AddSpacer(FromDIP(2));
    }
    /// A row of the panel the whole width of it.
    void Item(wxWindow* item) {
        if (auto b = dynamic_cast<FlatButton*>(item)) b->SetMenuItem(true);
        items->Add(item, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(1)));
    }
    void Toggle() {
        if (IsShown()) { Close(); return; }
        if (wxGetLocalTimeMillis() - closed_at < 250) return;
        Open();
    }
    void Open() {
        body->SetBackgroundColour(flat::Colours(anchor).field);
        Refit();
        anchor->SetAccent(true);
        // The keyboard into it, on its first control.
        wxWindow* first = nullptr;
        for (auto w : body->GetChildren())
            if (w->IsShown() && w->AcceptsFocusFromKeyboard()) { first = w; break; }
        Popup(first);
        if (on_change) on_change();
    }
    void Close() { if (IsShown()) DismissAndNotify(); }
    /// Fit what is shown in it, below its button (above it in the foot).
    void Refit() {
        body->Layout();
        auto size = body->GetSizer()->GetMinSize();
        size.x = std::max(size.x, FromDIP(180));
        body->SetSize(size);
        SetClientSize(size);
        auto at = anchor->ClientToScreen(wxPoint(0, 0));
        auto a = anchor->GetSize();
        int x = align_right ? at.x + a.x - size.x : at.x;
        int y = above ? at.y - size.y - FromDIP(4) : at.y + a.y + FromDIP(4);
        SetPosition(wxPoint(x, y));
        body->Refresh();
    }
};

/// How far the decisions have come: "Decided 2 / 9" and a thin bar, its
/// parts the differences taking the left side, the right side and some of
/// each, in greys, for a side is neither right nor wrong. Its label is the
/// four counts in words, as its tooltip says them.
class DecisionMeter final : public wxWindow {
    int left = 0, right = 0, partial = 0, total = 0;
    wxString Text() const { return fmt_tl("Decided %d / %d", left + right + partial, total); }
    int BarWidth() const { return FromDIP(110); }
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC buffered(this);
        wxGCDC dc(buffered);
        auto p = flat::Colours(this);
        auto under = GetParent()->GetBackgroundColour();
        auto size = GetClientSize();
        dc.SetBackground(wxBrush(under));
        dc.Clear();
        dc.SetFont(GetFont());
        dc.SetTextForeground(p.dim);
        auto text = Text();
        auto extent = dc.GetTextExtent(text);
        int bar_w = BarWidth(), bar_h = FromDIP(6);
        wxRect bar(size.x - bar_w, (size.y - bar_h) / 2, bar_w, bar_h);
        dc.DrawText(text, bar.x - FromDIP(8) - extent.x, (size.y - extent.y) / 2);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(theme::Mix(under, p.text, 0.1)));
        dc.DrawRoundedRectangle(bar, bar_h / 2.0);
        if (!total) return;
        // From the whole of what is decided back to the left side alone,
        // each over the last.
        int counts[] = {left + right + partial, left + right, left};
        double shades[] = {0.75, 0.55, 0.3};
        for (int i = 0; i < 3; ++i) {
            int w = (int)std::lround(double(bar_w) * counts[i] / total);
            if (w <= 0) continue;
            dc.SetBrush(wxBrush(theme::Mix(p.text, under, shades[i])));
            dc.DrawRoundedRectangle(wxRect(bar.x, bar.y, std::max(w, bar_h), bar_h), bar_h / 2.0);
        }
    }
public:
    DecisionMeter(wxWindow* parent)
    : wxWindow(parent, -1)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetName("compare-stats");
        Bind(wxEVT_PAINT, &DecisionMeter::OnPaint, this);
    }
    void Set(int pending, int l, int r, int p) {
        left = l; right = r; partial = p;
        if (total != pending + l + r + p) { total = pending + l + r + p; InvalidateBestSize(); }
        SetLabel(fmt_tl("%d pending · %d left · %d right · %d partial", pending, l, r, p));
        SetToolTip(GetLabel());
        Refresh(false);
    }
    wxSize DoGetBestClientSize() const override {
        wxClientDC dc(const_cast<DecisionMeter*>(this));
        dc.SetFont(GetFont());
        // as wide as when every one is decided, so it does not move
        auto extent = dc.GetTextExtent(fmt_tl("Decided %d / %d", total, total));
        return wxSize(extent.x + FromDIP(8) + BarWidth(), extent.y + FromDIP(4));
    }
    bool AcceptsFocus() const override { return false; }
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
    /// Where each row starts, from the first, and after the last: a row is
    /// as tall as the more lines its text wraps onto on either side.
    std::vector<int> row_y;
    /// The wrapped lines of each entry's row, left and right, while the
    /// panes' widths and what is shown in them stay as they were.
    std::unordered_map<size_t, std::pair<int, int>> lines_cache;
    wxString lines_key;
    std::vector<int> row_of;            // entry -> its row (the fold row for hidden entries)
    std::optional<size_t> current;      // the current difference (an entry)
    int hot = -1;                       // row under the pointer, in any pane or the gutter
    std::optional<model::Field> field_cursor; // the keyboard's field among the current difference's detail rows
    int cursor_row = -1;                // the keyboard's line, in both panes

    // override tags as the subtitle grid shows them: in full, simplified, hidden
    const agi::OptionValue* tag_mode;
    agi::signal::Connection tag_mode_changed;
    mutable std::unordered_map<std::string, std::vector<tag_summary::Piece>> summaries;
    tag_chips::Painter chips_painter;
    FlatSwitch* tags_switch;

    // metrics
    /// Every time is under an hour: shown without the hour ("1:02.500").
    bool short_times = false;
    /// The columns left out because every line has the same value there,
    /// said once at the end of the column names ("Style: Default").
    wxString same_note;
    /// A row of one line, and how much each line more of wrapped text adds.
    int row_h = 22, line_pitch = 20;
    int char_w = 7, number_w = 40, band_w = 28, time_w = 80, style_w = 80, actor_w = 0;
    int overview_w = 14 /* each pane's track */, content_w = 400, label_w = 80, delta_w = 0;
    /// How far the card stands in from a pane's outer edge, and its corners.
    int card_inset = 4, card_radius = 7;
    /// The lines' font, its size set by Subtitle/Compare/Font Size (0: the page's).
    wxFont pane_font, small_font, bold_font, small_bold_font;
    agi::signal::Connection font_size_changed;
    wxStaticText* font_size_label = nullptr;
    FlatButton *font_smaller = nullptr, *font_larger = nullptr, *font_reset = nullptr;

    DiffPane *left, *right;
    DiffGutter *gutter;
    DiffHeader *left_header, *right_header;
    wxStaticText *left_title, *right_title, *counter, *checked_count;
    DecisionMeter *stats;
    FlatButton *prev, *next, *undo, *redo;
    /// On the current difference's card: the left side, the right side, back to pending.
    FlatButton *reject_current, *accept_current, *reset_current;
    FlatButton *accept_checked, *reject_checked, *clear_checked, *accept_all, *reject_all, *finish;
    FlatCheck *check_all, *sync_box, *pending_box;
    /// The filters, the view's settings ("⋯") and the batch actions, each a
    /// panel dropped from its button.
    FlatButton *filter_button, *more_button, *batch_button;
    ComparePopup *filter_popup, *view_popup, *batch_popup;
    std::vector<std::pair<view::Category, FlatCheck*>> ignore_boxes;
    wxBoxSizer *main_sizer, *chips, *select_group;
    wxStaticText *left_role, *right_role;
    /// The two sides with their names and columns, one sheet in the page's
    /// colour for the lines, without frames inside it.
    wxPanel *sheet;
    TargetBadge *left_target, *right_target;
    wxString chips_key;
    FlatSwitch *view_switch;
    std::vector<std::pair<std::optional<view::Category>, FlatButton*>> category_buttons;
    wxStaticText *report;
    /// The keys for the current difference, on its card's foot after its
    /// buttons (PlaceCard).
    wxStaticText *hint;
    /// The changed fields of the current difference that have detail rows.
    std::vector<model::Field> DetailFields() const;

    void Measure();
    /// Set the lines' font from the option, and measure again (and lay the
    /// rows out again unless @p starting).
    void ApplyFontSize(bool starting = false);
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
    /// What the keys do on the current difference: on its fields, or on the
    /// keyboard's field.
    wxString KeyLine() const;
    void UpdateKeyLine();
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
    /// Put the keyboard's line on @p row (clamped), in view in @p from's pane.
    void PlaceCursor(int row, Side from);
    /// Move the keyboard's line by @p delta rows, from where it is if @p from's
    /// pane shows it, else from that pane's top row.
    void MoveCursor(int delta, Side from);
    /// Enter on the keyboard's line: open a fold, or make its difference current.
    void ActivateCursor(Side from);
    /// Space: select the keyboard's difference, else the current one.
    void CheckCursor();
    /// 0 tags in full, 1 simplified, 2 hidden.
    int TagMode() const { return tag_mode->GetInt(); }
    std::vector<tag_summary::Piece> const& Summary(std::string const& text) const;
    void Scrolled(Side from);
    void ContextMenu(DiffPane* pane, size_t entry, wxPoint pos);
    wxString Tooltip(int row, Side side) const;
    /// Line colour of a difference on one side: added lines green, removed
    /// red, lighter where the side is empty or the difference is decided,
    /// darker for the current one; a modified line only while current.
    wxColour RowColour(size_t entry, Side side) const;
    wxColour EdgeColour(size_t entry) const;
    /// Whether row @p row begins (ends) a run of differences of one kind.
    bool ChunkStart(int row) const;
    bool ChunkEnd(int row) const;
    /// Whether @p row is the last row of its entry (the entry, or the foot of its card).
    bool GroupEnd(int row) const { return row + 1 >= (int)layout.size() || !layout[row + 1].InCard(); }
    /// The rows of the current difference's card, first and last; -1 without one.
    std::pair<int, int> CardRows() const;
    /// Put the card's buttons on its foot in each pane, hidden where it is out of view.
    void PlaceCard();
    /// What the card's foot says is decided ("Partial: Text →, Actor ←").
    wxString CardStatus() const;
    void RefreshGutter() { gutter->Refresh(false); }
    /// Where @p row starts, from the first row, and how tall it is.
    int RowY(int row) const { return row >= 0 && row < (int)row_y.size() ? row_y[row] : row * row_h; }
    int RowH(int row) const { return row >= 0 && row + 1 < (int)row_y.size() ? row_y[row + 1] - row_y[row] : row_h; }
    /// Find how tall each row is (after the rows, the panes' widths or the font changed).
    void MeasureRows();
    /// Make the lines' text @p steps points larger (smaller if negative); 0 back to the page's size.
    void Zoom(int steps);
    /// What clicking @p part of @p row does, for its tooltip (empty for plain content); @p in_gutter for the arrows.
    wxString Help(Part part, bool in_gutter, int row) const;
};

// ---- DiffPane ----
DiffPane::DiffPane(wxWindow* parent, ComparePanel& ed, Side side)
: wxWindow(parent, -1, wxDefaultPosition, DipSize(300, 200), wxWANTS_CHARS | wxBORDER_NONE)
, ed(ed)
, side(side)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(ed.pal.bg); // under the card's buttons' corners
    SetName(side == Side::Left ? "compare-left" : "compare-right");
    // The rows scroll by a track of our own, which is also the overview of
    // the differences (DrawOverview); the toolkit's bar is kept, hidden, as
    // the record of the position.
    vbar = new wxScrollBar(this, -1, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
    vbar->Hide();
    Bind(wxEVT_PAINT, &DiffPane::OnPaint, this);
    Bind(wxEVT_SIZE, &DiffPane::OnSize, this);
    for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_RIGHT_DOWN, wxEVT_MOTION, wxEVT_LEAVE_WINDOW, wxEVT_MOUSEWHEEL})
        Bind(type, &DiffPane::OnMouse, this);
    Bind(wxEVT_KEY_DOWN, &DiffPane::OnKey, this);
    for (auto type : {wxEVT_SCROLL_TOP, wxEVT_SCROLL_BOTTOM, wxEVT_SCROLL_LINEUP, wxEVT_SCROLL_LINEDOWN,
            wxEVT_SCROLL_PAGEUP, wxEVT_SCROLL_PAGEDOWN, wxEVT_SCROLL_THUMBTRACK, wxEVT_SCROLL_THUMBRELEASE, wxEVT_SCROLL_CHANGED})
        vbar->Bind(type, &DiffPane::OnScroll, this);
    // The wheel over the bar scrolls as over the rows (once: not the bar's own way too).
    vbar->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) { Wheel(e); });
    // Tab goes from pane to pane, not into its bar.
    vbar->DisableFocusFromKeyboard();
    // The keyboard's line shows in both panes while either has the focus.
    auto focus_changed = [this](wxFocusEvent& e) {
        Refresh(false);
        if (auto other = this->ed.PaneFor(view::Other(this->side))) other->Refresh(false);
        e.Skip();
    };
    Bind(wxEVT_SET_FOCUS, focus_changed);
    Bind(wxEVT_KILL_FOCUS, focus_changed);
}

int DiffPane::RowsX() const { return side == Side::Left ? ed.overview_w : 0; }

DiffPane::Geometry DiffPane::Geo() const {
    Geometry g;
    auto size = GetClientSize();
    g.width = std::max(0, size.GetWidth() - ed.overview_w);
    g.height = std::max(0, size.GetHeight());
    // Both sides show their lines alike, each with its track on its outer edge.
    g.content_x = 0;
    g.content_right = g.width;
    g.overview_x = side == Side::Right ? g.width : -ed.overview_w;
    return g;
}

int DiffPane::VisibleRows() const {
    int rows = (int)ed.layout.size(), h = Geo().height, base = ed.RowY(top), n = 0;
    for (int r = top; r < rows && ed.RowY(r) - base < h; ++r) ++n;
    return std::max(1, n);
}

int DiffPane::LastTop() const {
    int rows = (int)ed.layout.size();
    if (rows <= 0 || (int)ed.row_y.size() != rows + 1) return 0;
    // the first row from which the rest fits
    auto it = std::lower_bound(ed.row_y.begin(), ed.row_y.begin() + rows, ed.row_y[rows] - Geo().height);
    return std::clamp(int(it - ed.row_y.begin()), 0, rows - 1);
}

void DiffPane::UpdateBars() {
    int rows = (int)ed.layout.size(), last = LastTop();
    top = std::clamp(top, 0, last);
    xoff = 0;
    // The bar's range is the rows, its thumb what is left once the last can be the top.
    int thumb = std::max(1, rows - last);
    vbar->SetScrollbar(top, thumb, std::max(rows, 1), std::max(1, VisibleRows() - 1));
}

void DiffPane::SetView(int new_top, int new_x, bool notify) {
    int old_top = top, old_x = xoff;
    top = new_top; xoff = new_x;
    (void)old_x;
    UpdateBars();
    if (top != old_top || xoff != old_x) {
        Redraw();
        if (notify) ed.Scrolled(side);
    }
}

void DiffPane::Reveal(int first, int last) {
    int rows = (int)ed.layout.size();
    if (first < 0 || first >= rows) return;
    last = std::clamp(last, first, rows - 1);
    int h = Geo().height;
    auto fits = [&](int t) { return t <= first && ed.RowY(last + 1) - ed.RowY(t) <= h; };
    if (fits(top)) return;
    if (ed.RowY(last + 1) - ed.RowY(first) >= h) { SetView(first, xoff); return; }
    // A third of the view above it where there is room.
    int t = first;
    while (t > 0 && ed.RowY(first) - ed.RowY(t - 1) <= h / 3) --t;
    while (!fits(t)) ++t;
    SetView(t, xoff);
}

void DiffPane::OnSize(wxSizeEvent&) {
    // Wrapped lines take as many lines as the new width leaves them.
    ed.MeasureRows();
    UpdateBars();
    Redraw();
}

void DiffPane::Redraw() {
    Refresh(false);
    if (header) header->Refresh(false);
    ed.RefreshGutter();
    ed.PlaceCard();
}

void DiffPane::Wheel(wxMouseEvent& event) {
    // Lines wrap: nothing to scroll sideways.
    if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL) return;
    bool zoom = event.ControlDown();
    int turned = event.GetWheelRotation();
    int& rest = zoom ? zoom_rest : wheel_rest;
    if (rest && (rest > 0) != (turned > 0)) rest = 0; // turned back: start afresh
    rest += turned;
    int delta = std::max(1, event.GetWheelDelta());
    int steps = rest / delta;
    if (!steps) return;
    rest -= steps * delta;
    if (zoom) { ed.Zoom(steps); return; }
    int lines = steps * std::max(1, event.GetLinesPerAction());
    SetView(top - lines, xoff, true);
}

void DiffPane::OnScroll(wxScrollEvent&) {
    SetView(vbar->GetThumbPosition(), xoff, true);
}

int DiffPane::RowAt(int y) const {
    int rows = (int)ed.layout.size();
    if (y < 0 || y >= Geo().height || (int)ed.row_y.size() != rows + 1) return -1;
    auto it = std::upper_bound(ed.row_y.begin(), ed.row_y.end(), ed.RowY(top) + y);
    int row = int(it - ed.row_y.begin()) - 1;
    return row >= 0 && row < rows ? row : -1;
}

Part DiffPane::PartAt(int row, wxPoint pt) const {
    auto g = Geo();
    if (pt.x >= g.overview_x && pt.x < g.overview_x + ed.overview_w && pt.y >= 0 && pt.y < g.height) return Part::Overview;
    if (row < 0) return Part::None;
    auto const& lr = ed.layout[row];
    if (lr.IsFold()) return Part::Fold;
    // The foot's buttons are windows of their own; between them is nothing.
    if (lr.IsFoot()) return Part::None;
    return Part::Content;
}

void DiffPane::OnMouse(wxMouseEvent& event) {
    auto client_pt = event.GetPosition();
    auto pt = client_pt - wxPoint(RowsX(), 0);
    auto g = Geo();
    if (event.GetEventType() == wxEVT_MOUSEWHEEL) { Wheel(event); return; }
    if (event.GetEventType() == wxEVT_LEAVE_WINDOW) {
        if (hover_part != Part::None) { hover_part = Part::None; Refresh(false); }
        ed.SetHot(-1);
        return;
    }
    // The track: dragging the thumb keeps hold where it was taken; a click
    // elsewhere centres the view there and holds the thumb by its middle.
    int rows_n = (int)ed.layout.size();
    double scale = rows_n && g.height > 0 ? double(g.height) / std::max(rows_n, VisibleRows()) : 0;
    int thumb_y = int(top * scale), thumb_h = std::max(FromDIP(16), int(VisibleRows() * scale));
    auto overview_jump = [&] {
        if (!rows_n || scale <= 0) return;
        SetView(int((pt.y - drag_offset) / scale + 0.5), xoff, true);
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
        // Over a simplified block: what its tags do.
        auto block = help.empty() && part == Part::Content ? BlockAt(row, pt) : nullptr;
        if (row != tip_row || tip != tip_part || block != tip_block) {
            tip_row = row; tip_part = tip; tip_block = block;
            if (!help.empty()) SetToolTip(help);
            else if (block) SetToolTip(tag_summary::Describe(*block));
            else if (row >= 0 && !ed.layout[row].IsFold()) SetToolTip(ed.Tooltip(row, side));
            else UnsetToolTip();
        }
        return;
    }
    if (event.GetEventType() == wxEVT_LEFT_DOWN || event.GetEventType() == wxEVT_LEFT_DCLICK) {
        SetFocus();
        if (part == Part::Overview) {
            dragging_overview = true;
            if (!HasCapture()) CaptureMouse();
            bool on_thumb = pt.y >= thumb_y && pt.y < thumb_y + thumb_h;
            drag_offset = on_thumb ? pt.y - thumb_y : int(VisibleRows() * scale / 2);
            if (!on_thumb) overview_jump();
            return;
        }
        if (row < 0) return;
        auto const& lr = ed.layout[row];
        ed.cursor_row = row;
        if (part == Part::Fold) ed.ExpandFold(row);
        else if (lr.IsDetail()) ed.SetFieldCursor(*lr.field);
        else if (!lr.IsFoot()) ed.Select(lr.entry, side);
    }
    if (event.GetEventType() == wxEVT_RIGHT_DOWN) {
        SetFocus();
        if (row < 0 || ed.layout[row].IsFold()) return;
        auto entry = ed.layout[row].entry;
        if (!ed.ShownDiff(entry)) return;
        if (!ed.layout[row].InCard()) ed.Select(entry, side);
        ed.ContextMenu(this, entry, client_pt);
    }
}

void DiffPane::OnKey(wxKeyEvent& event) {
    int vis = VisibleRows();
    if (event.GetKeyCode() == WXK_TAB && !event.ControlDown() && !event.AltDown()) {
        Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
        return;
    }
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
    if (event.HasAnyModifiers()) { event.Skip(); return; }
    // Up and down move the keyboard's line, the view following it.
    switch (event.GetKeyCode()) {
        case WXK_UP: ed.MoveCursor(-1, side); break;
        case WXK_DOWN: ed.MoveCursor(1, side); break;
        case WXK_PAGEUP: ed.MoveCursor(-vis, side); break;
        case WXK_PAGEDOWN: ed.MoveCursor(vis, side); break;
        case WXK_HOME: ed.PlaceCursor(0, side); break;
        case WXK_END: ed.PlaceCursor((int)ed.layout.size() - 1, side); break;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER: ed.ActivateCursor(side); break;
        case WXK_SPACE: ed.CheckCursor(); break;
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
    {
        // Rows stop where the scroll bar starts: one that floats over the
        // pane (GTK's overlay bars) must not cover a row's text.
        wxDCClipper rows_clip(dc, wxRect(0, 0, g.width, g.height));
        for (int row = top, y = 0; row < rows && y < g.height; y += ed.RowH(row), ++row) {
            DrawRow(dc, g, row, y);
            if (row == ed.cursor_row) DrawCursor(dc, g, row, y);
        }
    }
    DrawOverview(dc, g);
    // No frame round the pane: the focus is the keyboard's line, and a
    // stroke of the accent along the top while the line is out of view.
    if (HasFocus() && (ed.cursor_row < top || ed.cursor_row >= top + VisibleRows())) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(p.accent));
        dc.DrawRectangle(0, 0, g.content_right, 2);
    }
}

void DiffPane::DrawCursor(wxDC& dc, Geometry const& g, int row, int y) {
    // The keyboard's line, in the focused pane only: one frame on the page.
    if (!HasFocus()) return;
    dc.SetPen(wxPen(ed.pal.accent));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(g.content_x + 1, y, std::max(0, g.content_right - g.content_x - 2), ed.RowH(row));
}

void DiffPane::DrawRow(wxDC& dc, Geometry const& g, int row, int y) {
    auto const& p = ed.pal;
    auto const& lr = ed.layout[row];
    int rh = ed.RowH(row);
    wxDCClipper clip(dc, wxRect(0, y, g.width, rh));
    bool hot = row == ed.hot;
    if (lr.IsFold()) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(hot ? mix(p.fold_bg, p.fold_text, 0.12) : p.fold_bg));
        dc.DrawRectangle(0, y, g.width, ed.row_h);
        dc.SetTextForeground(p.fold_text);
        auto label = wxString::FromUTF8("⋯  ") + fmt_tl("%d hidden lines", (int)lr.hidden) + wxString::FromUTF8("  ·  ") + _("click or Enter to show");
        auto extent = dc.GetTextExtent(label);
        dc.DrawText(label, g.content_x + 8, y + (ed.row_h - extent.GetHeight()) / 2);
        return;
    }
    if (lr.IsDetail()) { DrawDetail(dc, g, row, y); return; }
    if (lr.IsFoot()) { DrawFoot(dc, g, row, y); return; }
    size_t entry = lr.entry;
    auto const& line = ed.state.Entries()[entry].Get(side);
    bool is_current = ed.IsCurrent(entry);
    bool diff = ed.ShownDiff(entry);

    // background: the line colour of the difference, hatched where this side
    // is empty; the current difference is the head of its card
    if (is_current) DrawCard(dc, g, row, y);
    else {
        wxColour bg = diff ? ed.RowColour(entry, side) : hot ? p.hover : p.bg;
        if (diff && hot) bg = mix(bg, p.text, 0.04);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(bg));
        dc.DrawRectangle(0, y, g.width, rh);
    }
    if (!line) {
        // within the card's face on the current one
        int x0 = is_current && side == Side::Left ? ed.card_inset + 1 : g.content_x;
        int x1 = is_current && side == Side::Right ? g.width - ed.card_inset - 1 : g.content_right;
        int y0 = is_current ? y + 3 : y;
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(p.hatch, wxBRUSHSTYLE_BDIAGONAL_HATCH));
        dc.DrawRectangle(x0, y0, x1 - x0, y + rh - y0);
    }
    if (line) DrawLine(dc, g, entry, y);
    // edges of each run of added or removed lines, carried on by the gutter's
    // bands; modified lines have no background for an edge to close
    if (diff && !is_current && ed.state.Entries()[entry].change != Change::Modified) {
        dc.SetPen(wxPen(ed.EdgeColour(entry)));
        if (ed.ChunkStart(row)) dc.DrawLine(0, y, g.width, y);
        if (ed.ChunkEnd(row)) dc.DrawLine(0, y + rh - 1, g.width, y + rh - 1);
    }
}

namespace {
/// Bytes in the UTF-8 sequence that starts with @p lead.
size_t utf8_length(unsigned char lead) {
    return lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
}
bool word_char(char c) { return (unsigned char)c < 0x80 && std::isalnum((unsigned char)c); }
}

std::vector<DiffPane::TextPiece> DiffPane::LayText(wxDC& dc, size_t entry, int x, bool highlight, bool full, int wrap_right) const {
    auto const& e = ed.state.Entries()[entry];
    auto const& text = e.Get(side)->Value(model::Text);
    auto const& spans = side == Side::Left ? e.text.left : e.text.right;
    size_t limit = std::min(text.size(), size_t(4000));
    auto touched = [&](size_t a, size_t b) {
        if (!highlight) return false;
        for (auto const& span : spans)
            if (span.begin < b && span.end > a) return true;
        return false;
    };
    auto shown = [&](size_t a, size_t b) {
        auto part = to_wx(text.substr(a, b - a));
        part.Replace("\n", " ");
        return part;
    };
    auto run_width = [&](size_t a, size_t b) { return dc.GetTextExtent(shown(a, b)).GetWidth(); };
    std::vector<TextPiece> out;
    // Wrapping: the next line starts where the text does. Too narrow a
    // column is not wrapped at all (its end is cut, as before).
    int line_left = x, line = 0;
    bool wrap = wrap_right > line_left + ed.char_w * 4, break_next = false;
    auto new_line = [&] { ++line; x = line_left; break_next = false; };
    // Onto the next line if @p w after @p lead goes past the end, or after a \N.
    auto fit = [&](int lead, int w) {
        if (!wrap || x <= line_left) { break_next = false; return; }
        if (break_next || x + lead + w > wrap_right) new_line();
    };
    // A run of the text, broken where it reaches the end (after the last
    // space when that is inside a word; anywhere between CJK characters),
    // and after each \N.
    auto add_run = [&](size_t a, size_t b) {
        while (a < b) {
            if (wrap && break_next && x > line_left) new_line();
            break_next = false;
            size_t end = b;
            if (wrap) {
                auto hard = text.find("\\N", a);
                if (hard != std::string::npos && hard + 2 <= b) end = hard + 2;
                auto part = shown(a, end);
                wxArrayInt widths;
                dc.GetPartialTextExtents(part, widths);
                int avail = wrap_right - x;
                if (!widths.empty() && widths.Last() > avail) {
                    size_t fit_end = a, unit = 0;
                    for (size_t i = a; i < end;) {
                        size_t n = utf8_length(text[i]);
                        // a wxString unit each, two for one past the BMP where wchar_t is 16 bits
                        size_t units = sizeof(wchar_t) == 2 && n == 4 ? 2 : 1;
                        if (unit + units > widths.size() || widths[unit + units - 1] > avail) break;
                        unit += units; i += n; fit_end = i;
                    }
                    if (fit_end == a) {
                        if (x > line_left) { new_line(); continue; }
                        fit_end = std::min(end, a + utf8_length(text[a])); // not even one fits: it goes alone
                    }
                    else if (fit_end < end && word_char(text[fit_end]) && word_char(text[fit_end - 1])) {
                        auto space = text.rfind(' ', fit_end - 1);
                        if (space != std::string::npos && space >= a) fit_end = space + 1;
                        else if (x > line_left) { new_line(); continue; } // the word from the next line
                    }
                    end = fit_end;
                }
            }
            TextPiece tp;
            tp.begin = a; tp.end = end;
            tp.left = x; tp.right = x + run_width(a, end);
            tp.changed = touched(a, end);
            tp.line = line;
            out.push_back(tp);
            x = tp.right;
            bool hard_break = end >= a + 2 && text.compare(end - 2, 2, "\\N") == 0;
            a = end;
            if (wrap && a < b && !hard_break) new_line();
            else if (wrap && hard_break) break_next = true;
        }
    };
    int mode = full ? 0 : ed.TagMode();
    if (mode == 0) {
        if (wrap) add_run(0, limit);
        else {
            TextPiece run;
            run.end = limit;
            run.left = x;
            run.right = x + run_width(0, limit);
            run.changed = touched(0, limit);
            out.push_back(run);
        }
        return out;
    }
    tag_chips::Metrics m(dc);
    // A block ending a drawing has no piece: nothing is drawn for its bytes.
    for (auto const& piece : ed.Summary(text)) {
        if (piece.begin >= limit) break;
        TextPiece tp;
        tp.begin = piece.begin;
        tp.end = std::min(piece.end, limit);
        tp.changed = touched(piece.begin, piece.end);
        if (piece.block) {
            tp.block = &piece;
            if (mode == 2) {
                // Hidden, but a changed block keeps a mark where it was.
                tp.kind = TextPiece::Gap;
                int w = tp.changed ? 4 : 0;
                if (w) fit(0, w);
                tp.left = x;
                tp.right = x + w;
            }
            else {
                tp.kind = TextPiece::Chip;
                int icons = std::max(1, ed.chips_painter.Width(dc, m, piece));
                // A changed block says what changed in it after its icons.
                if (tp.changed) tp.values = ChangedValues(entry, piece);
                int values = 0;
                if (!tp.values.empty()) {
                    dc.SetFont(ed.small_font);
                    values = 4 + dc.GetTextExtent(tp.values).GetWidth() + 4;
                    dc.SetFont(GetFont());
                }
                fit(m.spacing / 2, icons + values + m.spacing / 2);
                tp.left = x + m.spacing / 2;
                tp.right = tp.left + icons;
                if (values) { tp.values_left = tp.right + 4; tp.right += values; }
            }
            tp.line = line;
            x = tp.right + (tp.kind == TextPiece::Chip ? m.spacing / 2 : 0);
            out.push_back(std::move(tp));
        }
        else if (piece.drawing_points >= 0) {
            tp.kind = TextPiece::Label;
            tp.label = tag_summary::PlainText(piece);
            int w = dc.GetTextExtent(tp.label).GetWidth();
            fit(0, w);
            tp.left = x;
            tp.right = x + w;
            tp.line = line;
            x = tp.right;
            out.push_back(std::move(tp));
        }
        else if (wrap) add_run(tp.begin, tp.end);
        else {
            tp.left = x;
            tp.right = x + run_width(tp.begin, tp.end);
            x = tp.right;
            out.push_back(std::move(tp));
        }
    }
    return out;
}

namespace {
/// A tag's value as written after its name: "\pos(640,60)" gives "640,60",
/// "\fs40" "40", "\1c&H00FFFF&" "&H00FFFF&".
wxString tag_value(std::string const& tag) {
    size_t i = tag.starts_with('\\') ? 1 : 0;
    if (i < tag.size() && std::isdigit((unsigned char)tag[i])) ++i;
    while (i < tag.size() && std::isalpha((unsigned char)tag[i])) ++i;
    auto value = tag.substr(i);
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    if (value.size() >= 2 && value.front() == '(' && value.back() == ')') value = value.substr(1, value.size() - 2);
    auto out = to_wx(value);
    // A clip's or a drawing's points are no use in a line's width.
    if (out.size() > 18) out = out.Left(17) + wxString::FromUTF8("…");
    return out;
}
}

wxString DiffPane::ChangedValues(size_t entry, tag_summary::Piece const& block) const {
    auto const& e = ed.state.Entries()[entry];
    auto const& other_line = e.Get(view::Other(side));
    if (!e.Get(side) || !other_line) return {};
    auto blocks = [](std::vector<tag_summary::Piece> const& pieces) {
        std::vector<tag_summary::Piece const*> out;
        for (auto const& piece : pieces) if (piece.block) out.push_back(&piece);
        return out;
    };
    auto mine = blocks(ed.Summary(e.Get(side)->Value(model::Text)));
    auto theirs = blocks(ed.Summary(other_line->Value(model::Text)));
    // Blocks pair up in order, only while both sides have as many.
    if (mine.size() != theirs.size()) return {};
    auto at = std::find(mine.begin(), mine.end(), &block);
    if (at == mine.end()) return {};
    auto const& other = *theirs[at - mine.begin()];
    wxString out;
    for (auto const& mark : block.marks) {
        // A colour's swatch already shows what it changed to.
        if (mark.tags.empty() || mark.kind == tag_summary::Kind::Colour) continue;
        auto same = std::find_if(other.marks.begin(), other.marks.end(), [&](tag_summary::Mark const& m) {
            return m.kind == mark.kind && m.index == mark.index && m.tags == mark.tags;
        });
        if (same != other.marks.end()) continue;
        auto value = tag_value(mark.tags.back());
        if (value.empty()) continue;
        if (!out.empty()) out += wxString::FromUTF8(" · ");
        out += value;
    }
    return out;
}

int DiffPane::DrawText(wxDC& dc, size_t entry, int x, int y0, bool highlight, bool lost, bool full, int wrap_right, int* end_line) {
    auto const& p = ed.pal;
    auto const& e = ed.state.Entries()[entry];
    auto const& text = e.Get(side)->Value(model::Text);
    auto const& spans = side == Side::Left ? e.text.left : e.text.right;
    wxColour hl = side == Side::Right ? p.ins_char : p.del_char;
    wxColour mark_bg = lost ? mix(hl, p.bg, 0.5) : hl;
    auto pieces = LayText(dc, entry, x, highlight, full, wrap_right);
    int rh = ed.row_h;
    for (auto const& tp : pieces) {
        // each wrapped line as tall as a row of one line, a pitch apart
        int y = y0 + tp.line * ed.line_pitch;
        int text_y = y + (rh - dc.GetCharHeight()) / 2;
        auto strike = [&](int x0, int x1) {
            if (!lost || x1 <= x0) return;
            dc.SetPen(wxPen(p.dim));
            dc.DrawLine(x0, y + rh / 2, x1, y + rh / 2);
        };
        if (tp.kind == TextPiece::Run) {
            // The text itself, its changed tokens on the highlight colour.
            int px = tp.left;
            auto draw = [&](size_t a, size_t b, bool mark) {
                if (a >= b) return;
                auto part = to_wx(text.substr(a, b - a));
                part.Replace("\n", " ");
                int w = dc.GetTextExtent(part).GetWidth();
                if (mark) {
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(mark_bg));
                    dc.DrawRectangle(px, y + 3, std::max(w, 3), rh - 6);
                }
                dc.SetTextForeground(lost ? p.dim : p.text);
                dc.wxDC::DrawText(part, px, text_y);
                px += w;
            };
            size_t pos = tp.begin;
            if (highlight) {
                for (auto const& span : spans) {
                    size_t a = std::clamp(span.begin, tp.begin, tp.end), b = std::clamp(span.end, tp.begin, tp.end);
                    if (a >= b) continue;
                    draw(pos, a, false);
                    draw(a, b, true);
                    pos = b;
                }
            }
            draw(pos, tp.end, false);
            strike(tp.left, tp.right);
            continue;
        }
        if (tp.changed && !tp.values.empty()) {
            // A changed block whose changes can be told: its icons as they
            // are, framed in the modified colour, then what changed on
            // this side's highlight.
            tag_chips::Metrics m(dc);
            wxColour back = ed.RowColour(entry, side);
            int chip_y = y + (rh - m.height) / 2 - 1;
            int icons_right = tp.values_left - 4;
            dc.SetTextForeground(lost ? p.dim : p.text);
            ed.chips_painter.Draw(dc, *tp.block, tp.left, icons_right, chip_y, m, back);
            flat::DrawOutline(dc, wxRect(tp.left - 2, y + 3, tp.right - tp.left + 4, rh - 6), 3, 1, mix(p.mod_mark, back, lost ? 0.6 : 0.2));
            dc.SetFont(ed.small_font);
            int vw = dc.GetTextExtent(tp.values).GetWidth();
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(mark_bg));
            dc.DrawRoundedRectangle(tp.values_left - 2, y + 6, vw + 4, rh - 12, 2);
            dc.SetTextForeground(lost ? p.dim : p.text);
            dc.wxDC::DrawText(tp.values, tp.values_left, y + (rh - dc.GetCharHeight()) / 2);
            dc.SetFont(GetFont());
            continue;
        }
        if (tp.changed) {
            // A changed block (or drawing) is marked as a whole.
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(mark_bg));
            if (tp.kind == TextPiece::Gap) dc.DrawRectangle(tp.left + 1, y + 3, 3, rh - 6);
            else dc.DrawRoundedRectangle(tp.left - 2, y + 3, tp.right - tp.left + 4, rh - 6, 3);
        }
        if (tp.kind == TextPiece::Label) {
            dc.SetTextForeground(lost ? p.dim : mix(p.text, p.bg, 0.4));
            dc.wxDC::DrawText(tp.label, tp.left, text_y);
            strike(tp.left, tp.right);
        }
        else if (tp.kind == TextPiece::Chip) {
            tag_chips::Metrics m(dc);
            wxColour back = tp.changed ? mark_bg : ed.ShownDiff(entry) ? ed.RowColour(entry, side) : p.bg;
            dc.SetTextForeground(lost ? p.dim : p.text);
            ed.chips_painter.Draw(dc, *tp.block, tp.left, tp.right, y + (rh - m.height) / 2 - 1, m, back);
        }
    }
    if (end_line) *end_line = pieces.empty() ? 0 : pieces.back().line;
    return pieces.empty() ? x : pieces.back().right;
}

int DiffPane::TextX(Geometry const& g) const {
    int time_w = ed.time_w + (side == Side::Right ? ed.delta_w : 0);
    auto [style_w, actor_w] = LabelColumns();
    return g.content_x + 6 - xoff + time_w * 2 + style_w + actor_w;
}

std::pair<int, int> DiffPane::LabelColumns() const {
    auto g = Geo();
    int time_w = ed.time_w + (side == Side::Right ? ed.delta_w : 0);
    int room = g.content_right - g.content_x - 6 - time_w * 2;
    int text_min = std::max(ed.char_w * 16, room * 45 / 100);
    int style_w = ed.style_w, actor_w = ed.actor_w, over = style_w + actor_w - (room - text_min);
    if (over <= 0 || style_w + actor_w == 0) return {style_w, actor_w};
    // shared between the two as they are wide, each kept to a few characters
    int least = ed.char_w * 6 + 16;
    int cut_s = style_w ? std::min(style_w - std::min(style_w, least), over * style_w / (style_w + actor_w)) : 0;
    int cut_a = actor_w ? std::min(actor_w - std::min(actor_w, least), over - cut_s) : 0;
    return {style_w - cut_s, actor_w - cut_a};
}

int DiffPane::TextRight(Geometry const& g) const {
    // short of the pane's end, and of the current difference's card there
    return g.content_right - (side == Side::Right ? ed.card_inset : 0) - 8;
}

int DiffPane::DetailX(Geometry const& g) const { return g.content_x + 6 - xoff + ed.label_w; }

std::vector<DiffPane::Trail> DiffPane::Trailing(size_t entry) const {
    // The other changed fields as one quiet chip: the field and its value when
    // it is the only one, otherwise how many (the tooltip and the detail rows
    // list them).
    std::vector<Trail> out;
    auto const& e = ed.state.Entries()[entry];
    auto const& line = *e.Get(side);
    model::Fields shown = ed.ShownFields(entry);
    bool diff = ed.ShownDiff(entry);
    auto lost = [&](model::Field f) {
        if (!diff) return false;
        auto winner = ed.state.Winner(entry, f);
        return winner && *winner != side && (e.change != Change::Modified || (shown & f));
    };
    std::vector<model::Field> others;
    for (auto f : view::FieldOrder())
        if (!is_column(f) && (shown & f)) others.push_back(f);
    bool all_lost = !others.empty() && std::all_of(others.begin(), others.end(), lost);
    if (others.size() == 1) {
        auto f = others[0];
        wxString label = f == model::Comment ? (line.comment ? _("Comment") : _("Not a comment"))
            : field_label(f) + ": " + one_line(line.Value(f), 60);
        out.push_back({label, all_lost, all_lost});
    }
    else if (others.size() > 1) out.push_back({fmt_tl("+%d fields", (int)others.size()), all_lost, all_lost});
    if (line.comment && !(shown & model::Comment)) out.push_back({_("Comment"), true, false});
    return out;
}

int DiffPane::PlaceTrailing(wxDC& dc, std::vector<Trail> const& chips, int x, int line, int left, int right,
    std::function<void(Trail const&, int, int, int)> const& place) const
{
    dc.SetFont(ed.small_font);
    bool wrap = right > left + ed.char_w * 4;
    for (auto const& chip : chips) {
        int w = dc.GetTextExtent(chip.label).GetWidth() + 10;
        x += 8;
        if (wrap && x > left + 8 && x + w > right) { ++line; x = left; }
        if (place) place(chip, x, line, w);
        x += w;
    }
    dc.SetFont(GetFont());
    return line;
}

int DiffPane::RowLines(wxDC& dc, int row) const {
    auto const& lr = ed.layout[row];
    if (lr.IsFold() || lr.IsFoot() || !ed.state.Entries()[lr.entry].Get(side)) return 1;
    auto g = Geo();
    if (lr.IsDetail()) {
        if (*lr.field != model::Text) return 1;
        auto pieces = LayText(dc, lr.entry, DetailX(g), true, true, TextRight(g));
        return pieces.empty() ? 1 : pieces.back().line + 1;
    }
    int text_x = TextX(g), right = TextRight(g);
    auto pieces = LayText(dc, lr.entry, text_x, ed.ShownFields(lr.entry) & model::Text, false, right);
    int x = pieces.empty() ? text_x : pieces.back().right, line = pieces.empty() ? 0 : pieces.back().line;
    return PlaceTrailing(dc, Trailing(lr.entry), x, line, text_x, right, nullptr) + 1;
}

const tag_summary::Piece* DiffPane::BlockAt(int row, wxPoint pt) {
    if (ed.TagMode() != 1 || row < 0 || row >= (int)ed.layout.size()) return nullptr;
    auto const& lr = ed.layout[row];
    if (lr.IsFold() || lr.InCard() || !ed.state.Entries()[lr.entry].Get(side)) return nullptr;
    auto g = Geo();
    if (pt.x < g.content_x || pt.x >= g.content_right) return nullptr;
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    bool highlight = ed.ShownFields(lr.entry) & model::Text;
    for (auto const& tp : LayText(dc, lr.entry, TextX(g), highlight, false))
        if (tp.kind == TextPiece::Chip && pt.x >= tp.left - 1 && pt.x < tp.right + 1) return tp.block;
    return nullptr;
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

int DiffPane::DrawLine(wxDC& dc, Geometry const& g, size_t entry, int y) {
    auto const& p = ed.pal;
    auto const& e = ed.state.Entries()[entry];
    auto const& line = *e.Get(side);
    int row_h_all = ed.RowH(ed.row_of[entry]);
    wxDCClipper clip(dc, wxRect(g.content_x, y, std::max(0, g.content_right - g.content_x), row_h_all));
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
        if (ed.short_times && (f == model::Start || f == model::End) && value.StartsWith("0:")) value = value.Mid(2);
        bool changed = shown & f;
        {
            wxDCClipper inner(dc, wxRect(x, y, std::max(0, width - 4), ed.row_h));
            dc.SetFont(changed ? ed.bold_font : GetFont());
            // a style or an actor cut short where its column is narrowed
            if (f == model::Style || f == model::Actor) value = wxControl::Ellipsize(value, dc, wxELLIPSIZE_END, std::max(0, width - 10));
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
    auto [style_w, actor_w] = LabelColumns();
    if (style_w) cell(model::Style, style_w);
    if (actor_w) cell(model::Actor, actor_w);

    // The text, wrapping onto lines of its own under it.
    int text_x = x, right = TextRight(g), text_line = 0;
    x = DrawText(dc, entry, x, y, shown & model::Text, lost(model::Text), false, right, &text_line);

    // Then its other changed fields and its being a comment, as quiet chips.
    int end = x;
    PlaceTrailing(dc, Trailing(entry), x, text_line, text_x, right, [&](Trail const& t, int cx, int line_no, int w) {
        int chip_y = y + line_no * ed.line_pitch + 4, chip_h = ed.row_h - 8;
        dc.SetFont(ed.small_font);
        dc.SetPen(wxPen(p.border));
        dc.SetBrush(wxBrush(p.gutter));
        dc.DrawRoundedRectangle(cx, chip_y, w, chip_h, 3);
        dc.SetTextForeground(t.struck ? p.dim : t.quiet && !t.struck ? p.gutter_text : p.text);
        dc.wxDC::DrawText(t.label, cx + 5, chip_y + (chip_h - dc.GetCharHeight()) / 2);
        if (t.struck) { dc.SetPen(wxPen(p.dim)); dc.DrawLine(cx + 3, chip_y + chip_h / 2, cx + w - 3, chip_y + chip_h / 2); }
        end = cx + w;
    });
    dc.SetFont(GetFont());
    return end;
}

void DiffPane::DrawCard(wxDC& dc, Geometry const& g, int row, int y) {
    // The card stands off the page by a shadow, in from the pane's outer
    // edge and on over its inner one into the gutter, which carries it on.
    auto const& p = ed.pal;
    auto [first, last] = ed.CardRows();
    int rh = ed.RowH(row), r = ed.card_radius;
    int y0 = y - (ed.RowY(row) - ed.RowY(first)) + 2, y1 = y + (ed.RowY(last + 1) - ed.RowY(row)) - 2;
    int over = r + 4; // past the inner edge, so no corner shows there
    wxRect card = side == Side::Left ? wxRect(ed.card_inset, y0, g.width - ed.card_inset + over, y1 - y0)
        : wxRect(-over, y0, g.width - ed.card_inset + over, y1 - y0);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(p.bg));
    dc.DrawRectangle(0, y, g.width, rh);
    dc.SetBrush(wxBrush(mix(p.bg, *wxBLACK, theme::IsDark() ? 0.4 : 0.07)));
    dc.DrawRoundedRectangle(card.x, card.y + 2, card.width, card.height, r);
    dc.SetPen(wxPen(p.border));
    dc.SetBrush(wxBrush(p.bg));
    dc.DrawRoundedRectangle(card, r);
    if (row == first) {
        // the line itself, in its own colour, under the card's top edge
        wxDCClipper head(dc, wxRect(0, y, g.width, rh));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(ed.RowColour(ed.layout[row].entry, side)));
        dc.DrawRoundedRectangle(card.x + 1, card.y + 1, card.width - 2, rh + r, r - 1);
    }
}

void DiffPane::DrawDetail(wxDC& dc, Geometry const& g, int row, int y) {
    // One changed field of the current difference, in its card: the field's
    // name, then this side's value in full (the gutter's arrows choose a side).
    auto const& p = ed.pal;
    auto const& lr = ed.layout[row];
    size_t entry = lr.entry;
    auto f = *lr.field;
    auto const& e = ed.state.Entries()[entry];
    auto const& line = *e.Get(side);
    DrawCard(dc, g, row, y);

    auto winner = ed.state.Winner(entry, f);
    bool lost = winner && *winner != side;
    {
        int inner = side == Side::Left ? ed.card_inset + 1 : 0;
        int inner_right = side == Side::Right ? g.width - ed.card_inset - 1 : g.width;
        wxDCClipper clip(dc, wxRect(inner, y, std::max(0, inner_right - inner), ed.RowH(row)));
        int x = g.content_x + 6 - xoff;
        int text_y = y + (ed.row_h - dc.GetCharHeight()) / 2;
        dc.SetFont(ed.small_font);
        dc.SetTextForeground(p.gutter_text);
        auto label = field_label(f);
        dc.wxDC::DrawText(label, x, y + (ed.row_h - dc.GetTextExtent(label).GetHeight()) / 2);
        dc.SetFont(GetFont());
        x += ed.label_w;
        if (f == model::Text) {
            // In full: the tags themselves are what is told apart here.
            DrawText(dc, entry, x, y, true, lost, true, TextRight(g));
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
    if (ed.field_cursor == f && HasFocus()) {
        // the keyboard's field, framed in the focused pane only
        int x0 = side == Side::Left ? ed.card_inset + 3 : 2;
        int x1 = side == Side::Right ? g.width - ed.card_inset - 3 : g.width - 2;
        dc.SetPen(wxPen(p.accent, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(x0, y + 2, x1 - x0, ed.RowH(row) - 4, 3);
    }
}

void DiffPane::DrawFoot(wxDC& dc, Geometry const& g, int row, int y) {
    // Under a hairline: on the left what is decided, beside the button that
    // takes the left side; on the right the button that keeps the right
    // side, the reset, and the keys that do the same (ComparePanel::hint).
    auto const& p = ed.pal;
    DrawCard(dc, g, row, y);
    int x0 = side == Side::Left ? ed.card_inset + 10 : 10;
    int x1 = side == Side::Right ? g.width - ed.card_inset - 10 : g.width - 10;
    dc.SetPen(wxPen(mix(p.border, p.bg, 0.4)));
    dc.DrawLine(side == Side::Left ? x0 : 0, y, side == Side::Right ? x1 : g.width, y);
    wxString text;
    int from = x0, to = x1;
    if (side == Side::Left) {
        text = ed.CardStatus();
        to = std::min(to, ed.reject_current->GetPosition().x - RowsX() - 10);
    }
    if (text.empty() || to - from < 20) return;
    dc.SetFont(ed.small_font);
    dc.SetTextForeground(p.text);
    text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, to - from);
    dc.wxDC::DrawText(text, from, y + (ed.row_h - 2 - dc.GetCharHeight()) / 2);
    dc.SetFont(GetFont());
}

void DiffPane::DrawOverview(wxDC& dc, Geometry const& g) {
    // The track: what is in view as the thumb, and every difference as a
    // tick in the colour of its change, fainter once decided. On the left
    // pane it is drawn before the rows, outside the device origin's shift.
    auto const& p = ed.pal;
    int x = g.overview_x, w = ed.overview_w;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(mix(p.bg, p.text, 0.03)));
    dc.DrawRectangle(x, 0, w, g.height);
    int rows = (int)ed.layout.size();
    if (!rows || g.height <= 0) return;
    double scale = double(g.height) / std::max(rows, VisibleRows());
    bool hot = hover_part == Part::Overview || dragging_overview;
    int thumb_y = int(top * scale), thumb_h = std::max(FromDIP(16), int(VisibleRows() * scale));
    if (rows > VisibleRows()) {
        dc.SetBrush(wxBrush(mix(p.bg, p.text, hot ? 0.28 : 0.16)));
        dc.DrawRoundedRectangle(x + 3, thumb_y, w - 6, std::min(thumb_h, g.height - thumb_y), (w - 6) / 2.0);
    }
    int mark_h = std::max(2, (int)scale);
    for (int r = 0; r < rows; ++r) {
        auto const& lr = ed.layout[r];
        if (lr.IsFold() || lr.InCard() || !ed.ShownDiff(lr.entry)) continue;
        auto const& e = ed.state.Entries()[lr.entry];
        wxColour c = e.change == Change::Modified ? p.mod_mark : e.change == Change::Inserted ? p.ins_mark : p.del_mark;
        if (ed.state.GetDecision(lr.entry) != Decision::Pending) c = mix(c, p.bg, 0.6);
        dc.SetBrush(wxBrush(c));
        // On the outer half, so the thumb stays readable over them.
        int mx = side == Side::Right ? x + w / 2 : x + 1;
        dc.DrawRectangle(mx, int(r * scale), w / 2 - 1, mark_h);
    }
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
    Bind(wxEVT_MOUSEWHEEL, [pane](wxMouseEvent& e) { pane->Wheel(e); });
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
    dc.SetBackground(wxBrush(p.bg));
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
    auto [style_w, actor_w] = pane->LabelColumns();
    if (style_w) column(model::Style, style_w);
    if (actor_w) column(model::Actor, actor_w);
    column(model::Text, 0);
    // The columns left out, at the end of the names, where the text column
    // leaves room for it.
    if (!ed.same_note.empty()) {
        int w = dc.GetTextExtent(ed.same_note).GetWidth();
        int nx = pane->ColumnsRight() - w - 8;
        if (nx > x + dc.GetTextExtent(field_label(model::Text)).GetWidth() + 24) {
            dc.SetTextForeground(mix(p.gutter_text, p.bg, 0.25));
            dc.DrawText(ed.same_note, nx, y);
        }
    }
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

int DiffGutter::Width() const { return ed.number_w * 2 + ed.band_w; }

DiffGutter::Columns DiffGutter::Cols() const {
    Columns c;
    c.left_number = 0;
    c.band = ed.number_w;
    c.right_number = c.band + ed.band_w;
    c.width = c.right_number + ed.number_w;
    return c;
}

int DiffGutter::Offset(Side side) const {
    return ed.PaneFor(side)->ClientToScreen(wxPoint(0, 0)).y - ClientToScreen(wxPoint(0, 0)).y;
}

int DiffGutter::RowAt(Side side, int y) const {
    return ed.PaneFor(side)->RowAt(y - Offset(side));
}

namespace {
/// A small arrow drawn in lines, pointing left or right, centred on @p x, @p y.
void draw_arrow(wxDC& dc, int x, int y, bool right, int size, wxColour const& colour) {
    int h = std::max(3, size / 2), dir = right ? 1 : -1;
    dc.SetPen(wxPen(colour, std::max(1, size / 6)));
    dc.DrawLine(x - h * dir, y, x + h * dir, y);
    dc.DrawLine(x + h * dir, y, x + h * dir - (h * 2 / 3 + 1) * dir, y - h * 2 / 3 - 1);
    dc.DrawLine(x + h * dir, y, x + h * dir - (h * 2 / 3 + 1) * dir, y + h * 2 / 3 + 1);
}
}

void DiffGutter::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    auto const& p = ed.pal;
    auto c = Cols();
    int rh = ed.row_h;
    dc.SetBackground(wxBrush(p.bg));
    dc.Clear();
    dc.SetFont(GetFont());
    int rows = (int)ed.layout.size();
    if (!rows) return;
    auto pane_rows = [&](Side s) {
        auto pane = ed.PaneFor(s);
        return std::make_pair(pane->Top(), std::min(rows, pane->Top() + pane->VisibleRows() + 1));
    };
    auto [left_first, left_last] = pane_rows(Side::Left);
    auto [right_first, right_last] = pane_rows(Side::Right);
    int off_l = Offset(Side::Left), off_r = Offset(Side::Right);
    auto y_of = [&](Side s, int row) { return (s == Side::Left ? off_l : off_r) + ed.RowY(row) - ed.RowY(ed.PaneFor(s)->Top()); };

    // The lane between the numbers: the colour of added and removed runs
    // and of the current difference, carried from one side to the other,
    // bent where the panes are scrolled apart.
    auto curve = [&](int ya, int yb, std::vector<wxPoint>& out, bool reverse) {
        constexpr int steps = 10;
        for (int i = 0; i <= steps; ++i) {
            double t = double(reverse ? steps - i : i) / steps, s = t * t * (3 - 2 * t);
            out.emplace_back(c.band + int(t * ed.band_w + 0.5), int(ya + (yb - ya) * s + 0.5));
        }
    };
    int first = std::min(left_first, right_first), last = std::max(left_last, right_last);
    int top = std::min(off_l, off_r);
    int bottom = std::max(off_l + ed.left->ViewHeight(), off_r + ed.right->ViewHeight());
    {
        wxDCClipper clip(dc, wxRect(c.band, top, ed.band_w, std::max(0, bottom - top)));
        auto [card_first, card_last] = ed.CardRows();
        for (int row = first; row < last; ++row) {
            auto const& lr = ed.layout[row];
            bool in_card = row >= card_first && row <= card_last;
            bool diff = !lr.IsFold() && ed.ShownDiff(lr.entry);
            if (!diff && !lr.IsFold()) continue;
            int yl = y_of(Side::Left, row), yr = y_of(Side::Right, row);
            rh = ed.RowH(row);
            if (in_card) {
                // The card goes on across the lane, its edges and shadow too.
                int top_in = row == card_first ? 2 : 0, bottom_in = row == card_last ? 2 : 0;
                std::vector<wxPoint> face, shadow, top_edge, bottom_edge;
                curve(yl + top_in, yr + top_in, face, false);
                curve(yl + rh - bottom_in, yr + rh - bottom_in, face, true);
                dc.SetPen(*wxTRANSPARENT_PEN);
                if (bottom_in) {
                    curve(yl + rh - 2, yr + rh - 2, shadow, false);
                    curve(yl + rh, yr + rh, shadow, true);
                    dc.SetBrush(wxBrush(mix(p.bg, *wxBLACK, theme::IsDark() ? 0.4 : 0.07)));
                    dc.DrawPolygon((int)shadow.size(), shadow.data());
                }
                dc.SetBrush(wxBrush(row == card_first ? ed.RowColour(lr.entry, Side::Right) : p.bg));
                dc.DrawPolygon((int)face.size(), face.data());
                dc.SetPen(wxPen(p.border));
                if (top_in) dc.DrawLines(11, face.data());
                if (bottom_in) dc.DrawLines(11, face.data() + 11);
                if (lr.IsFoot()) {
                    // the hairline over the foot
                    dc.SetPen(wxPen(mix(p.border, p.bg, 0.4)));
                    dc.DrawLines(11, face.data());
                }
                continue;
            }
            std::vector<wxPoint> shape;
            curve(yl, yr, shape, false);
            curve(yl + rh, yr + rh, shape, true);
            if (lr.IsFold()) {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(p.fold_bg));
                dc.DrawPolygon((int)shape.size(), shape.data());
                continue;
            }
            auto const& e = ed.state.Entries()[lr.entry];
            if (e.change == Change::Modified) {
                // Nothing to fill: a thread joins the two sides where they part.
                if (yl != yr) {
                    std::vector<wxPoint> mid;
                    curve(yl + rh / 2, yr + rh / 2, mid, false);
                    dc.SetPen(wxPen(ed.EdgeColour(lr.entry), 2));
                    dc.DrawLines((int)mid.size(), mid.data());
                }
                continue;
            }
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(ed.RowColour(lr.entry, e.change == Change::Removed ? Side::Left : Side::Right)));
            dc.DrawPolygon((int)shape.size(), shape.data());
            dc.SetPen(wxPen(ed.EdgeColour(lr.entry)));
            if (ed.ChunkStart(row)) dc.DrawLines(11, shape.data());
            if (ed.ChunkEnd(row)) dc.DrawLines(11, shape.data() + 11);
        }
    }
    for (auto s : {Side::Left, Side::Right}) {
        auto [from, to] = s == Side::Left ? std::make_pair(left_first, left_last) : std::make_pair(right_first, right_last);
        int off = s == Side::Left ? off_l : off_r;
        int x = s == Side::Left ? c.left_number : c.right_number;
        wxDCClipper clip(dc, wxRect(x, off, ed.number_w, std::max(0, std::min(GetClientSize().GetHeight() - off, ed.PaneFor(s)->ViewHeight()))));
        for (int row = from; row < to; ++row) DrawStrip(dc, s, row, y_of(s, row));
    }
    // Each difference's mark, midway between where its two sides are.
    wxDCClipper clip(dc, wxRect(c.band, top, ed.band_w, std::max(0, bottom - top)));
    for (int row = first; row < last; ++row)
        DrawMark(dc, row, (y_of(Side::Left, row) + y_of(Side::Right, row)) / 2);
}

void DiffGutter::DrawMark(wxDC& dc, int row, int y) {
    // In the lane: a stroke in the colour of a pending difference's change,
    // an arrow to the side a decided one keeps (◐ for a partial one), and on
    // the pointed and the current line, and each of its fields, the two
    // sides to choose from, the chosen one filled.
    auto const& p = ed.pal;
    auto c = Cols();
    // the choices on the first line of a wrapped row, the stroke down all of it
    int rh = ed.row_h, full_h = ed.RowH(row);
    auto const& lr = ed.layout[row];
    if (lr.IsFold() || lr.IsFoot() || !ed.ShownDiff(lr.entry)) return;
    auto const& e = ed.state.Entries()[lr.entry];
    bool detail = lr.IsDetail();
    bool is_current = ed.IsCurrent(lr.entry);
    bool active = detail || row == ed.hot || is_current;
    int mid = c.band + ed.band_w / 2, cy = y + rh / 2;
    int arrow = std::max(8, rh / 3);
    Decision d = ed.state.GetDecision(lr.entry);
    if (active) {
        bool left_chosen, right_chosen, partial = false;
        if (detail) {
            auto winner = ed.state.Winner(lr.entry, *lr.field);
            left_chosen = winner && *winner == Side::Left;
            right_chosen = winner && *winner == Side::Right;
        }
        else {
            left_chosen = d == Decision::Rejected;
            right_chosen = d == Decision::Accepted;
            partial = d == Decision::Partial;
        }
        int bw = (ed.band_w - 8) / 2, bh = std::min(rh - 8, FromDIP(18));
        for (auto side : {Side::Left, Side::Right}) {
            bool left = side == Side::Left;
            wxRect box(left ? mid - 1 - bw : mid + 1, cy - bh / 2, bw, bh);
            bool chosen = left ? left_chosen : right_chosen;
            Part part = left ? Part::Reject : Part::Accept;
            bool hot = row == hover_row && hover_part == part;
            wxColour back = ed.RowColour(lr.entry, side);
            dc.SetPen(wxPen(hot ? p.accent : chosen ? p.current : partial ? mix(p.current, back, 0.4) : mix(p.border, back, 0.2)));
            dc.SetBrush(wxBrush(chosen ? p.current : hot ? mix(p.accent, back, 0.88) : back));
            dc.DrawRoundedRectangle(box, 3);
            draw_arrow(dc, box.x + box.width / 2, cy, !left, arrow, chosen ? p.bg : hot ? p.accent : p.gutter_text);
        }
        return;
    }
    wxColour quiet = mix(p.current, p.bg, 0.35);
    if (d == Decision::Pending) {
        wxColour c_mark = e.change == Change::Modified ? p.mod_mark : e.change == Change::Inserted ? p.ins_mark : p.del_mark;
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(c_mark));
        dc.DrawRoundedRectangle(mid - 2, y + 4, 4, full_h - 8, 2);
    }
    else if (d == Decision::Partial) {
        int r = std::max(4, arrow / 2 + 1);
        dc.SetPen(wxPen(quiet));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawCircle(mid, cy, r);
        dc.SetBrush(wxBrush(quiet));
        dc.DrawEllipticArc(mid - r, cy - r, 2 * r, 2 * r, 90, 270);
    }
    else draw_arrow(dc, mid, cy, d == Decision::Accepted, arrow, quiet);
    // Real doubts about the match: the rows cross, or several candidates.
    wxString doubt;
    if (e.ambiguity == model::Ambiguity::Crossing) doubt = wxString::FromUTF8("⇅");
    else if (e.ambiguity == model::Ambiguity::Duplicate || e.ambiguity == model::Ambiguity::ConflictingEvidence) doubt = "?";
    if (!doubt.empty()) {
        dc.SetFont(ed.small_bold_font);
        auto extent = dc.GetTextExtent(doubt);
        dc.SetTextForeground(p.warn);
        dc.DrawText(doubt, mid + 6, cy - extent.GetHeight() / 2);
        dc.SetFont(GetFont());
    }
}

void DiffGutter::DrawStrip(wxDC& dc, Side side, int row, int y) {
    // A side's line number, coloured as its line; the right one is a check
    // box where it is pointed at, checked, or while a selection is made.
    auto const& p = ed.pal;
    auto c = Cols();
    // filled down the whole row, the number on its first line
    int rh = ed.RowH(row), line_h = ed.row_h;
    bool left = side == Side::Left;
    int x0 = left ? c.left_number : c.right_number, w = ed.number_w;
    auto const& lr = ed.layout[row];
    dc.SetPen(*wxTRANSPARENT_PEN);
    if (lr.IsFold()) {
        dc.SetBrush(wxBrush(p.fold_bg));
        dc.DrawRectangle(x0, y, w, rh);
        return;
    }
    auto const& e = ed.state.Entries()[lr.entry];
    bool is_current = ed.IsCurrent(lr.entry);
    bool detail = lr.InCard();
    bool diff = ed.ShownDiff(lr.entry);
    if (is_current) {
        // in the card, as the lane and the panes draw it
        auto [first, last] = ed.CardRows();
        int top = y + (row == first ? 2 : 0), bottom = y + rh - (row == last ? 2 : 0);
        if (row == last) {
            dc.SetBrush(wxBrush(mix(p.bg, *wxBLACK, theme::IsDark() ? 0.4 : 0.07)));
            dc.DrawRectangle(x0, bottom, w, 2);
        }
        dc.SetBrush(wxBrush(row == first ? ed.RowColour(lr.entry, side) : p.bg));
        dc.DrawRectangle(x0, top, w, bottom - top);
        dc.SetPen(wxPen(p.border));
        if (row == first) dc.DrawLine(x0, top, x0 + w, top);
        if (row == last) dc.DrawLine(x0, bottom - 1, x0 + w, bottom - 1);
        if (lr.IsFoot()) {
            dc.SetPen(wxPen(mix(p.border, p.bg, 0.4)));
            dc.DrawLine(x0, y, x0 + w, y);
        }
    }
    else if (diff) {
        dc.SetBrush(wxBrush(ed.RowColour(lr.entry, side)));
        dc.DrawRectangle(x0, y, w, rh);
        dc.SetPen(wxPen(ed.EdgeColour(lr.entry)));
        if (e.change != Change::Modified) {
            if (ed.ChunkStart(row)) dc.DrawLine(x0, y, x0 + w, y);
            if (ed.ChunkEnd(row)) dc.DrawLine(x0, y + rh - 1, x0 + w, y + rh - 1);
        }
    }
    else if (row == ed.hot) {
        dc.SetBrush(wxBrush(p.hover));
        dc.DrawRectangle(x0, y, w, rh);
    }
    int text_y = y + (line_h - dc.GetCharHeight()) / 2;
    bool hovering_check = row == hover_row && hover_part == Part::Check;
    if (!left && diff && !detail && (ed.state.Checked(lr.entry) || ed.Selecting() || hovering_check)) {
        int s = std::min(FromDIP(14), line_h - 8);
        flat::DrawCheck(dc, this, wxRect(x0 + (w - s) / 2, y + (line_h - s) / 2, s, s),
            ed.state.Checked(lr.entry) ? 1 : 0, flat::Colours(this));
    }
    else if (!detail && e.Get(side)) {
        auto number = wxString::Format("%zu", e.Get(side)->number);
        dc.SetTextForeground(diff || is_current ? p.text : p.gutter_text);
        int nx = left ? x0 + w - 6 - dc.GetTextExtent(number).GetWidth() : x0 + 6;
        dc.DrawText(number, nx, text_y);
    }
}

void DiffGutter::OnMouse(wxMouseEvent& event) {
    auto type = event.GetEventType();
    if (type == wxEVT_MOUSEWHEEL) {
        // Both sides: together when in sync (the right one leads), else each.
        ed.right->Wheel(event);
        if (!ed.sync) ed.left->Wheel(event);
        return;
    }
    if (type == wxEVT_LEAVE_WINDOW) {
        if (hover_row != -1) { hover_row = -1; hover_part = Part::None; Refresh(false); }
        ed.SetHot(-1);
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
        bool in_lane = pt.x >= c.band && pt.x < c.right_number;
        if (lr.IsFold()) part = Part::Fold;
        else if (lr.IsFoot()) part = Part::None;
        else if (diff && in_lane) part = side == Side::Left ? Part::Reject : Part::Accept;
        else if (diff && side == Side::Right && !lr.InCard() && pt.x >= c.right_number) part = Part::Check;
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
, tag_mode(OPT_GET("Subtitle/Grid/Hide Overrides"))
{
    auto project_file = c->subsController->Filename();
    project_name = project_file.empty() ? _("Untitled") : wxString(project_file.filename().wstring());

    // Three bands round the panes: the toolbar, the pane headers, the foot.
    // Toolbar: where you are, what is shown, how far the decisions have
    // come, undo. Deciding is on the current difference's card.
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
    view_switch = new FlatSwitch(this, {_("Differences only"), _("Everything")});
    view_switch->SetName("compare-view");
    view_switch->SetItemToolTip(0, _("Fold long runs of identical lines, keeping context around each difference"));
    view_switch->SetItemToolTip(1, _("Show every line"));
    bar->Add(view_switch, wxSizerFlags().Center().Border(wxRIGHT, 2 * gap));
    filter_button = add(wxID_ANY, _("Filter"), _("Show one kind of difference, or only those pending; leave fields out of the comparison"), "compare-filter", FlatGlyph::Filter);
    // What the filters do, each removable on its own.
    chips = new wxBoxSizer(wxHORIZONTAL);
    bar->Add(chips, wxSizerFlags().Center());
    bar->AddStretchSpacer();
    // How far the decisions have come, the four counts in its tooltip.
    stats = new DecisionMeter(this);
    bar->Add(stats, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, 3 * gap));
    undo = add(ID_UNDO, {}, _("Undo the last decision (Ctrl+Z)"), "compare-undo", FlatGlyph::Undo);
    redo = add(ID_REDO, {}, _("Redo (Ctrl+Y)"), "compare-redo", FlatGlyph::Redo);
    more_button = make(wxID_ANY, wxString::FromUTF8("⋯"), _("How the lines are shown: override tags, scrolling, text size"), "compare-more");
    bar->Add(more_button, wxSizerFlags().Center());

    // The filters' panel, dropped from its button: one kind of difference at
    // a time, the chosen one in the accent colour with how many there are of
    // each; what is pending only; field groups to leave out.
    filter_popup = new ComparePopup(this, filter_button, false, false);
    auto fp = filter_popup->body;
    filter_popup->Heading(flat::WithoutColon(_("Show:")), true);
    auto add_category = [&](std::optional<view::Category> category) {
        auto b = new FlatButton(fp, "");
        b->SetToolTip(category ? _("Show only this kind of difference; Use Left for All and Use Right for All then apply to these") : _("Show every kind of difference"));
        b->SetName(wxString("compare-category-") + (category ? view::CategoryName(*category) : "all"));
        b->Bind(wxEVT_BUTTON, [this, category](wxCommandEvent&) { filter.category = category; ApplyFilter(); });
        filter_popup->Item(b);
        category_buttons.emplace_back(category, b);
    };
    add_category(std::nullopt);
    for (auto category : view::CategoryOrder()) add_category(category);
    filter_popup->items->AddSpacer(FromDIP(6));
    // What is left to decide: those decided go, except the one being decided.
    pending_box = new FlatCheck(fp, _("Pending only"));
    pending_box->SetName("compare-pending");
    pending_box->SetToolTip(_("Show only the differences not yet decided. The one you decide stays until you move on."));
    pending_box->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        filter.pending_only = pending_box->GetValue();
        ApplyFilter();
    });
    filter_popup->items->Add(pending_box, wxSizerFlags().Border(wxLEFT | wxRIGHT, FromDIP(8)));
    filter_popup->Heading(flat::WithoutColon(_("Ignore:")));
    for (auto group : {view::Category::Time, view::Category::Text, view::Category::StyleActor, view::Category::OtherFields}) {
        auto box = new FlatCheck(fp, group_label(group));
        box->SetName(wxString("compare-ignore-") + view::CategoryName(group));
        box->SetToolTip(_("Leave these fields out: they are not highlighted, not counted, and decisions here leave them as they are"));
        box->Bind(wxEVT_CHECKBOX, [this, group, box](wxCommandEvent&) {
            if (box->GetValue()) filter.ignored |= view::CategoryFields(group);
            else filter.ignored &= ~view::CategoryFields(group);
            ApplyFilter();
        });
        filter_popup->items->Add(box, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
        ignore_boxes.emplace_back(group, box);
    }
    filter_popup->on_change = [this] { UpdateFilterBar(); };
    filter_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { filter_popup->Toggle(); });

    // "⋯": how the lines are shown, which is not what is shown. Override
    // tags as the subtitle grid shows them, the one setting for both.
    view_popup = new ComparePopup(this, more_button, false, true);
    auto vp = view_popup->body;
    view_popup->Heading(flat::WithoutColon(_("Tags:")), true);
    tags_switch = new FlatSwitch(vp, {_("Full"), _("Simplified"), _("Hidden")});
    tags_switch->SetName("compare-tags");
    tags_switch->SetItemToolTip(0, _("Show override tags as written"));
    tags_switch->SetItemToolTip(1, _("Show each {...} block of override tags as a chip of icons saying what it does; point at one for its tags. The subtitle grid shows them the same way."));
    tags_switch->SetItemToolTip(2, _("Leave override tags out; a changed block keeps a mark where it was"));
    tags_switch->SetSelection(std::clamp<int>(TagMode(), 0, 2));
    tags_switch->on_choose = [](int index) { OPT_SET("Subtitle/Grid/Hide Overrides")->SetInt(index); };
    view_popup->items->Add(tags_switch, wxSizerFlags().Border(wxLEFT | wxRIGHT, FromDIP(8)));
    view_popup->items->AddSpacer(FromDIP(10));
    sync_box = new FlatCheck(vp, _("Sync scrolling"));
    sync_box->SetName("compare-sync"); sync_box->SetValue(true);
    sync_box->SetToolTip(_("Scroll both sides together. When off, each side scrolls on its own and selecting a line locates it on the other side."));
    view_popup->items->Add(sync_box, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
    // The lines' text size (Ctrl with the wheel, +, - or 0 too).
    view_popup->Heading(_("Text size"));
    auto size_row = new wxBoxSizer(wxHORIZONTAL);
    auto size_button = [&](wxString const& label, wxString const& tip, char const* name) {
        auto b = new FlatButton(vp, label);
        b->SetToolTip(tip);
        b->SetName(name);
        b->SetLeastSize(wxSize(FromDIP(28), FromDIP(26)));
        return b;
    };
    font_smaller = size_button(wxString::FromUTF8("−"), _("Smaller text (Ctrl+-)"), "compare-font-smaller");
    font_larger = size_button("+", _("Larger text (Ctrl+=)"), "compare-font-larger");
    font_reset = size_button(_("Reset"), _("The page's own text size (Ctrl+0)"), "compare-font-reset");
    font_size_label = new wxStaticText(vp, -1, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
    font_size_label->SetName("compare-font-size");
    font_size_label->SetMinSize(wxSize(font_size_label->GetTextExtent(fmt_tl("%d pt (default)", 88)).GetWidth() + FromDIP(8), -1));
    size_row->Add(font_smaller, wxSizerFlags().Center());
    size_row->Add(font_size_label, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, FromDIP(4)));
    size_row->Add(font_larger, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(8)));
    size_row->Add(font_reset, wxSizerFlags().Center());
    view_popup->items->Add(size_row, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
    font_smaller->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Zoom(-1); });
    font_larger->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Zoom(1); });
    font_reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Zoom(0); });
    more_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { view_popup->Toggle(); });

    // The sheet: each side's role and file, the swap between them, the
    // column names and the lines.
    sheet = new wxPanel(this, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE);
    sheet->SetName("compare-sheet");
    sheet->SetBackgroundColour(pal.bg);
    auto swap = new FlatButton(sheet, {}, wxID_ANY);
    swap->SetGlyph(FlatGlyph::Swap);
    swap->SetToolTip(_("Swap Sides") + ": " + _("Show the open subtitles on the other side. Decisions keep their effect on the open subtitles."));
    swap->SetName("compare-swap");
    swap->SetLeastSize(wxSize(-1, FromDIP(26)));
    left_role = flat::DimLabel(sheet, _("Base version"));
    right_role = flat::DimLabel(sheet, _("Current version"));
    left_title = new wxStaticText(sheet, -1, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
    right_title = new wxStaticText(sheet, -1, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
    left_title->SetName("compare-left-title"); right_title->SetName("compare-right-title");
    auto bold = GetFont().Bold();
    left_title->SetFont(bold); right_title->SetFont(bold);
    left_target = new TargetBadge(sheet, _("target project"), "compare-left-target");
    right_target = new TargetBadge(sheet, _("target project"), "compare-right-target");
    left = new DiffPane(sheet, *this, Side::Left);
    right = new DiffPane(sheet, *this, Side::Right);
    gutter = new DiffGutter(sheet, *this);
    // The card's buttons, on its foot in each pane (PlaceCard): the left side
    // beside the gutter on the left, the right side and the reset beside it
    // on the right.
    auto card_button = [&](DiffPane* pane, int id, wxString const& label, wxString const& tip, char const* name) {
        auto b = new FlatButton(pane, label, id);
        b->SetToolTip(tip);
        b->SetName(name);
        b->Hide();
        // Tab goes from pane to pane; the keys choose as these do (Alt+L, Ctrl+arrows).
        b->DisableFocusFromKeyboard();
        b->Bind(wxEVT_MOUSEWHEEL, [pane](wxMouseEvent& e) { pane->Wheel(e); });
        return b;
    };
    reject_current = card_button(left, ID_REJECT_CURRENT, wxString::FromUTF8("← ") + _("Use &Left"),
        _("Take the left side of the current difference (Ctrl+Left)") + " Alt+L", "compare-reject-current");
    accept_current = card_button(right, ID_ACCEPT_CURRENT, _("Use &Right") + wxString::FromUTF8(" →"),
        _("Keep the right side of the current difference (Ctrl+Right)") + " Alt+R", "compare-accept-current");
    reset_current = card_button(right, wxID_ANY, wxString::FromUTF8("↺"), _("Reset this difference to pending"), "compare-reset-current");
    reset_current->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (auto e = CurrentEntry()) Decide(*e, Decision::Pending); });
    hint = new wxStaticText(right, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    hint->SetName("compare-hint");
    hint->SetFont(GetFont().Smaller());
    hint->SetForegroundColour(pal.dim);
    hint->SetBackgroundColour(pal.bg);
    hint->Hide();
    hint->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) { right->Wheel(e); });
    left_header = new DiffHeader(sheet, *this, left);
    right_header = new DiffHeader(sheet, *this, right);
    left->SetHeader(left_header);
    right->SetHeader(right_header);
    int title_h = swap->GetBestSize().GetHeight() + 2 * gap;
    auto title_row = [&](wxStaticText* role, wxStaticText* title, TargetBadge* target) {
        auto row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(role, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, 2 * gap));
        row->Add(title, wxSizerFlags(1).Center());
        row->Add(target, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, 2 * gap));
        row->SetMinSize(wxSize(-1, title_h));
        return row;
    };

    // The foot: deciding many at once, what writing will do, and the
    // buttons that end the comparison. Many at once is a panel dropped from
    // "Batch", or, while some are selected, what to do with those.
    batch_button = make(wxID_ANY, _("Batch") + wxString::FromUTF8(" ▾"), _("Decide every difference shown at once, or select them all"), "compare-batch");
    batch_popup = new ComparePopup(this, batch_button, true, false);
    auto bp = batch_popup->body;
    auto batch_item = [&](wxString const& label, wxString const& tip, char const* name) {
        auto b = new FlatButton(bp, label);
        b->SetToolTip(tip);
        b->SetName(name);
        batch_popup->Item(b);
        return b;
    };
    reject_all = batch_item(_("Use Left for All"), _("Take the left side of every difference shown"), "compare-reject-all");
    accept_all = batch_item(_("Use Right for All"), _("Keep the right side of every difference shown"), "compare-accept-all");
    batch_popup->items->AddSpacer(FromDIP(6));
    check_all = new FlatCheck(bp, _("Select all differences"));
    check_all->SetName("compare-check-all");
    batch_popup->items->Add(check_all, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
    batch_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { batch_popup->Toggle(); });
    checked_count = new wxStaticText(this, -1, "");
    checked_count->SetName("compare-checked-count");
    checked_count->SetFont(GetFont().Bold());
    reject_checked = make(wxID_ANY, _("Use Left"), _("Take the left side of the selected differences"), "compare-reject-checked");
    accept_checked = make(wxID_ANY, _("Use Right"), _("Keep the right side of the selected differences"), "compare-accept-checked");
    clear_checked = make(wxID_ANY, _("Clear"), _("Unselect every difference"), "compare-clear-checked");
    select_group = new wxBoxSizer(wxHORIZONTAL);
    select_group->Add(checked_count, wxSizerFlags().Center().Border(wxRIGHT, 3 * gap));
    select_group->Add(reject_checked, wxSizerFlags().Center().Border(wxRIGHT, gap));
    select_group->Add(accept_checked, wxSizerFlags().Center().Border(wxRIGHT, gap));
    select_group->Add(clear_checked, wxSizerFlags().Center());

    report = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    report->SetName("compare-report");

    // Cancel, then the button the page is for in the accent colour (first
    // on Windows), as flat::DialogButtons lays out a dialog's.
    finish = new FlatButton(this, "", wxID_OK, true);
    auto cancel = new FlatButton(this, "", wxID_CANCEL);
    for (auto b : {finish, cancel}) b->SetLeastSize(wxSize(FromDIP(76), FromDIP(28)));
    cancel->SetToolTip(_("End the comparison without writing to the subtitles"));
    finish->SetToolTip(_("Write the decisions to the open subtitles as one undoable change and end the comparison"));

    auto foot = new wxBoxSizer(wxHORIZONTAL);
    foot->Add(batch_button, wxSizerFlags().Center());
    foot->Add(select_group, wxSizerFlags().Center());
    foot->AddSpacer(5 * gap);
    foot->Add(report, wxSizerFlags(1).Center());
    foot->AddSpacer(3 * gap);
#ifdef __WXMSW__
    foot->Add(finish, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(6)));
    foot->Add(cancel, wxSizerFlags().Center());
#else
    foot->Add(cancel, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(6)));
    foot->Add(finish, wxSizerFlags().Center());
#endif

    auto left_column = new wxBoxSizer(wxVERTICAL);
    left_column->Add(title_row(left_role, left_title, left_target), wxSizerFlags().Expand());
    left_column->Add(left_header, wxSizerFlags().Expand());
    left_column->Add(left, wxSizerFlags(1).Expand());
    auto right_column = new wxBoxSizer(wxVERTICAL);
    right_column->Add(title_row(right_role, right_title, right_target), wxSizerFlags().Expand());
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
    sheet->SetSizer(panes);

    main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(bar, wxSizerFlags().Expand().Border(wxALL, 8));
    main_sizer->Add(sheet, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, 8));
    main_sizer->Add(foot, wxSizerFlags().Expand().Border(wxALL, 8));
    SetName("compare-page");
    SetSizer(main_sizer);

    pane_font = GetFont();
    ApplyFontSize(true);
    font_size_changed = OPT_SUB("Subtitle/Compare/Font Size", [this](agi::OptionValue const&) { ApplyFontSize(); });
    // The right pane also holds the overview ruler and room for how far
    // times moved: it gets that much more width than the left one.
    left->SetMinSize(DipSize(320, 200));
    right->SetMinSize(wxSize(320 + 2 * delta_w, 200));
    gutter->SetMinSize(wxSize(gutter->Width(), 200));
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        int panes = GetClientSize().GetWidth() - 16 - gutter->Width();
        int extra = 2 * delta_w;
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
        // the selection's buttons take the place of the one it hangs from
        batch_popup->Close();
        for (auto e : shown) state.Check(e, check_all->GetValue());
        Changed();
    });
    clear_checked->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        for (auto e : state.CheckedEntries()) state.Check(e, false);
        Changed();
    });
    auto checked_shown = [this] {
        std::vector<size_t> out;
        for (auto e : state.CheckedEntries()) if (ShownDiff(e)) out.push_back(e);
        return out;
    };
    accept_checked->Bind(wxEVT_BUTTON, [this, checked_shown](wxCommandEvent&) { Batch(checked_shown(), Decision::Accepted, false); });
    reject_checked->Bind(wxEVT_BUTTON, [this, checked_shown](wxCommandEvent&) { Batch(checked_shown(), Decision::Rejected, false); });
    accept_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { batch_popup->Close(); Batch(shown, Decision::Accepted, true); });
    reject_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { batch_popup->Close(); Batch(shown, Decision::Rejected, true); });
    finish->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnFinish(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { TryClose(true); }, wxID_CANCEL);
    // Keys reach this tab before the main window: its hotkeys and menu
    // shortcuts would act on the locked subtitles. Keys the window does not
    // use go on to the focused control, and no further.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (!OnKey(event)) event.DoAllowNextEvent();
    });
    // The wheel anywhere over a side (its file's name, its column names,
    // what has no window of its own) scrolls that side; between them, both.
    auto both = [this](wxMouseEvent& e) { right->Wheel(e); if (!sync) left->Wheel(e); };
    sheet->Bind(wxEVT_MOUSEWHEEL, [this, both](wxMouseEvent& event) {
        wxPoint pt = event.GetPosition();
        wxRect sides(left->GetPosition(), right->GetRect().GetBottomRight());
        sides.SetTop(0);
        if (!sides.Contains(pt)) return;
        if (pt.x < gutter->GetPosition().x) left->Wheel(event);
        else if (pt.x >= right->GetPosition().x) right->Wheel(event);
        else both(event);
    });
    // Mouse events do not reach the page from its children: those over the sides say so.
    for (auto label : {left_role, left_title}) label->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) { left->Wheel(e); });
    for (auto label : {right_role, right_title}) label->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) { right->Wheel(e); });
    left_target->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) { left->Wheel(e); });
    right_target->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) { right->Wheel(e); });
    swap->Bind(wxEVT_MOUSEWHEEL, both);
    // The grid's tag setting, changed here or there.
    tag_mode_changed = OPT_SUB("Subtitle/Grid/Hide Overrides", [this](agi::OptionValue const& opt) {
        tags_switch->SetSelection(std::clamp<int>((int)opt.GetInt(), 0, 2));
        Measure();
        MeasureRows();
        left->UpdateBars(); right->UpdateBars();
        left->Refresh(false); right->Refresh(false);
    });

    // Follow the appearance while open.
    theme_changed = theme::AddChangeListener([this] {
        pal = make_palette();
        hint->SetForegroundColour(pal.dim);
        hint->SetBackgroundColour(pal.bg);
        sheet->SetBackgroundColour(pal.bg);
        left->SetBackgroundColour(pal.bg); right->SetBackgroundColour(pal.bg);
        sheet->Refresh();
        UpdateTitles();
        for (auto w : {static_cast<wxWindow*>(left), static_cast<wxWindow*>(right), static_cast<wxWindow*>(gutter)}) w->Refresh(false);
        for (auto w : sheet->GetChildren()) if (dynamic_cast<DiffHeader*>(w) || dynamic_cast<TargetBadge*>(w)) w->Refresh(false);
    });

    Changed();
}

bool ComparePanel::OnKey(wxKeyEvent& event) {
    int key = event.GetKeyCode(), mods = event.GetModifiers();
    if (key == WXK_ESCAPE && mods == wxMOD_NONE && field_cursor) { SetFieldCursor(std::nullopt); return true; }
    if ((key == WXK_F7 && mods == wxMOD_NONE) || (key == WXK_DOWN && mods == wxMOD_ALT)) { Go(1); return true; }
    if ((key == WXK_F7 && mods == wxMOD_SHIFT) || (key == WXK_UP && mods == wxMOD_ALT)) { Go(-1); return true; }
    // Ctrl with +, - and 0: the lines' text larger, smaller, as the page's.
    if (mods == wxMOD_CONTROL || mods == (wxMOD_CONTROL | wxMOD_SHIFT)) {
        if (key == '=' || key == '+' || key == WXK_NUMPAD_ADD) { Zoom(1); return true; }
        if (key == '-' || key == WXK_NUMPAD_SUBTRACT) { Zoom(-1); return true; }
        if (key == '0' || key == WXK_NUMPAD0) { Zoom(0); return true; }
    }
    // Alt+L and Alt+R decide the current difference wherever its card is.
    if (mods == wxMOD_ALT && (key == 'L' || key == 'R')) {
        if (auto e = CurrentEntry()) Decide(*e, key == 'R' ? Decision::Accepted : Decision::Rejected);
        return true;
    }
    if (mods == wxMOD_ALT && flat::PressMnemonic(this, key)) return true;
    // F6: from one side to the other (to the right one from elsewhere).
    if (key == WXK_F6 && (mods == wxMOD_NONE || mods == wxMOD_SHIFT)) {
        (right->HasFocus() ? left : right)->SetFocus();
        return true;
    }
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
        cursor_row = row;
    }
    else if (!layout.empty()) cursor_row = 0;
    right->SetFocus();
}

void ComparePanel::Measure() {
    if (!pane_font.IsOk()) pane_font = GetFont();
    wxClientDC dc(this);
    dc.SetFont(pane_font);
    char_w = std::max(1, dc.GetCharWidth());
    int char_h = dc.GetCharHeight();
    row_h = std::max(28, char_h + 12);
    line_pitch = char_h + 6;
    small_font = pane_font.Smaller();
    bold_font = pane_font.Bold();
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
    short_times = true;
    for (auto const& e : state.Entries()) {
        time_changes |= (e.changed & (model::Start | model::End)) != 0;
        for (auto const* line : {e.left ? &*e.left : nullptr, e.right ? &*e.right : nullptr}) {
            if (!line) continue;
            max_number = std::max(max_number, line->number);
            short_times &= std::max(line->start, line->end) < 3600000;
            styles.insert(line->Value(model::Style));
            actors.insert(line->Value(model::Actor));
            int w = 0;
            if (TagMode() == 0) w = estimate(line->Value(model::Text));
            else for (auto const& piece : Summary(line->Value(model::Text)))
                w += piece.block ? (TagMode() == 1 ? char_w * 10 : 0) : estimate(from_wx(tag_summary::PlainText(piece)));
            for (auto f : view::FieldOrder())
                if (!is_column(f) && (e.changed & f)) w += estimate(line->Value(f)) + char_w * 16;
            text = std::max(text, w);
        }
    }
    for (auto const& s : styles) style = std::max(style, dc.GetTextExtent(to_wx(s)).GetWidth());
    for (auto const& a : actors) actor = std::max(actor, dc.GetTextExtent(to_wx(a)).GetWidth());
    // A column with one value on every line of both sides tells nothing:
    // it is left out, its value said once after the column names.
    bool one_style = styles.size() == 1, one_actor = actors.size() <= 1;
    same_note.clear();
    auto note = [&](model::Field f, std::set<std::string> const& values) {
        if (values.empty() || values.begin()->empty()) return;
        if (!same_note.empty()) same_note += wxString::FromUTF8(" · ");
        same_note += field_label(f) + ": " + to_wx(*values.begin());
    };
    if (one_style) note(model::Style, styles);
    if (one_actor) note(model::Actor, actors);
    number_w = dc.GetTextExtent(wxString::Format("%zu", max_number)).GetWidth() + 14;
    // The lane holds the two sides to choose, side by side.
    band_w = std::max(FromDIP(44), 2 * (row_h - 6) + 8);
    dc.SetFont(bold_font);
    time_w = dc.GetTextExtent(short_times ? "00:00.000" : "0:00:00.000").GetWidth() + 14;
    dc.SetFont(small_font);
    // room for "+12.5s" after a changed time, on the right side
    delta_w = time_changes ? dc.GetTextExtent(wxString::FromUTF8("−0:00.0")).GetWidth() + 6 : 0;
    style_w = one_style ? 0 : std::min(style, char_w * 22) + 16;
    actor_w = one_actor ? 0 : std::min(actor, char_w * 16) + 16;
    content_w = 6 + (time_w + delta_w) * 2 + style_w + actor_w + text + char_w * 12;
    card_inset = FromDIP(4);
    card_radius = FromDIP(7);
    label_w = 0;
    for (auto f : view::FieldOrder()) label_w = std::max(label_w, dc.GetTextExtent(field_label(f)).GetWidth());
    label_w += 14;
    lines_key.clear();
}

void ComparePanel::MeasureRows() {
    // Each row as tall as the more lines its text wraps onto on either side;
    // a fold and the card's foot are one line.
    wxClientDC dl(left), dr(right);
    dl.SetFont(left->GetFont()); dr.SetFont(right->GetFont());
    auto key = wxString::Format("%d %d %d %d %d %llu %d", left->GetClientSize().x, right->GetClientSize().x, TagMode(), row_h,
        pane_font.GetPointSize(), (unsigned long long)filter.ignored, (int)state.ProjectSide());
    if (key != lines_key) { lines_key = key; lines_cache.clear(); }
    row_y.assign(layout.size() + 1, 0);
    for (size_t r = 0; r < layout.size(); ++r) {
        auto const& lr = layout[r];
        int lines = 1;
        if (lr.IsDetail()) lines = std::max(left->RowLines(dl, (int)r), right->RowLines(dr, (int)r));
        else if (!lr.IsFold() && !lr.IsFoot()) {
            auto it = lines_cache.find(lr.entry);
            if (it == lines_cache.end())
                it = lines_cache.emplace(lr.entry, std::make_pair(left->RowLines(dl, (int)r), right->RowLines(dr, (int)r))).first;
            lines = std::max(it->second.first, it->second.second);
        }
        row_y[r + 1] = row_y[r] + row_h + (lines - 1) * line_pitch;
    }
}

void ComparePanel::ApplyFontSize(bool starting) {
    pane_font = GetFont();
    int size = (int)OPT_GET("Subtitle/Compare/Font Size")->GetInt();
    if (size > 0) pane_font.SetPointSize(size);
    for (wxWindow* w : {static_cast<wxWindow*>(left), static_cast<wxWindow*>(right), static_cast<wxWindow*>(gutter),
            static_cast<wxWindow*>(left_header), static_cast<wxWindow*>(right_header)}) {
        w->SetFont(pane_font);
        w->InvalidateBestSize();
    }
    Measure();
    if (font_size_label) {
        font_size_label->SetLabel(size > 0 ? fmt_tl("%d pt", size) : fmt_tl("%d pt (default)", GetFont().GetPointSize()));
        font_smaller->Enable(pane_font.GetPointSize() > 6);
        font_larger->Enable(pane_font.GetPointSize() < 32);
        font_reset->Enable(size > 0);
        view_popup->Refit();
    }
    if (starting) return;
    gutter->SetMinSize(wxSize(gutter->Width(), 200));
    sheet->Layout();
    Relayout();
    Changed();
}

void ComparePanel::Zoom(int steps) {
    auto opt = OPT_SET("Subtitle/Compare/Font Size");
    if (!steps) { opt->SetInt(0); return; }
    int now = pane_font.IsOk() ? pane_font.GetPointSize() : GetFont().GetPointSize();
    opt->SetInt(std::clamp(now + steps, 6, 32));
}

void ComparePanel::Relayout(std::optional<size_t> anchor) {
    // Where the anchor entry is on screen now, in each pane.
    if (!anchor && !layout.empty()) anchor = layout[std::min<size_t>(right->Top(), layout.size() - 1)].entry;
    bool had = !layout.empty() && anchor && *anchor < row_of.size();
    int off_r = had ? row_of[*anchor] - right->Top() : 0;
    int off_l = had ? row_of[*anchor] - left->Top() : 0;
    // The keyboard's line stays on what it was on.
    std::optional<Row> at_cursor;
    if (cursor_row >= 0 && cursor_row < (int)layout.size()) at_cursor = layout[cursor_row];

    layout.clear();
    auto cur = CurrentEntry();
    for (auto const& lr : state.Layout(show_all, expanded, 3, filter)) {
        layout.push_back({lr.entry, lr.hidden, std::nullopt});
        // The current difference opens into a card: each changed field on a
        // row of its own, read and chosen one by one, however few, and a foot
        // to choose the whole line.
        if (lr.IsFold() || !cur || *cur != lr.entry) continue;
        auto fields = ShownFields(lr.entry);
        for (auto f : view::FieldOrder())
            if (fields & f) layout.push_back({lr.entry, 0, f});
        layout.push_back({lr.entry, 0, std::nullopt, true});
    }
    // The keyboard's field belongs to the current difference's detail rows.
    if (field_cursor && std::none_of(layout.begin(), layout.end(), [&](Row const& r) { return r.IsDetail() && r.field == field_cursor; }))
        field_cursor.reset();
    row_of.assign(state.Entries().size(), 0);
    for (size_t r = 0; r < layout.size(); ++r) {
        auto const& lr = layout[r];
        if (lr.InCard()) continue;
        for (size_t k = 0; k < std::max<size_t>(1, lr.hidden); ++k) row_of[lr.entry + k] = (int)r;
    }
    if (at_cursor && !layout.empty()) {
        cursor_row = row_of[at_cursor->entry];
        if (at_cursor->IsDetail())
            for (int r = 0; r < (int)layout.size(); ++r)
                if (layout[r].entry == at_cursor->entry && layout[r].field == at_cursor->field) cursor_row = r;
    }
    if (cursor_row >= (int)layout.size()) cursor_row = (int)layout.size() - 1;
    MeasureRows();
    int top_r = anchor ? row_of[*anchor] - off_r : 0;
    int top_l = anchor ? row_of[*anchor] - off_l : 0;
    right->SetView(top_r, right->XOffset());
    left->SetView(sync ? top_r : top_l, left->XOffset());
    left->UpdateBars(); right->UpdateBars();
    left->Refresh(false); right->Refresh(false); gutter->Refresh(false);
    PlaceCard();
}

std::pair<int, int> ComparePanel::CardRows() const {
    auto cur = CurrentEntry();
    if (!cur || *cur >= row_of.size() || layout.empty()) return {-1, -1};
    int first = row_of[*cur];
    if (first >= (int)layout.size() || layout[first].entry != *cur || layout[first].IsFold()) return {-1, -1};
    int last = first;
    while (!GroupEnd(last)) ++last;
    return {first, last};
}

void ComparePanel::PlaceCard() {
    auto [first, foot] = CardRows();
    auto place = [&](DiffPane* pane, std::initializer_list<FlatButton*> buttons, bool from_right) {
        int y = RowY(foot) - RowY(pane->Top());
        bool shown = foot >= 0 && foot >= pane->Top() && y + row_h <= pane->ViewHeight();
        int h = row_h - 8;
        // from the gutter outwards
        int x = from_right ? pane->ColumnsRight() - 8 : pane->RowsX() + 8;
        for (auto b : buttons) {
            int w = b->GetBestSize().GetWidth();
            if (from_right) x -= w;
            wxRect r(x, y + 3, w, h);
            if (b->GetRect() != r) b->SetSize(r);
            if (b->IsShown() != shown) b->Show(shown);
            x = from_right ? x - 4 : x + w + 4;
        }
    };
    place(left, {reject_current}, true);
    place(right, {accept_current, reset_current}, false);
    // the keys, after the reset to the pane's end
    int x = reset_current->GetRect().GetRight() + 1 + FromDIP(12);
    int w = right->ColumnsRight() - FromDIP(10) - x;
    int y = RowY(foot) - RowY(right->Top());
    bool shown = reset_current->IsShown() && w > FromDIP(40);
    if (shown) {
        int h = hint->GetBestSize().GetHeight();
        wxRect r(x, y + (row_h - 2 - h) / 2, w, h);
        if (hint->GetRect() != r) hint->SetSize(r);
    }
    if (hint->IsShown() != shown) hint->Show(shown);
}

wxString ComparePanel::CardStatus() const {
    auto cur = CurrentEntry();
    if (!cur) return {};
    Decision d = state.GetDecision(*cur);
    auto out = decision_label(d);
    if (d != Decision::Partial) return out;
    // which side each field keeps, as the gutter's arrows point
    wxString fields;
    for (auto f : view::FieldOrder()) {
        if (!(ShownFields(*cur) & f)) continue;
        auto winner = state.Winner(*cur, f);
        if (!winner) continue;
        if (!fields.empty()) fields += ", ";
        fields += field_label(f) + wxString::FromUTF8(*winner == Side::Right ? " →" : " ←");
    }
    return fields.empty() ? out : out + ": " + fields;
}

void ComparePanel::ApplyFilter() {
    filter.keep = current;
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
    // A modified line is coloured only while it is the current difference.
    if (e.change == Change::Modified) return IsCurrent(entry) ? pal.mod_line : pal.bg;
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

wxString ComparePanel::Help(Part part, bool, int row) const {
    bool detail = row >= 0 && row < (int)layout.size() && layout[row].IsDetail();
    auto keys = [](char const* k) { return wxString::FromUTF8(k); };
    switch (part) {
        case Part::Accept:
            if (detail) return keys("→ ") + fmt_tl("%s: keep the right side", field_label(*layout[row].field)) + keys(" (Ctrl+→)");
            return keys("→ ") + _("Keep the right side. Click again to reset.") + keys(" (Ctrl+→)");
        case Part::Reject:
            if (detail) return keys("← ") + fmt_tl("%s: take the left side", field_label(*layout[row].field)) + keys(" (Ctrl+←)");
            return keys("← ") + _("Take the left side. Click again to reset.") + keys(" (Ctrl+←)");
        case Part::Check: return _("Select this difference to decide it with the other selected ones (Space)");
        case Part::Overview: return _("Overview of all differences: click or drag to jump");
        case Part::Fold: return _("Click to show the hidden lines");
        default: return {};
    }
}

wxString ComparePanel::KeyLine() const {
    if (field_cursor)
        return fmt_tl("%s: Ctrl+Left takes the left side, Ctrl+Right keeps the right side, Esc leaves the field", field_label(*field_cursor));
    // the arrows outside the translated text, which wx reads as ASCII
    auto keys = [](char const* k) { return wxString::FromUTF8(k); };
    wxString text = keys("Ctrl+←→ ") + _("takes a side");
    if (DetailFields().size() > 1) text = keys("Ctrl+↑↓ ") + _("picks a field") + keys(" · ") + text;
    return text;
}

void ComparePanel::UpdateKeyLine() {
    auto text = KeyLine();
    if (hint->GetLabelText() != text) { hint->SetLabelText(text); hint->SetToolTip(text); }
}

bool ComparePanel::ChunkStart(int row) const {
    // Runs of shown differences of one kind; folds and detail rows break them.
    auto key = [this](int r) {
        auto const& lr = layout[r];
        if (lr.IsFold()) return -2;
        if (lr.InCard()) return -3;
        return ShownDiff(lr.entry) ? (int)state.Entries()[lr.entry].change : -1;
    };
    return row <= 0 || key(row - 1) != key(row) || key(row) < 0;
}

bool ComparePanel::ChunkEnd(int row) const {
    return row + 1 >= (int)layout.size() || ChunkStart(row + 1);
}

void ComparePanel::UpdateTitles() {
    // The file's name, and a badge on the open subtitles' side: what writing changes.
    bool project_right = state.ProjectSide() == Side::Right;
    left_title->SetLabel(project_right ? file_name : project_name);
    right_title->SetLabel(project_right ? project_name : file_name);
    left_target->Show(!project_right);
    right_target->Show(project_right);
    sheet->Layout();
}

void ComparePanel::UpdateFilterBar() {
    std::map<view::Category, int> counts;
    int all = 0;
    // Counted as shown: pending ones only, while only those are shown.
    view::Filter any_category = filter;
    any_category.category.reset();
    for (auto e : state.Diffs()) {
        if (!state.Visible(e, any_category)) continue;
        if (auto category = state.CategoryOf(e, filter.ignored)) { ++counts[*category]; ++all; }
    }
    // Categories with nothing to show are left out of the panel, which
    // fits what is left while it is open.
    bool refit = false;
    for (auto& [category, button] : category_buttons) {
        int n = category ? counts[*category] : all;
        auto label = category ? category_label(*category) : _("All");
        auto count = wxString::Format("%d", n);
        if (button->GetLabel() != label || button->GetTrailing() != count) { button->SetLabel(label); button->SetTrailing(count); refit = true; }
        button->SetAccent(filter.category == category);
        bool show = !category || n > 0 || filter.category == category;
        if (button->IsShown() != show) { button->Show(show); refit = true; }
    }
    if (refit && filter_popup->IsShown()) filter_popup->Refit();
    bool changed = false;
    // While the panel is closed, what it sets shows next to its button,
    // each with a click to take it off.
    std::vector<std::pair<wxString, std::function<void()>>> active;
    if (!filter_popup->IsShown()) {
        if (filter.category) active.emplace_back(category_label(*filter.category), [this] { filter.category.reset(); });
        if (filter.pending_only) active.emplace_back(_("Pending only"), [this] {
            filter.pending_only = false;
            pending_box->SetValue(false);
        });
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
        wxWindow* after = filter_button;
        for (auto& [label, clear] : active) {
            auto chip = new FlatButton(this, label + wxString::FromUTF8("  ×"));
            chip->SetToolTip(_("Take this filter off"));
            chip->SetLeastSize(wxSize(-1, FromDIP(26)));
            // Tab reaches them where they are, after the Filter button.
            chip->MoveAfterInTabOrder(after);
            after = chip;
            // Rebuilding the chips destroys this one: after its click is done,
            // the keyboard going back to the Filter button if it was on it.
            chip->Bind(wxEVT_BUTTON, [this, clear, chip](wxCommandEvent&) {
                bool focused = wxWindow::FindFocus() == chip;
                clear();
                CallAfter([this, focused] { ApplyFilter(); if (focused) filter_button->SetFocus(); });
            });
            chips->Add(chip, wxSizerFlags().Center().Border(wxRIGHT, FromDIP(4)));
        }
        changed = true;
    }
    if (changed) Layout();
}

void ComparePanel::Changed() {
    // Showing only what is pending, decisions and moving on change what is shown.
    if (filter.pending_only) {
        filter.keep = current;
        auto now = state.VisibleDiffs(filter);
        if (now != shown) { shown = std::move(now); Relayout(current); }
    }
    size_t total = shown.size();
    auto index = CurrentIndex();
    // "3 / 14": where the current difference is among those shown.
    counter->SetLabel(index ? wxString::Format("%d / %d", (int)*index + 1, (int)total) : wxString::Format("%d", (int)total));
    wxString where = index ? fmt_tl("Difference %d of %d", (int)*index + 1, (int)total) : fmt_tl("%d differences", (int)total);
    if (filter.Active()) where += fmt_tl(" (%d in total)", (int)state.Diffs().size());
    counter->SetToolTip(where);
    stats->Set((int)state.Count(Decision::Pending), (int)state.Count(Decision::Rejected),
        (int)state.Count(Decision::Accepted), (int)state.Count(Decision::Partial));
    size_t checked = 0;
    for (auto e : state.CheckedEntries()) checked += ShownDiff(e);
    checked_count->SetLabel(fmt_tl("%d selected", (int)checked));
    check_all->SetValue(total && checked == total);
    // What to do with the selected ones while there are any, else the
    // batch panel for all those shown.
    auto foot = batch_button->GetContainingSizer();
    if (foot->IsShown(select_group) != (checked > 0) || batch_button->IsShown() != (checked == 0)) {
        if (checked) batch_popup->Close();
        foot->Show(select_group, checked > 0);
        batch_button->Show(checked == 0);
        foot->Layout();
    }
    accept_checked->Enable(checked > 0);
    reject_checked->Enable(checked > 0);
    accept_all->Enable(total > 0); reject_all->Enable(total > 0);
    batch_button->Enable(total > 0);
    prev->Enable(total > 0); next->Enable(total > 0);
    reset_current->Enable(CurrentEntry() && state.GetDecision(*CurrentEntry()) != Decision::Pending);
    PlaceCard();
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
        // and what becomes of those not decided
        auto text = summarize(r);
        if (r.Ok() && state.Count(Decision::Pending))
            text += wxString::FromUTF8(" · ") + fmt_tl("%d pending leave the subtitles as they are", (int)state.Count(Decision::Pending));
        report->SetLabelText(text);
        report->SetToolTip(describe(r));
        report->SetForegroundColour(r.Ok() ? flat::Colours(this).dim : pal.del_mark);
        finish->SetLabel(fmt_tl("&Write to Project (%d)", (int)choices.size()));
        finish->Enable(r.Ok());
    }
    report->Refresh();
    finish->GetContainingSizer()->Layout();
    UpdateKeyLine();
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
    cursor_row = row;
    while (!GroupEnd(last)) ++last;
    right->Reveal(row, last); left->Reveal(row, last);
    if (sync) left->SetView(right->Top(), right->XOffset());
    Changed();
}

void ComparePanel::Select(size_t entry, Side from) {
    if (cursor_row < 0 || cursor_row >= (int)layout.size() || layout[cursor_row].entry != entry) cursor_row = row_of[entry];
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
    }
    UpdateKeyLine();
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

void ComparePanel::PlaceCursor(int row, Side from) {
    if (layout.empty()) return;
    cursor_row = std::clamp(row, 0, (int)layout.size() - 1);
    auto pane = PaneFor(from);
    pane->Reveal(cursor_row);
    if (sync) PaneFor(view::Other(from))->SetView(pane->Top(), pane->XOffset());
    else PaneFor(view::Other(from))->Reveal(cursor_row);
    left->Refresh(false); right->Refresh(false);
}

void ComparePanel::MoveCursor(int delta, Side from) {
    auto pane = PaneFor(from);
    int row = cursor_row;
    // Scrolled away from it: start from what is in view.
    if (row < pane->Top() || row >= pane->Top() + pane->VisibleRows())
        row = delta > 0 ? pane->Top() - 1 : pane->Top() + pane->VisibleRows();
    // The card's foot is its buttons' row, not a line to be on.
    int to = row + delta;
    if (to >= 0 && to < (int)layout.size() && layout[to].IsFoot()) to += delta > 0 ? 1 : -1;
    PlaceCursor(to, from);
}

void ComparePanel::ActivateCursor(Side from) {
    if (cursor_row < 0 || cursor_row >= (int)layout.size()) return;
    auto const& lr = layout[cursor_row];
    if (lr.IsFold()) { ExpandFold(cursor_row); PlaceCursor(cursor_row, from); }
    else if (lr.IsDetail()) SetFieldCursor(*lr.field);
    else if (ShownDiff(lr.entry)) { Select(lr.entry, from); PlaceCursor(row_of[lr.entry], from); }
}

void ComparePanel::CheckCursor() {
    if (cursor_row >= 0 && cursor_row < (int)layout.size()) {
        auto const& lr = layout[cursor_row];
        if (!lr.IsFold() && ShownDiff(lr.entry)) { ToggleCheck(lr.entry); return; }
    }
    if (auto entry = CurrentEntry()) ToggleCheck(*entry);
}

std::vector<tag_summary::Piece> const& ComparePanel::Summary(std::string const& text) const {
    auto it = summaries.find(text);
    if (it != summaries.end()) return it->second;
    // Only those lately drawn, as in the grid
    if (summaries.size() > 4000) summaries.clear();
    return summaries.emplace(text, tag_summary::Summarize(text)).first->second;
}

void ComparePanel::Scrolled(Side from) {
    if (sync) {
        auto src = PaneFor(from);
        PaneFor(view::Other(from))->SetView(src->Top(), src->XOffset());
    }
    // Painted now, both sides and the gutter together, rather than each
    // when the toolkit gets to it: in sync they would show apart a moment.
    left->Update(); gutter->Update(); right->Update();
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
    if (row < 0 || row >= (int)layout.size() || layout[row].IsFold() || layout[row].IsFoot()) return {};
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
