// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "workspace_tabs.h"

#include "format.h"
#include "include/aegisub/context.h"
#include "base_grid.h"
#include "theme.h"

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <utility>

WorkspaceTabs::WorkspaceTabs(wxWindow *parent, agi::Context *c)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN)
, c(c)
{
    SetName("workspace-tabs");

    // Tab strip at the bottom left, shown only while a comparison is open:
    // the tabs as VS Code's panel tabs, then a notice when a command is refused.
    strip = new wxPanel(this);
    strip->SetName("workspace-strip");
    edit_tab = new WorkspaceTab(strip, _("Subtitles"));
    edit_tab->SetName("workspace-tab-edit");
    edit_tab->SetToolTip(_("The subtitle editor. It is locked while the comparison is open."));
    compare_tab = new WorkspaceTab(strip, _("Compare"));
    compare_tab->SetName("workspace-tab-compare");
    compare_tab->SetToolTip(_("The open comparison"));
    close_tab = new WorkspaceTab(strip, "", true);
    close_tab->SetName("workspace-tab-close");
    close_tab->SetToolTip(_("End the comparison without writing to the subtitles"));
    notice = new wxStaticText(strip, -1, "");
    notice->SetName("workspace-notice");
    auto tabs = new wxBoxSizer(wxHORIZONTAL);
    tabs->Add(edit_tab, wxSizerFlags().Expand());
    tabs->Add(compare_tab, wxSizerFlags().Expand());
    tabs->Add(close_tab, wxSizerFlags().Expand().Border(wxRIGHT, FromDIP(12)));
    tabs->Add(notice, wxSizerFlags(1).Center());
    strip->SetSizer(tabs);
    // A line between it and the page above
    strip->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxPaintDC dc(strip);
        dc.SetBackground(wxBrush(strip->GetBackgroundColour()));
        dc.Clear();
        auto text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
        dc.SetPen(wxPen(theme::Mix(strip->GetBackgroundColour(), text, 0.15)));
        dc.DrawLine(0, 0, strip->GetClientSize().x, 0);
    });

    // The editor tab's lock notice.
    banner = new wxPanel(this);
    banner->SetName("workspace-banner");
    banner_text = new wxStaticText(banner, -1, "");
    banner_text->SetName("workspace-banner-text");
    auto back = new wxButton(banner, -1, _("Back to Comparison"));
    back->SetName("workspace-banner-back");
    auto end = new wxButton(banner, -1, _("End Comparison"));
    end->SetName("workspace-banner-end");
    end->SetToolTip(_("End the comparison without writing to the subtitles"));
    auto banner_sizer = new wxBoxSizer(wxHORIZONTAL);
    banner_sizer->Add(banner_text, wxSizerFlags(1).Center().Border(wxLEFT | wxRIGHT, 8));
    banner_sizer->Add(back, wxSizerFlags().Center().Border(wxTOP | wxBOTTOM | wxRIGHT, 4));
    banner_sizer->Add(end, wxSizerFlags().Center().Border(wxTOP | wxBOTTOM | wxRIGHT, 4));
    banner->SetSizer(banner_sizer);

    book = new wxSimplebook(this);
    book->SetName("workspace-book");

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(banner, wxSizerFlags().Expand());
    sizer->Add(book, wxSizerFlags(1).Expand());
    sizer->Add(strip, wxSizerFlags().Expand());
    SetSizer(sizer);

    edit_tab->on_click = [this] { Select(false); };
    compare_tab->on_click = [this] { Select(true); };
    back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Select(true); });
    close_tab->on_click = [this] { CloseComparison(); };
    end->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CloseComparison(); });
    theme_changed = theme::AddChangeListener([this] { Update(); });
    Update();
}

WorkspaceTabs::~WorkspaceTabs() {
    if (c->workspaceTabs == this) c->workspaceTabs = nullptr;
}

wxWindow *WorkspaceTabs::Book() const { return book; }

void WorkspaceTabs::SetEditor(wxWindow *window) {
    editor = window;
    book->InsertPage(0, editor, _("Subtitles"), true);
    Update();
}

void WorkspaceTabs::OpenComparison(ComparisonPage *p, wxString const& file) {
    page = p;
    name = file;
    last_applied = false;
    book->AddPage(page, name);
    Select(true);
    page->Started();
}

void WorkspaceTabs::ComparisonEnded(bool applied) {
    if (!page) return;
    last_applied = applied;
    auto old = page;
    page = nullptr;
    name.clear();
    // The page is still running the handler that ended it: remove it now and
    // destroy it once that handler returned.
    book->RemovePage(book->FindPage(old));
    old->Hide();
    CallAfter([old] { old->Destroy(); });
    Select(false);
    if (c->subsGrid) c->subsGrid->SetFocus();
}

