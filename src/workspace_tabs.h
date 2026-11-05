// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The main window's work area: the subtitle editor, and while a comparison is
// open a second tab holding it (docs/subtitle-compare-ui.md). The comparison
// session excludes every other edit, so while it is open the editor tab is
// locked and says so; while the comparison tab is in front, menu and hotkey
// commands other than application ones are refused.
#pragma once

#include <libaegisub/signal.h>

#include <wx/panel.h>

#include <string_view>

class wxButton;
class wxSimplebook;
class wxStaticText;
class wxToggleButton;
namespace agi { struct Context; }

/// A tab holding a comparison. It owns the comparison session.
class ComparisonPage : public wxPanel {
public:
    using wxPanel::wxPanel;
    /// End the comparison without writing, first confirming the decisions it
    /// would discard when @p ask. False if the user kept it open.
    virtual bool TryClose(bool ask) = 0;
    /// Called once the page has its size in the tab: show the first difference.
    virtual void Started() = 0;
};

class WorkspaceTabs final : public wxPanel {
    agi::Context *c;
    wxPanel *strip;
    wxToggleButton *edit_tab, *compare_tab;
    wxButton *close_tab;
    wxStaticText *notice;
    wxPanel *banner;
    wxStaticText *banner_text;
    wxSimplebook *book;
    wxWindow *editor = nullptr;
    ComparisonPage *page = nullptr;
    wxString name;
    bool last_applied = false;
    agi::signal::Connection theme_changed;

    void Select(bool comparison);
    void Update();
public:
    WorkspaceTabs(wxWindow *parent, agi::Context *c);
    ~WorkspaceTabs();

    /// Parent for the pages: the editor and comparison pages are created on it.
    wxWindow *Book() const;
    void SetEditor(wxWindow *editor);

    /// Add @p page as the comparison tab of the file @p name and bring it to the front.
    void OpenComparison(ComparisonPage *page, wxString const& name);
    /// Called by the page once its comparison ended: removes the tab.
    /// @p applied whether decisions were written to the subtitles.
    void ComparisonEnded(bool applied);
    /// End the open comparison, asking first when @p ask. True if none is open afterwards.
    bool CloseComparison(bool ask = true);

    ComparisonPage *Comparison() const { return page; }
    bool ComparisonInFront() const;
    void ShowEditor() { Select(false); }
    void ShowComparison() { Select(true); }
    /// Whether the last comparison ended by writing to the subtitles.
    bool LastApplied() const { return last_applied; }

    /// Whether the menu or hotkey command @p name may run now. When it may
    /// not, the tab strip says why.
    bool AllowsCommand(std::string_view name);
    /// Whether @p name may run now, without saying why not.
    bool Permits(std::string_view name) const;
};