bool WorkspaceTabs::CloseComparison(bool ask) {
    if (!page) return true;
    if (ask) Select(true); // the confirmation is about what this tab shows
    return page->TryClose(ask);
}

bool WorkspaceTabs::ComparisonInFront() const {
    return page && book->GetSelection() != wxNOT_FOUND && book->GetPage(book->GetSelection()) == page;
}

void WorkspaceTabs::Select(bool comparison) {
    if (comparison && !page) comparison = false;
    if (auto target = comparison ? static_cast<wxWindow*>(page) : editor) {
        int index = book->FindPage(target);
        if (index != wxNOT_FOUND && index != book->GetSelection()) book->ChangeSelection(index);
    }
    notice->SetLabel("");
    Update();
    if (comparison) page->SetFocus();
}

void WorkspaceTabs::Update() {
    bool open = page != nullptr, front = ComparisonInFront();
    auto colours = theme::UiColours();
    edit_tab->SetSelected(!front);
    compare_tab->SetSelected(front);
    strip->SetBackgroundColour(theme::Surface());
    notice->SetForegroundColour(theme::Mix(theme::Surface(), wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT), 0.6));
    strip->Refresh();
    if (open) {
        compare_tab->SetLabel(fmt_tl("Compare: %s", name));
        banner_text->SetLabel(fmt_tl("Comparing with %s. The subtitles cannot be edited until the comparison ends.", name));
    }
    banner->SetBackgroundColour(colours.notice_back);
    banner_text->SetForegroundColour(colours.notice_text);
    banner->Refresh();
    GetSizer()->Show(strip, open);
    GetSizer()->Show(banner, open && !front);
    Layout();
    changed();
}

bool WorkspaceTabs::Permits(std::string_view command) const {
    return !ComparisonInFront() || command.starts_with("app/") || command.starts_with("help/");
}

bool WorkspaceTabs::AllowsCommand(std::string_view command) {
    if (Permits(command)) return true;
    notice->SetLabel(_("This command is not available in the comparison tab. Switch to the Subtitles tab to use it."));
    strip->Layout();
    return false;
}

// WorkspaceTab

WorkspaceTab::WorkspaceTab(wxWindow *parent, wxString const& label, bool close)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
, label(label)
, close(close)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_PAINT, &WorkspaceTab::OnPaint, this);
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) { if (IsEnabled()) Click(); });
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hover = true; Refresh(false); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = false; Refresh(false); });
    theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void WorkspaceTab::SetLabel(wxString const& new_label) {
    if (label == new_label) return;
    label = new_label;
    InvalidateBestSize();
    GetParent()->Layout();
    Refresh(false);
}

void WorkspaceTab::SetSelected(bool on) {
    if (selected == on) return;
    selected = on;
    Refresh(false);
}

wxSize WorkspaceTab::DoGetBestClientSize() const {
    int height = GetCharHeight() + FromDIP(14);
    if (close) return wxSize(height - FromDIP(4), height);
    return wxSize(GetTextExtent(label).x + FromDIP(24), height);
}

void WorkspaceTab::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC buffered(this);
    wxGCDC dc(buffered);
    wxSize size = GetClientSize();
    wxColour back = GetParent()->GetBackgroundColour();
    wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
    wxColour accent = theme::UiColours().accent;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(back));
    dc.DrawRectangle(wxPoint(0, 0), size);

    if (close) {
        // A cross, on a rounded square under the mouse
        int box = std::min(size.x, size.y) - FromDIP(8);
        wxRect rect((size.x - box) / 2, (size.y - box) / 2, box, box);
        if (hover) {
            dc.SetBrush(wxBrush(theme::Mix(back, text, 0.12)));
            dc.DrawRoundedRectangle(rect, FromDIP(3));
        }
        int arm = FromDIP(4);
        wxPoint c(size.x / 2, size.y / 2);
        dc.SetPen(wxPen(theme::Mix(back, text, hover ? 0.9 : 0.6), std::max(1, FromDIP(1))));
        dc.DrawLine(c.x - arm, c.y - arm, c.x + arm, c.y + arm);
        dc.DrawLine(c.x - arm, c.y + arm, c.x + arm, c.y - arm);
        return;
    }

    // The text: full colour in front or under the mouse, dim otherwise
    dc.SetFont(GetFont());
    dc.SetTextForeground(theme::Mix(back, text, selected ? 1.0 : hover ? 0.85 : 0.6));
    wxSize extent = dc.GetTextExtent(label);
    int x = (size.x - extent.x) / 2;
    dc.DrawText(label, x, (size.y - extent.y) / 2);
    // The tab in front: an accent line under its text
    if (selected) {
        int thick = std::max(2, FromDIP(2));
        dc.SetBrush(wxBrush(accent));
        dc.DrawRectangle(x, size.y - thick - FromDIP(2), extent.x, thick);
    }
}
