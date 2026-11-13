// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <gtest/gtest.h>
#include <set>
#include "include/aegisub/context.h"
#include "ass_file.h"
#include "ass_dialogue.h"
#include "ass_attachment.h"
#include "ass_info.h"
#include "ass_style.h"
#include "subs_preview.h"
#include "subtitle_project_access.h"
#include "subtitle_row_list_edit.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "timeedit_ctrl.h"
#include "time_unit_toggle.h"
#include "flat_controls.h"
#include "keyboard_regions.h"
#include "base_grid.h"
#include "auto4_base.h"
#include "dialog_export_ebu3264.h"
#include "dialog_progress.h"
#include "options.h"
#include "command/command.h"
#include "include/aegisub/hotkey.h"
#include <libaegisub/hotkey.h>
#include <tuple>
#include <typeinfo>
#include "flat_dialog.h"
#include "flat_list.h"
#include "command/edit.h"
#include "subtitle_binding.h"
#include "search_replace_engine.h"
#include "text_selection_controller.h"
#include "dialog_style_editor.h"
#include "ass_export_filter.h"
#include "ass_exporter.h"
#include "export_fixstyle.h"
#include "export_framerate.h"
#include "dialogs.h"
#include "theme.h"
#include "command_palette.h"
#include "preferences.h"
#include "preferences_base.h"
#include "style_list.h"
#include "status_info.h"
#include <wx/sashwin.h>
#include <wx/checkbox.h>
#include "include/aegisub/toolbar.h"
#include "libresrc/libresrc.h"
#include "visual_tool_rectangle.h"
#include <wx/bmpbuttn.h>
#include <wx/menu.h>
#include "workspace_tabs.h"
#include "dialog_manager.h"
#include "compat.h"
#include "utils.h"
#include "audio_controller.h"
#include "audio_timing.h"
#include "audio_display.h"
#include "audio_box.h"
#include "audio_karaoke.h"
#include "audio_levels.h"
#include "audio_renderer_waveform.h"
#include "audio_marker.h"
#include "audio_colorscheme.h"
#include "audio_rendering_style.h"
#include "audio_edit_session.h"
#include "ass_karaoke.h"
#include "project.h"
#include "video_display.h"
#include "video_box.h"
#include "video_slider.h"
#include "video_time_label.h"
#include "video_zoom_control.h"
#include "style_edit.h"
#include "animation_page.h"
#include "animation_tags.h"
#include "style_look.h"
#include "script_panel.h"
#include "tag_summary.h"
#include "grid_column.h"

#include <wx/dcmemory.h>
#include <wx/image.h>
#include "style_panel.h"
#include "style_panel_parts.h"
#include "timing_adjust.h"
#include "timing_check.h"
#include "timing_page.h"
#include "video_controller.h"
#include "async_video_provider.h"
#include "video_frame.h"
#include "include/aegisub/video_provider.h"
#include "bilingual.h"
#include "keyframe_snap.h"
#include "speech_lines.h"
#include "speech_transcribe.h"
#include "scene_detect.h"
#include "magia_timeline.h"
#include "dialog_magia_timeline.h"
#include <signal.h>
#include <libaegisub/io.h>
#include <limits>
#include "visual_tool_drag.h"
#include "visual_tool_cross.h"
#include "visual_tool_scale.h"
#include "visual_tool_rotatexy.h"
#include "visual_tool_rotatez.h"
#include "visual_tool_clip.h"
#include "visual_tool_vector_clip.h"
#include "visual_tool_scroll.h"
#include "scroll_effect.h"
#include "gl_text.h"
#include "dialog_scroll_effect.h"
#include "visual_tool_rectangle.h"
#include "subtitle_format.h"
#include "subtitle_dialog_edit.h"
#include "dialog_rectangle.h"
#include "dialog_translation.h"
#include "dialogs.h"
#include "../support/live_model_test_access.h"
#include <wx/combobox.h>
#include <wx/toolbar.h>
#include <wx/evtloop.h>
#include <thread>
#include <fstream>
#include <chrono>
#include <libaegisub/ass/karaoke.h>
#include <libaegisub/keyframe.h>
#include <libaegisub/path.h>
#include <libaegisub/mru.h>
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>
#include <wx/dcclient.h>
#include <wx/modalhook.h>
#include <wx/msgdlg.h>
#include <wx/textctrl.h>
#include <wx/filedlg.h>
#include <wx/choicdlg.h>
#include <wx/listbox.h>
#include <wx/checklst.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/radiobut.h>
#include <wx/tglbtn.h>
#include <wx/dataview.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <functional>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <filesystem>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif
#include <boost/scope_exit.hpp>
#include <cmath>

namespace {
class AcceptMessages : public wxModalDialogHook {
public:
    AcceptMessages() { Register(); }
    int Enter(wxDialog* dialog) override { return dynamic_cast<wxMessageDialog*>(dialog) ? wxID_OK : wxID_NONE; }
};
class RunModal : public wxModalDialogHook {
    wxString title;
    std::function<void(wxDialog*)> action;
public:
    RunModal(wxString title,std::function<void(wxDialog*)> action):title(title),action(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if(dialog->GetTitle()!=title || !action)return wxID_NONE;
        dialog->CallAfter([this,dialog] {
            action(dialog);
            if(dialog->IsModal() && dialog->IsShown())dialog->EndModal(wxID_CANCEL);
        });
        return wxID_NONE;
    }
};
std::vector<wxWindow*> descendants(wxWindow* parent) {
    std::vector<wxWindow*> found;
    for(auto child:parent->GetChildren()) {
        found.push_back(child); auto nested=descendants(child); found.insert(found.end(),nested.begin(),nested.end());
    }
    return found;
}
void button(wxWindow* window, int id) {
    wxCommandEvent event(wxEVT_BUTTON,id); event.SetEventObject(window); window->GetEventHandler()->ProcessEvent(event);
}
struct testing_comparison_page final : ComparisonPage {
    using ComparisonPage::ComparisonPage;
    bool TryClose(bool) override { return true; }
    void Started() override {}
};
class ordinary_edit : public testing::Test {
protected:
    std::unique_ptr<agi::Context> c;
    wxFrame *window = nullptr;
    WorkspaceTabs *tabs = nullptr;
    SubsEditBox *box = nullptr;
    SubsTextEditCtrl *editor = nullptr;
    void SetUp() override {
        c = std::make_unique<agi::Context>();
        window = new wxFrame(nullptr, wxID_ANY, "isolated editing tests");
        c->parent = window;
        tabs = new WorkspaceTabs(window, c.get());
        tabs->SetSize(1200, 800);
        c->workspaceTabs = tabs;
        tabs->SetEditor(new wxPanel(tabs->Book(), -1));
        box = new SubsEditBox(window, c.get());
        for (auto child : box->GetChildren())
            if (auto text = dynamic_cast<SubsTextEditCtrl*>(child)) editor = text;
        ASSERT_NE(nullptr, editor);
        auto write = c->subtitleAccess->TryWrite();
        LiveWriteForTest(*c).LoadDefault(); LiveWriteForTest(*c).Commit("", AssFile::COMMIT_NEW);
        auto line = &LiveRead(*c).Events.front();
        c->selectionController->SetSelectionAndActiveIDs({line->Id}, line->Id);
    }
    void TearDown() override { delete window; c.reset(); }
    /// Drags capture the mouse, which GTK can do only for a control that
    /// has its native window. Make it, leaving the frame off the screen as
    /// the other tests have it: shown, the display would lay itself out
    /// again whenever the desktop sized the frame.
    static void RealizeForCapture(wxWindow *control) {
#ifdef __WXGTK__
        gtk_widget_realize(control->m_wxwindow ? control->m_wxwindow : control->m_widget);
#else
        (void)control;
#endif
    }
    void Input(std::string const& value) {
        editor->SetTextRaw(value.c_str());
        // Hidden controls do not receive all native WM_NOTIFY deliveries.
        // Deliver the same wx event to the production handler deterministically.
        wxStyledTextEvent event(wxEVT_STC_MODIFIED, editor->GetId());
        event.SetModificationType(wxSTC_MOD_INSERTTEXT | wxSTC_STARTACTION);
        event.SetEventObject(editor); editor->GetEventHandler()->ProcessEvent(event);
    }
};
TEST_F(ordinary_edit, real_editor_text_event_is_gated_and_preserves_undo_granularity) {
    auto id = LiveRead(*c).Events.front().Id;
    Input(" 中 文 {\\i1} ");
    EXPECT_EQ(" 中 文 {\\i1} ", LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(id, LiveRead(*c).Events.front().Id);
    EXPECT_FALSE(c->subsController->IsUndoStackEmpty());
    auto before = LiveRead(*c).Events.front().Text.get();
    auto comparison = c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    Input("blocked");
    EXPECT_EQ(before, LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(before, editor->GetTextRaw().data());
    comparison.reset();
    c->subsController->Undo(); EXPECT_EQ("", LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo(); EXPECT_EQ(before, LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, real_metadata_and_millisecond_time_events_share_multiline_write_lifetime) {
    {
        auto write=c->subtitleAccess->TryWrite();
        auto other=new AssDialogue; LiveWriteForTest(*c).Events.push_back(*other);
        LiveWriteForTest(*c).Commit("insert fixture", AssFile::COMMIT_DIAG_ADDREM);
        c->selectionController->SetSelectedIDs({(LiveRead(*c).Events.front()).Id,other->Id});
    }
    TimeEdit *start=nullptr; wxTextCtrl *layer=nullptr; wxTextCtrl *actor=nullptr;
    for (auto child:descendants(box)) {
        if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="Start time") start=t;
        else if (auto l=dynamic_cast<wxTextCtrl*>(child); l && l->GetToolTipText().StartsWith("Layer:")) layer=l;
        else if (auto a=dynamic_cast<wxTextCtrl*>(child); a && a->GetToolTipText().StartsWith("Actor name")) actor=a;
    }
    ASSERT_NE(nullptr,start); ASSERT_NE(nullptr,layer); ASSERT_NE(nullptr,actor);
    auto send=[&](wxWindow* child, int value, wxString text={}) {
        if (child==layer) {
            layer->ChangeValue(wxString::Format("%d",value));
            wxCommandEvent event(wxEVT_TEXT_ENTER,layer->GetId()); event.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(event);
            return;
        }
        if (child==actor) actor->ChangeValue(text);
        wxCommandEvent event(wxEVT_TEXT,child->GetId()); event.SetInt(value); event.SetString(text);
        event.SetEventObject(child); box->GetEventHandler()->ProcessEvent(event);
    };
    start->SetTime(1001); send(start,0);
    send(layer,7); send(actor,0,"actor");
    for(auto const& row:LiveRead(*c).Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    start->SetTime(1002); send(start,0); send(layer,8); send(actor,0,"blocked");
    for(auto const& row:LiveRead(*c).Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
}
TEST_F(ordinary_edit, grid_number_cell_direct_event_is_gated) {
    auto grid=new BaseGrid(window,c.get()); c->subsGrid=grid;
    { auto write=c->subtitleAccess->TryWrite(); LiveWriteForTest(*c).Commit("",AssFile::COMMIT_NEW); }
    auto click=[&] {
        wxMouseEvent event(wxEVT_LEFT_DOWN); event.SetPosition({1,grid->LineHeight()+2});
        event.SetEventObject(grid); grid->GetEventHandler()->ProcessEvent(event);
    };
    click(); EXPECT_TRUE(LiveRead(*c).Events.front().Comment);
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    click(); EXPECT_TRUE(LiveRead(*c).Events.front().Comment);
    comparison.reset(); click(); EXPECT_FALSE(LiveRead(*c).Events.front().Comment);
    c->subsGrid=nullptr;
}
TEST_F(ordinary_edit, paste_insert_delete_sort_and_history_restore_ids_order_and_selection) {
    auto original=LiveRead(*c).Events.front().Id;
    cmd::PasteSubtitleLines(c.get(), "Dialogue: 0,0:00:02.001,0:00:03.002,Default,B,0,0,0,,second\nDialogue: 1,0:00:01.001,0:00:02.002,Default,A,0,0,0,,first");
    ASSERT_EQ(3u,LiveRead(*c).Events.size());
    std::vector<int> pasted_ids;
    for(auto const& row:LiveRead(*c).Events)pasted_ids.push_back(row.Id);
    EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
    EXPECT_EQ(pasted_ids.front(),c->selectionController->ReadActiveLine()->Id);
    c->subsController->Undo(); ASSERT_EQ(1u,LiveRead(*c).Events.size()); EXPECT_EQ(original,LiveRead(*c).Events.front().Id);
    c->subsController->Redo(); ASSERT_EQ(3u,LiveRead(*c).Events.size());
    std::vector<int> restored;for(auto const& row:LiveRead(*c).Events)restored.push_back(row.Id);
    EXPECT_EQ(pasted_ids,restored); EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
    cmd::call("grid/sort/start/selected",c.get());
    EXPECT_EQ("first",LiveRead(*c).Events.front().Text.get()); EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    cmd::call("edit/line/delete",c.get()); ASSERT_EQ(1u,LiveRead(*c).Events.size()); EXPECT_EQ(original,LiveRead(*c).Events.front().Id);
    c->subsController->Undo(); EXPECT_EQ(3u,LiveRead(*c).Events.size());
    cmd::call("subtitle/insert/before",c.get()); EXPECT_EQ(4u,LiveRead(*c).Events.size());
    c->subsController->Undo(); EXPECT_EQ(3u,LiveRead(*c).Events.size());
}
TEST_F(ordinary_edit, binding_and_extradata_survive_undo_redo_and_blocked_public_entry) {
    cmd::PasteSubtitleLines(c.get(),"one\ntwo");
    BindSelectedSubtitles(c.get());
    auto group=GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()); ASSERT_FALSE(group.empty());
    auto extras=LiveRead(*c).Extradata.size();
    c->subsController->Undo(); EXPECT_TRUE(GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()).empty());
    c->subsController->Redo(); EXPECT_EQ(group,GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()));
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    UnbindSelectedSubtitles(c.get()); EXPECT_EQ(group,GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()));
    EXPECT_EQ(extras,LiveRead(*c).Extradata.size()); comparison.reset();
    UnbindSelectedSubtitles(c.get()); EXPECT_TRUE(GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()).empty());
}
TEST_F(ordinary_edit, nested_command_reuses_lease_and_commit_observers_cannot_edit_or_undo) {
    Input("keep");
    auto outer=c->subtitleAccess->TryWrite(); auto generation=c->subtitleEditGate.Revision().generation;
    cmd::call("edit/clear",c.get());
    EXPECT_EQ(generation+1,c->subtitleEditGate.Revision().generation); // Only Commit advanced it.
    bool visited=false;
    agi::signal::Connection observer=LiveRead(*c).AddCommitListener([&](int,const AssDialogue*) {
        visited=true; cmd::call("edit/clear",c.get()); c->subsController->Undo();
    });
    Input("notification"); EXPECT_TRUE(visited); EXPECT_EQ("notification",LiveRead(*c).Events.front().Text.get());
    outer.reset(); EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(ordinary_edit, undo_redo_reject_before_history_move_and_selection_notifications_cannot_reenter_history) {
    Input("one"); Input("two");
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    auto description=c->subsController->GetUndoDescription();
    c->subsController->Undo(); EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(description,c->subsController->GetUndoDescription()); comparison.reset();
    c->subsController->Undo(); EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get());
    comparison=c->subtitleEditGate.TryBeginComparison(); c->subsController->Redo();
    EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get()); comparison.reset();
    bool called=false;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&] {
        called=true; c->subsController->Undo();
    });
    c->subsController->Redo(); EXPECT_TRUE(called); EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, saved_modified_flag_tracks_ordinary_undo_redo_without_changing_save_policy) {
    Input("saved baseline");
    { auto write=c->subtitleAccess->TryWrite();c->subsController->Save(agi::fs::path("ordinary-saved.ass")); }
    EXPECT_FALSE(c->subsController->IsModified());
    cmd::call("edit/clear",c.get()); EXPECT_TRUE(c->subsController->IsModified());
    c->subsController->Undo(); EXPECT_FALSE(c->subsController->IsModified()); EXPECT_EQ("saved baseline",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo(); EXPECT_TRUE(c->subsController->IsModified());
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    c->subsController->Undo();EXPECT_TRUE(c->subsController->IsModified());EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, single_replacement_uses_real_engine_and_comparison_blocks_replace_all) {
    Input("alpha alpha");
    SearchReplaceSettings settings{}; settings.find="alpha";settings.replace_with="beta";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::ALL;
    c->search->Configure(settings); c->textSelectionController->SetSelection(0,5);
    EXPECT_TRUE(c->search->ReplaceNext()); EXPECT_EQ("beta alpha",LiveRead(*c).Events.front().Text.get());
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    EXPECT_FALSE(c->search->ReplaceAll()); EXPECT_FALSE(c->search->ReplaceNext());
    EXPECT_EQ("beta alpha",LiveRead(*c).Events.front().Text.get());
    comparison.reset();
    AcceptMessages messages;
    EXPECT_TRUE(c->search->ReplaceAll()); EXPECT_EQ("beta beta",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo(); EXPECT_EQ("beta alpha",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, canonical_unicode_find_is_readonly_and_returns_original_byte_offsets) {
    std::string original="x e\xCC\x81 {\\i1} e\xCC\x81 y"; Input(original);
    SearchReplaceSettings settings{}; settings.find="\xC3\xA9";settings.replace_with="Z";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::ALL;
    settings.skip_tags=true; settings.match_case=true;
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    auto revision=c->subtitleEditGate.Revision(); auto matcher=SearchReplaceEngine::GetMatcher(settings);
    auto match=matcher(&LiveRead(*c).Events.front(),0); ASSERT_TRUE(match);
    EXPECT_EQ(2u,match.start); EXPECT_EQ(5u,match.end);
    auto second=matcher(&LiveRead(*c).Events.front(),match.end); ASSERT_TRUE(second);
    EXPECT_EQ("e\xCC\x81",original.substr(second.start,second.end-second.start));
    EXPECT_EQ(original,LiveRead(*c).Events.front().Text.get()); EXPECT_EQ(revision,c->subtitleEditGate.Revision());
    comparison.reset(); c->search->Configure(settings); c->textSelectionController->SetSelection(2,5);
    EXPECT_TRUE(c->search->ReplaceNext()); EXPECT_EQ("x Z {\\i1} e\xCC\x81 y",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo(); EXPECT_EQ(original,LiveRead(*c).Events.front().Text.get());
    settings.use_regex=true;settings.find="(\xC3\xA9)";settings.replace_with="[$1]";
    c->search->Configure(settings); AcceptMessages messages;
    EXPECT_TRUE(c->search->ReplaceAll()); EXPECT_EQ("x [\xC3\xA9] {\\i1} [\xC3\xA9] y",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, real_style_editor_apply_is_gated_and_new_styles_keep_undo_redo) {
    AcceptMessages messages;
    wxArrayString fonts; fonts.Add("Arial");
    DialogStyleEditor dialog(window,LiveRead(*c).GetStyle("Default"),c.get(),nullptr,"Copied",fonts);
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    button(&dialog,wxID_APPLY); EXPECT_EQ(1u,LiveRead(*c).Styles.size()); EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Copied"));
    comparison.reset(); button(&dialog,wxID_APPLY);
    ASSERT_NE(nullptr,LiveRead(*c).GetStyle("Copied")); EXPECT_EQ(2u,LiveRead(*c).Styles.size());
    auto style=LiveRead(*c).GetStyle("Copied");
    button(&dialog,wxID_APPLY); EXPECT_EQ(style,LiveRead(*c).GetStyle("Copied")); // Apply again updates, no duplicate.
    c->subsController->Undo(); EXPECT_EQ(2u,LiveRead(*c).Styles.size());
    c->subsController->Undo(); EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Copied"));
    c->subsController->Redo(); EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Copied"));
}
TEST_F(ordinary_edit, style_editor_is_drawn_fields_that_write_the_style_on_apply) {
    AcceptMessages messages; wxArrayString fonts; fonts.Add("Arial");
    DialogStyleEditor dialog(window,LiveRead(*c).GetStyle("Default"),c.get(),nullptr,"",fonts);
    auto frame=[&](wxString const& label)->FlatFrame*{for(auto child:descendants(&dialog))if(auto f=dynamic_cast<FlatFrame*>(child);f && f->GetLabel()==label)return f;return nullptr;};
    auto size=frame("Size");ASSERT_NE(nullptr,size);
    auto size_text=dynamic_cast<wxTextCtrl*>(size->GetContent());ASSERT_NE(nullptr,size_text);
    EXPECT_EQ("48",size_text->GetValue());
    size_text->SetValue("60");size->Wheel(2);EXPECT_EQ("62",size_text->GetValue());
    size->Scrub(-3,false);size->Scrub(-2,true);EXPECT_EQ("60",size_text->GetValue()) << "dragged from where it was";
    auto outline=frame("Outline");ASSERT_NE(nullptr,outline);outline->Wheel(5);
    StylePanel::Toggle *bold=nullptr;StylePanel::PageSwitch *border=nullptr;
    for(auto child:descendants(&dialog)){
        if(auto t=dynamic_cast<StylePanel::Toggle*>(child);t && t->GetToolTipText()=="Bold")bold=t;
        if(auto b=dynamic_cast<StylePanel::PageSwitch*>(child))border=b;
    }
    ASSERT_NE(nullptr,bold);ASSERT_NE(nullptr,border);
    bold->on_click();EXPECT_EQ("on",bold->Shown());
    border->on_choose(1);EXPECT_EQ("Per line",border->Shown());
    EXPECT_EQ(48.,LiveRead(*c).GetStyle("Default")->fontsize) << "nothing written before Apply";
    button(&dialog,wxID_APPLY);
    auto style=LiveRead(*c).GetStyle("Default");
    EXPECT_EQ(60.,style->fontsize);EXPECT_TRUE(style->bold);EXPECT_EQ(3,style->borderstyle);EXPECT_DOUBLE_EQ(2.5,style->outline_w);
}
TEST_F(ordinary_edit, style_editor_rejects_stale_live_style_after_another_edit) {
    AcceptMessages messages; wxArrayString fonts; fonts.Add("Arial");
    DialogStyleEditor dialog(window,LiveRead(*c).GetStyle("Default"),c.get(),nullptr,"",fonts);
    Input("changed elsewhere"); auto revision=c->subsController->GetUndoDescription();
    button(&dialog,wxID_APPLY);
    EXPECT_EQ(revision,c->subsController->GetUndoDescription()); EXPECT_EQ(1u,LiveRead(*c).Styles.size());
}
TEST_F(ordinary_edit, script_properties_are_drawn_fields_written_on_ok) {
    RunModal hook("Script Properties",[&](wxDialog* dialog) {
        FlatCheck* scale=nullptr;std::vector<FlatChoice*> lists;wxTextCtrl* width=nullptr;
        for(auto child:descendants(dialog)){
            if(auto check=dynamic_cast<FlatCheck*>(child))scale=check;
            if(auto list=dynamic_cast<FlatChoice*>(child))lists.push_back(list);
            if(auto frame=dynamic_cast<FlatFrame*>(child);frame && frame->GetLabel()=="Width" && !width)width=dynamic_cast<wxTextCtrl*>(frame->GetContent());
        }
        ASSERT_NE(nullptr,scale);ASSERT_EQ(2u,lists.size());ASSERT_NE(nullptr,width);
        bool was=scale->GetValue();scale->Toggle();EXPECT_NE(was,scale->GetValue());
        FlatChoice* wrap=lists[0]->GetItems().size()==4?lists[0]:lists[1];
        wrap->Choose(2);width->ChangeValue("1280");
        button(dialog,wxID_OK);
    });
    auto scaled=LiveRead(*c).GetScriptInfo("ScaledBorderAndShadow");
    ShowPropertiesDialog(c.get());
    EXPECT_EQ("2",LiveRead(*c).GetScriptInfo("WrapStyle"));
    EXPECT_EQ("1280",LiveRead(*c).GetScriptInfo("PlayResX"));
    EXPECT_NE(scaled,LiveRead(*c).GetScriptInfo("ScaledBorderAndShadow"));
}
TEST_F(ordinary_edit, script_properties_event_rejects_comparison_and_preserves_undo) {
    auto original=std::string(LiveRead(*c).GetScriptInfo("Title"));
    RunModal hook("Script Properties",[&](wxDialog* dialog) {
        wxTextCtrl* title=nullptr;
        for(auto child:descendants(dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child)){title=text;break;}
        ASSERT_NE(nullptr,title);title->ChangeValue("isolated title");button(dialog,wxID_OK);
    });
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    ShowPropertiesDialog(c.get());EXPECT_EQ(original,LiveRead(*c).GetScriptInfo("Title"));comparison.reset();
    ShowPropertiesDialog(c.get());EXPECT_EQ("isolated title",LiveRead(*c).GetScriptInfo("Title"));
    c->subsController->Undo();EXPECT_EQ(original,LiveRead(*c).GetScriptInfo("Title"));
}
TEST_F(ordinary_edit, attachment_delete_event_is_gated_and_undo_restores_content) {
    std::ofstream data("attachment.ttf",std::ios::binary);data << "attachment fixture";data.close();
    { auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).InsertAttachment(agi::fs::path("attachment.ttf"));LiveWriteForTest(*c).Commit("attachment",AssFile::COMMIT_ATTACHMENT); }
    auto original=LiveRead(*c).Attachments.front().GetEntryData();
    RunModal hook("Attachment List",[&](wxDialog* dialog) {
        FlatList* list=nullptr;FlatButton* remove=nullptr;
        for(auto child:descendants(dialog)) {
            if(auto view=dynamic_cast<FlatList*>(child))list=view;
            if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
        }
        ASSERT_NE(nullptr,list);ASSERT_NE(nullptr,remove);list->SetSelection(0);
        wxCommandEvent event(wxEVT_BUTTON,remove->GetId());event.SetEventObject(remove);remove->GetEventHandler()->ProcessEvent(event);
    });
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    ShowAttachmentsDialog(c.get());EXPECT_EQ(1u,LiveRead(*c).Attachments.size());comparison.reset();
    ShowAttachmentsDialog(c.get());EXPECT_TRUE(LiveRead(*c).Attachments.empty());
    c->subsController->Undo();ASSERT_EQ(1u,LiveRead(*c).Attachments.size());EXPECT_EQ(original,LiveRead(*c).Attachments.front().GetEntryData());
}
TEST_F(ordinary_edit, real_style_import_and_delete_preserve_unrelated_data_and_history) {
    class ImportDialogs : public wxModalDialogHook {
    public:
        wxString path;
        int files=0;
        explicit ImportDialogs(wxString path):path(path) { Register(); }
        int Enter(wxDialog* dialog) override {
            if(auto file=dynamic_cast<wxFileDialog*>(dialog)) {
                ++files; file->SetPath(path);
                // GTK's chooser takes the path once it has read the folder: wait for it
                for(int i=0;i<200 && file->GetPath().empty();++i){wxYield();wxMilliSleep(10);}
                return wxID_OK;
            }
            if(dialog->GetTitle()=="Import Styles") { for(auto child:descendants(dialog))if(auto list=dynamic_cast<FlatList*>(child)){list->Check(0);list->Check(1);} return wxID_OK; }
            if(dynamic_cast<wxMessageDialog*>(dialog)) return wxID_YES;
            return wxID_NONE;
        }
    };
    AssStyle imported; imported.name="Imported";imported.fontsize=72;imported.UpdateData();
    AssStyle replaced; replaced.fontsize=70;replaced.UpdateData();
    std::ofstream data("styles.ass",std::ios::binary);
    data << "[Script Info]\nScriptType: v4.00+\n[V4+ Styles]\n" << replaced.GetEntryData() << '\n' << imported.GetEntryData() << '\n'; data.close();
    std::ofstream attachment("attachment.ttf",std::ios::binary);attachment << "fixture attachment";attachment.close();
    {
        auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).InsertAttachment(agi::fs::path("attachment.ttf"));
        auto extra=LiveWriteForTest(*c).AddExtradata("opaque","retained"); LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{extra};
        LiveWriteForTest(*c).Commit("fixture metadata",AssFile::COMMIT_ATTACHMENT|AssFile::COMMIT_EXTRADATA);
    }
    ShowStyleManagerDialog(c.get());
    wxDialog* manager=nullptr;
    for(auto top:wxTopLevelWindows) if(auto d=dynamic_cast<wxDialog*>(top);d && d->GetTitle()=="Styles Manager")manager=d;
    ASSERT_NE(nullptr,manager); manager->Hide();
    auto cleanup=std::unique_ptr<wxDialog,std::function<void(wxDialog*)>>(manager,[](wxDialog* d){d->Close();});
    FlatButton *import=nullptr,*remove=nullptr;StyleList *current=nullptr;wxWindow *down=nullptr,*sort=nullptr;
    for(auto child:descendants(manager)) {
        if(auto b=dynamic_cast<FlatButton*>(child); b && b->GetLabel()=="&Import from script...")import=b;
        if(child->GetParent()->GetLabel()=="Current script") {
            if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
            if(auto list=dynamic_cast<StyleList*>(child))current=list;
            if(child->GetToolTipText()=="Move style down")down=child;
            if(child->GetToolTipText()=="Sort styles alphabetically")sort=child;
        }
    }
    ASSERT_NE(nullptr,import); ASSERT_NE(nullptr,remove); ASSERT_NE(nullptr,current);
    ASSERT_NE(nullptr,down);ASSERT_NE(nullptr,sort);
    auto path=std::filesystem::absolute("styles.ass").wstring(); ImportDialogs dialogs{wxString(path)};
    auto click=[](wxWindow* control) { wxCommandEvent event(wxEVT_BUTTON,control->GetId());event.SetEventObject(control);control->GetEventHandler()->ProcessEvent(event); };
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    click(import);EXPECT_EQ(0,dialogs.files);EXPECT_EQ(1u,LiveRead(*c).Styles.size());comparison.reset();
    click(import);ASSERT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));EXPECT_EQ(72.,LiveRead(*c).GetStyle("Imported")->fontsize);
    EXPECT_EQ(70.,LiveRead(*c).GetStyle("Default")->fontsize);EXPECT_EQ(1,dialogs.files);
    EXPECT_EQ(1u,LiveRead(*c).Attachments.size());EXPECT_EQ("retained",LiveRead(*c).Extradata.front().value);
    current->DeselectAll();current->SetStringSelection("Default");click(down);
    EXPECT_EQ("Imported",LiveRead(*c).Styles.front().name);c->subsController->Undo();
    EXPECT_EQ("Default",LiveRead(*c).Styles.front().name);click(sort);c->subsController->Undo();
    current->DeselectAll();current->SetStringSelection("Imported");
    comparison=c->subtitleEditGate.TryBeginComparison();click(remove);EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));comparison.reset();
    click(remove);EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Imported"));
    c->subsController->Undo();EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));
    c->subsController->Undo();EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Imported"));EXPECT_EQ(48.,LiveRead(*c).GetStyle("Default")->fontsize);
    c->subsController->Redo();EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Imported"));
    EXPECT_EQ(1u,LiveRead(*c).Attachments.size());EXPECT_EQ("retained",LiveRead(*c).Extradata.front().value);
}

TEST_F(ordinary_edit, all_migrated_command_operators_reject_comparison_before_any_model_or_history_change) {
    Input("unchanged");
    auto id=LiveRead(*c).Events.front().Id;
    auto description=c->subsController->GetUndoDescription();
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    char const* commands[] = {
        "tool/resampleres",
        "edit/color/primary",
        "edit/color/secondary",
        "edit/color/outline",
        "edit/color/shadow",
        "edit/style/bold",
        "edit/style/italic",
        "edit/style/underline",
        "edit/style/strikeout",
        "edit/font",
        "edit/line/cut",
        "edit/line/delete",
        "edit/line/duplicate",
        "edit/line/split/after",
        "edit/line/split/before",
        "edit/line/join/as_karaoke",
        "edit/line/join/concatenate",
        "edit/line/join/keep_first",
        "edit/line/paste",
        "edit/line/paste/over",
        "edit/line/recombine",
        "edit/line/split/by_karaoke",
        "edit/line/split/estimate",
        "edit/line/split/preserve",
        "edit/line/split/video",
        "edit/redo",
        "edit/undo",
        "edit/line/reverse_move",
        "subtitle/bind",
        "subtitle/unbind",
        "edit/revert",
        "edit/clear",
        "edit/clear/text",
        "edit/insert_original",
        "grid/line/next/create",
        "grid/sort/actor",
        "grid/sort/actor/selected",
        "grid/sort/effect",
        "grid/sort/effect/selected",
        "grid/sort/end",
        "grid/sort/end/selected",
        "grid/sort/layer",
        "grid/sort/layer/selected",
        "grid/sort/start",
        "grid/sort/start/selected",
        "grid/sort/style",
        "grid/sort/style/selected",
        "grid/move/up",
        "grid/move/down",
        "grid/swap",
        "subtitle/insert/after",
        "subtitle/insert/after/videotime",
        "subtitle/insert/before",
        "subtitle/insert/before/videotime",
        "time/continuous/end",
        "time/continuous/start",
        "time/frame/current",
        "time/snap/end_video",
        "time/snap/scene",
        "time/lead/both",
        "time/lead/in",
        "time/lead/out",
        "time/length/increase",
        "time/length/increase/shift",
        "time/length/decrease",
        "time/length/decrease/shift",
        "time/start/increase",
        "time/start/decrease",
        "time/snap/start_video",
        "time/snap/scene_nearby",
        "time/next",
        "time/prev",
    };
    for(auto name:commands) {
        SCOPED_TRACE(name); (*cmd::get(name))(c.get());
        EXPECT_EQ(1u,LiveRead(*c).Events.size()); EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
        EXPECT_EQ("unchanged",LiveRead(*c).Events.front().Text.get());
        EXPECT_EQ(description,c->subsController->GetUndoDescription());
        EXPECT_EQ(id,c->selectionController->ReadActiveLine()->Id);
    }
    cmd::PasteSubtitleLines(c.get(),"blocked\nblocked"); EXPECT_EQ(1u,LiveRead(*c).Events.size());
}

TEST_F(ordinary_edit, command_palette_matches_word_starts_runs_and_pinyin) {
    auto keys = palette::MakeKeys(L"Open Subtitles...", "subtitle/open");
    auto match = palette::BestMatch(palette::NormaliseQuery(L"O S"), keys);
    ASSERT_TRUE(match);
    EXPECT_EQ((std::vector<size_t>{0, 5}), match->positions) << "the s that starts a word";
    EXPECT_FALSE(palette::BestMatch(L"xyz", keys));
    auto run = palette::BestMatch(L"sub", keys), scattered = palette::BestMatch(L"sbs", keys);
    ASSERT_TRUE(run && scattered);
    EXPECT_GT(run->score, scattered->score);
    auto by_id = palette::BestMatch(L"subtitleopen", keys);
    ASSERT_TRUE(by_id);
    EXPECT_TRUE(by_id->positions.empty()) << "the command id is searched but not drawn";

    ASSERT_EQ(L"zi", palette::Pinyin(L'\u5b57')) << "ICU's Han-Latin transliterator";
    auto chinese = palette::MakeKeys(L"\u6253\u5f00\u5b57\u5e55...", "subtitle/open"); // 打开字幕
    auto initials = palette::BestMatch(L"dkzm", chinese);
    ASSERT_TRUE(initials);
    EXPECT_EQ((std::vector<size_t>{0, 1, 2, 3}), initials->positions);
    auto syllables = palette::BestMatch(L"zimu", chinese);
    ASSERT_TRUE(syllables);
    EXPECT_EQ((std::vector<size_t>{2, 3}), syllables->positions);
    auto characters = palette::BestMatch(palette::NormaliseQuery(L"\u5b57\u5e55"), chinese);
    ASSERT_TRUE(characters);
    EXPECT_EQ((std::vector<size_t>{2, 3}), characters->positions);
}

TEST_F(ordinary_edit, command_palette_matches_words_in_any_order_english_names_menus_and_shortcuts) {
    // Words in any order, each where it fits best.
    auto keys = palette::MakeKeys(L"Hide Tags", "grid/tags/hide");
    auto swapped = palette::MatchQuery(L"tags hide", keys);
    ASSERT_TRUE(swapped);
    EXPECT_EQ((std::vector<size_t>{0, 1, 2, 3, 5, 6, 7, 8}), swapped->positions);
    EXPECT_FALSE(palette::MatchQuery(L"tags xyz", keys)) << "every word has to match";
    EXPECT_TRUE(palette::MatchQuery(L"  hide   tags ", keys));

    // The English name in a translated interface, drawn below the name.
    auto translated = palette::MakeKeys(L"\u5e73\u79fb\u65f6\u95f4...", "time/shift", L"Shift Times...", L"\u65f6\u95f4\u8f74"); // 平移时间, 时间轴
    auto english = palette::MatchQuery(L"shift times", translated);
    ASSERT_TRUE(english);
    EXPECT_TRUE(english->positions.empty());
    EXPECT_EQ((std::vector<size_t>{0, 1, 2, 3, 4, 6, 7, 8, 9, 10}), english->original_positions);
    auto native = palette::MatchQuery(L"shift times", palette::MakeKeys(L"Shift Times...", "time/shift"));
    ASSERT_TRUE(native);
    EXPECT_GT(native->score, english->score) << "the shown name before the English one";
    EXPECT_EQ(2u, palette::MakeKeys(L"Open", "", L"open").size()) << "no English key when it is the name";

    // The menu: "sjz py" is 时间轴 (the menu) then 平移 (the name).
    auto by_menu = palette::MatchQuery(L"sjz py", translated);
    ASSERT_TRUE(by_menu);
    EXPECT_EQ((std::vector<size_t>{0, 1}), by_menu->positions);
    EXPECT_EQ((std::vector<size_t>{0, 1, 2}), by_menu->path_positions);
    auto name_only = palette::MatchQuery(L"py", translated), menu_only = palette::MatchQuery(L"sj", palette::MakeKeys(L"\u6253\u5f00", "x", L"", L"\u65f6\u95f4"));
    ASSERT_TRUE(name_only && menu_only);
    EXPECT_GT(name_only->score, menu_only->score) << "the name before the menu";

    // Shortcuts as typed.
    EXPECT_EQ(L"ctrl-shift-p", palette::CanonicalHotkey(L"Shift+Ctrl+P", true));
    EXPECT_EQ(L"ctrl-shift-p", palette::CanonicalHotkey(L"ctrl shift p", true));
    EXPECT_EQ(L"ctrl-shift-p", palette::CanonicalHotkey(L"Ctrl-Shift-P", false));
    EXPECT_EQ(L"ctrl--", palette::CanonicalHotkey(L"Ctrl--", true));
    EXPECT_EQ(L"f6", palette::CanonicalHotkey(L"F6", true));
    EXPECT_EQ(L"ctrl-shift-", palette::CanonicalHotkey(L"ctrl+shift+", true));
    EXPECT_EQ(L"", palette::CanonicalHotkey(L"ctrl shift", true)) << "modifiers alone, no '+' after";
    EXPECT_EQ(L"", palette::CanonicalHotkey(L"hide tags", true));
    EXPECT_EQ(L"", palette::CanonicalHotkey(L"a", true)) << "one letter is a name being typed";
    EXPECT_EQ(L"a", palette::CanonicalHotkey(L"A", false));
}

TEST_F(ordinary_edit, command_palette_finds_dialogue_as_runs_and_reads_goto_queries) {
    // Dialogue: each word one run, in the text or its pinyin, not scattered.
    auto keys = palette::MakeTextKeys(L"\u6253\u5f00\u5b57\u5e55\u6587\u4ef6"); // 打开字幕文件
    auto syllables = palette::MatchQuery(L"zimu", keys, true);
    ASSERT_TRUE(syllables);
    EXPECT_EQ((std::vector<size_t>{2, 3}), syllables->positions);
    EXPECT_FALSE(palette::MatchQuery(L"imu", keys, true)) << "pinyin runs start a syllable";
    EXPECT_FALSE(palette::MatchQuery(L"dz", keys, true)) << "initials not side by side";
    EXPECT_TRUE(palette::MatchQuery(L"dz", keys, false)) << "as a command name it would match";
    auto words = palette::MatchQuery(L"wj dk", keys, true);
    ASSERT_TRUE(words);
    EXPECT_EQ((std::vector<size_t>{0, 1, 4, 5}), words->positions);
    auto english = palette::MatchQuery(L"orl", palette::MakeTextKeys(L"Hello world"), true);
    ASSERT_TRUE(english);
    EXPECT_EQ((std::vector<size_t>{7, 8, 9}), english->positions) << "plain text: anywhere";

    EXPECT_EQ(120, palette::ParseGoto(L" 120 ").line);
    EXPECT_EQ(-1, palette::ParseGoto(L"120").time);
    EXPECT_EQ(83450, palette::ParseGoto(L"1:23.45").time);
    EXPECT_EQ(3723000, palette::ParseGoto(L"1:02:03").time);
    EXPECT_EQ(83000, palette::ParseGoto(L"1\uff1a23").time) << "a Chinese input method's colon";
    EXPECT_EQ(12, palette::ParseGoto(L"\uff11\uff12").line) << "full-width digits";
    EXPECT_EQ(0, palette::ParseGoto(L"abc").line);
    EXPECT_EQ(-1, palette::ParseGoto(L"abc").time);
    EXPECT_EQ(0, palette::ParseGoto(L"").line);
    EXPECT_EQ(-1, palette::ParseGoto(L":").time) << "no digits";
}

TEST_F(ordinary_edit, command_palette_finds_lines_and_goes_to_lines_and_times) {
    std::vector<int> ids{LiveRead(*c).Events.front().Id};
    {
        auto write = c->subtitleAccess->TryWrite();
        auto& file = LiveWriteForTest(*c);
        struct { int start, end; char const *text; bool comment; } rows[] = {
            {1000, 2000, "{\\i1}Hello\\Nworld", false},
            {3000, 5000, "\u6253\u5f00\u5b57\u5e55\u6587\u4ef6", false},
            {6000, 7000, "a note world", true},
        };
        for (auto const& row : rows) {
            auto line = new AssDialogue;
            line->Start = row.start; line->End = row.end;
            line->Text = row.text; line->Comment = row.comment;
            line->Actor = "Kei";
            file.Events.push_back(*line);
            ids.push_back(line->Id);
        }
        file.Commit("", AssFile::COMMIT_DIAG_ADDREM);
    }

    CommandPalette palette(c.get());
    palette.SetQuery("@");
    EXPECT_EQ(CommandPalette::Mode::Lines, palette.CurrentMode());
    EXPECT_TRUE(palette.Targets().empty());
    EXPECT_FALSE(palette.EmptyMessage().empty()) << "says what to type";

    palette.SetQuery("@world");
    ASSERT_EQ(2u, palette.Targets().size()) << "a comment is found too";
    EXPECT_TRUE(palette.Shown().empty()) << "no commands in this mode";
    auto const& hello = palette.Targets()[0];
    EXPECT_EQ(ids[1], hello.line_id);
    EXPECT_EQ("Hello world", hello.text) << "tags dropped, \\N a space";
    EXPECT_EQ((std::vector<size_t>{6, 7, 8, 9, 10}), hello.match.positions);
    EXPECT_EQ("0:00:01.00", hello.aside);
    EXPECT_TRUE(hello.detail.StartsWith("#2"));
    EXPECT_TRUE(hello.detail.Contains("Kei"));
    EXPECT_TRUE(palette.Targets()[1].comment);

    palette.SetQuery(wxString::FromUTF8("\uff20zimu")); // ＠, as a Chinese input method types it
    ASSERT_EQ(1u, palette.Targets().size());
    EXPECT_EQ(ids[2], palette.Targets()[0].line_id);
    palette.SetQuery("@world hello");
    ASSERT_EQ(1u, palette.Targets().size());
    palette.SetQuery("@hw");
    EXPECT_TRUE(palette.Targets().empty());
    EXPECT_FALSE(palette.EmptyMessage().empty());

    // ':' a line number
    palette.SetQuery(":3");
    EXPECT_EQ(CommandPalette::Mode::Goto, palette.CurrentMode());
    ASSERT_EQ(1u, palette.Targets().size());
    EXPECT_EQ(ids[2], palette.Targets()[0].line_id);
    palette.SetQuery(":99");
    EXPECT_TRUE(palette.Targets().empty());
    EXPECT_TRUE(palette.EmptyMessage().Contains("99"));
    palette.SetQuery(":");
    EXPECT_TRUE(palette.Targets().empty());
    EXPECT_FALSE(palette.EmptyMessage().empty());

    // ...or a time: with no video, the line on screen then first; seeking
    // is there but not available.
    palette.SetQuery(":0:00:03.5");
    ASSERT_EQ(2u, palette.Targets().size());
    EXPECT_EQ(ids[2], palette.Targets()[0].line_id);
    EXPECT_EQ(3500, palette.Targets()[1].time);
    EXPECT_FALSE(palette.Targets()[1].enabled);
    palette.SetQuery(":5.5");
    ASSERT_EQ(1u, palette.Targets().size()) << "nothing on screen or after but a comment";
    EXPECT_EQ(5500, palette.Targets()[0].time);

    palette.SetQuery(":0:00:03.5");
    palette.Select(1);
    EXPECT_FALSE(palette.Accept()) << "no video to seek";
    palette.Select(0);
    EXPECT_TRUE(palette.Accept());
    ASSERT_TRUE(palette.ChosenTarget());
    palette.GoToChosen();
    EXPECT_EQ(ids[2], c->selectionController->ActiveID());
    EXPECT_EQ(std::set<int>{ids[2]}, c->selectionController->SelectedIDs());

    // Through the command: the line is gone to once the palette has closed.
    struct Choose : wxModalDialogHook {
        Choose() { Register(); }
        int Enter(wxDialog *dialog) override {
            auto palette = dynamic_cast<CommandPalette*>(dialog);
            if (!palette) return wxID_NONE;
            palette->SetQuery(wxString::FromUTF8("\uff1a2"));
            palette->CallAfter([palette] { EXPECT_TRUE(palette->Accept()); });
            return wxID_NONE;
        }
        void Exit(wxDialog *) override {}
    } choose;
    cmd::call("app/command_palette", c.get());
    EXPECT_EQ(ids[1], c->selectionController->ActiveID());

    // While the comparison tab is in front the lines are shown, not gone to.
    auto page = new testing_comparison_page(tabs->Book());
    tabs->OpenComparison(page, "other.ass");
    CommandPalette locked(c.get());
    locked.SetQuery("@world");
    ASSERT_FALSE(locked.Targets().empty());
    EXPECT_FALSE(locked.Targets()[0].enabled);
    EXPECT_FALSE(locked.Accept());
    tabs->ComparisonEnded(false);
}

TEST_F(ordinary_edit, command_palette_finds_settings_and_opens_them_searched_for) {
    CommandPalette palette(c.get());
    palette.SetQuery(",");
    EXPECT_EQ(CommandPalette::Mode::Settings, palette.CurrentMode());
    EXPECT_TRUE(palette.Targets().empty());
    EXPECT_FALSE(palette.EmptyMessage().empty());

    palette.SetQuery(", seek line start");
    ASSERT_FALSE(palette.Targets().empty());
    auto const& seek = palette.Targets()[0];
    EXPECT_EQ("Seek video to line start on selection change", seek.text);
    EXPECT_EQ(seek.text, seek.setting);
    EXPECT_FALSE(seek.aside.empty()) << "the page it is on";
    EXPECT_FALSE(seek.match.positions.empty());

    palette.SetQuery(wxString::FromUTF8("\uff0caudio")); // ，, as a Chinese input method types it
    auto const& audio = palette.Targets();
    ASSERT_FALSE(audio.empty());
    EXPECT_TRUE(std::any_of(audio.begin(), audio.end(), [](auto const& t) { return t.aside.empty() && t.text == "Audio"; })) << "the page itself";
    palette.SetQuery(",qqqqzzzz");
    EXPECT_TRUE(palette.Targets().empty());

    // Chosen: once the palette has closed, the settings open searched for it.
    palette.SetQuery(", seek line start");
    ASSERT_TRUE(palette.Accept());
    struct Look : wxModalDialogHook {
        bool seen = false;
        Look() { Register(); }
        int Enter(wxDialog *dialog) override {
            auto prefs = dynamic_cast<Preferences*>(dialog);
            if (!prefs) return wxID_NONE;
            seen = true;
            EXPECT_FALSE(prefs->MatchingPages().empty());
            EXPECT_EQ(prefs->MatchingPages().front(), prefs->CurrentPage());
            EXPECT_FALSE(prefs->MatchingControls(prefs->CurrentPage()).empty());
            return wxID_CANCEL;
        }
        void Exit(wxDialog *) override {}
    } look;
    palette.GoToChosen();
    EXPECT_TRUE(look.seen);
}

TEST_F(ordinary_edit, command_palette_orders_by_use_and_remembers_what_was_chosen_for_a_query) {
    // The bonus: more for more uses, less as they get older, never much.
    EXPECT_EQ(0, palette::UsageBonus(0, 0));
    EXPECT_GT(palette::UsageBonus(3, 0), palette::UsageBonus(1, 0));
    EXPECT_GT(palette::UsageBonus(1, 0), palette::UsageBonus(1, 2 * 86400));
    EXPECT_GT(palette::UsageBonus(1, 2 * 86400), palette::UsageBonus(1, 60 * 86400));
    EXPECT_EQ(24, palette::UsageBonus(1000, 0));
    auto use = palette::ParseUsage("time/shift\t3\t1700000000");
    ASSERT_TRUE(use);
    EXPECT_EQ("time/shift", use->name);
    EXPECT_EQ(3, use->count);
    EXPECT_EQ(1700000000, use->last);
    EXPECT_EQ("time/shift\t3\t1700000000", palette::FormatUsage(*use));
    for (auto bad : {"", "time/shift", "time/shift\tx\t1", "\t1\t1", "a\t0\t1"})
        EXPECT_FALSE(palette::ParseUsage(bad)) << bad;

    auto clear = [] {
        for (auto option : {"App/Command Palette/Recent", "App/Command Palette/Usage", "App/Command Palette/Queries"})
            OPT_SET(option)->SetListString({});
    };

    // Before uses were counted: the recent list is read as one use each.
    clear();
    OPT_SET("App/Command Palette/Recent")->SetListString({"app/exit", "grid/tags/hide"});
    {
        CommandPalette old(c.get());
        auto order = old.Shown();
        ASSERT_GE(order.size(), 2u);
        EXPECT_EQ("app/exit", order[0]);
        EXPECT_EQ("grid/tags/hide", order[1]);
    }

    // The first use counted is one, not one more than the recent list
    // it was read from before it was written.
    clear();
    CommandPalette::Remember("app/exit");
    EXPECT_TRUE(OPT_GET("App/Command Palette/Usage")->GetListString().at(0).starts_with("app/exit\t1\t"));

    // Used more often comes before used once more lately.
    clear();
    for (int i = 0; i < 3; ++i) CommandPalette::Remember("app/exit");
    CommandPalette::Remember("grid/tags/hide");
    EXPECT_EQ("grid/tags/hide\t1", OPT_GET("App/Command Palette/Usage")->GetListString().at(0).substr(0, 16));
    {
        CommandPalette often(c.get());
        auto order = often.Shown();
        ASSERT_GE(order.size(), 2u);
        EXPECT_EQ("app/exit", order[0]);
        EXPECT_EQ("grid/tags/hide", order[1]);
        EXPECT_GT(often.ShownItem(0).usage_bonus, often.ShownItem(1).usage_bonus);
    }

    // What was chosen for a query comes first for it, and gains a little
    // while the query is being typed.
    clear();
    std::string wanted;
    {
        CommandPalette before(c.get());
        before.SetQuery("sa");
        auto found = before.Shown();
        ASSERT_GE(found.size(), 3u);
        wanted = found[2];
    }
    CommandPalette::Remember(wanted, "S A");
    EXPECT_EQ("sa\t" + wanted, OPT_GET("App/Command Palette/Queries")->GetListString().at(0)) << "kept lower case, without spaces";
    {
        CommandPalette after(c.get());
        after.SetQuery("sa");
        ASSERT_FALSE(after.Shown().empty());
        EXPECT_EQ(wanted, after.Shown()[0]);
        int remembered = after.ShownMatch(0).score;
        after.SetQuery("s");
        auto partly = after.Shown();
        auto at = std::find(partly.begin(), partly.end(), wanted);
        ASSERT_NE(partly.end(), at);
        EXPECT_LT(after.ShownMatch(at - partly.begin()).score, remembered);
    }
    CommandPalette::Remember("app/exit", "sa");
    auto queries = OPT_GET("App/Command Palette/Queries")->GetListString();
    EXPECT_EQ("sa\tapp/exit", queries.at(0));
    EXPECT_EQ(1, std::count_if(queries.begin(), queries.end(), [](auto const& q) { return q.starts_with("sa\t"); })) << "one choice a query, the latest";
    clear();
}

TEST_F(ordinary_edit, command_palette_lists_commands_remembers_choices_and_runs_after_closing) {
    for (auto option : {"App/Command Palette/Recent", "App/Command Palette/Usage", "App/Command Palette/Queries"})
        OPT_SET(option)->SetListString({});
    {
        CommandPalette palette(c.get());
        auto all = palette.Shown();
        EXPECT_GT(all.size(), 200u);
        EXPECT_EQ(all.end(), std::find(all.begin(), all.end(), "app/command_palette"));
        EXPECT_TRUE(std::none_of(all.begin(), all.end(), [](auto const& name) { return name.starts_with("recent/"); }));
        auto exit = std::find(all.begin(), all.end(), "app/exit");
        ASSERT_NE(all.end(), exit);
        EXPECT_EQ("Ctrl-Q", palette.ShownItem(exit - all.begin()).hotkey);

        palette.SetQuery("copy coordinates");
        ASSERT_FALSE(palette.Shown().empty());
        EXPECT_EQ("video/copy_coordinates", palette.Shown()[0]);
        EXPECT_FALSE(palette.ShownItem(0).enabled) << "no video is open";
        EXPECT_FALSE(palette.Accept());
        EXPECT_TRUE(palette.Chosen().empty());

        palette.SetQuery("hide tags");
        ASSERT_FALSE(palette.Shown().empty());
        EXPECT_EQ("grid/tags/hide", palette.Shown()[0]);
        palette.SetQuery("qqqqzzzz");
        EXPECT_TRUE(palette.Shown().empty());
        EXPECT_FALSE(palette.Accept());

        // Words in any order; the menu each command is in.
        palette.SetQuery("tags hide");
        ASSERT_FALSE(palette.Shown().empty());
        EXPECT_EQ("grid/tags/hide", palette.Shown()[0]);
        EXPECT_FALSE(palette.ShownItem(0).path.empty());
        palette.SetQuery("timing shift");
        auto timing = palette.Shown();
        ASSERT_GE(timing.size(), 1u);
        auto shift = std::find(timing.begin(), timing.end(), "time/shift");
        ASSERT_NE(timing.end(), shift);
        EXPECT_LT(shift - timing.begin(), 3);
        EXPECT_FALSE(palette.ShownMatch(shift - timing.begin()).path_positions.empty());

        // By shortcut, in any spelling, the shortcut marked.
        palette.SetQuery("q+ctrl");
        ASSERT_FALSE(palette.Shown().empty());
        EXPECT_EQ("app/exit", palette.Shown()[0]);
        EXPECT_EQ(L"Ctrl-Q", palette.ShownMatch(0).hotkey);
        palette.SetQuery("ctrl+");
        auto with_ctrl = palette.Shown();
        EXPECT_GT(with_ctrl.size(), 10u);
        EXPECT_NE(with_ctrl.end(), std::find(with_ctrl.begin(), with_ctrl.end(), "app/exit"));
    }

    // Through the command: the palette closes, then the chosen command runs.
    OPT_SET("Subtitle/Grid/Hide Overrides")->SetInt(0);
    // The query is typed before the palette is first drawn: the fixture has
    // no video display, which some commands' state needs (the app always has one).
    struct Choose : wxModalDialogHook {
        Choose() { Register(); }
        int Enter(wxDialog *dialog) override {
            auto palette = dynamic_cast<CommandPalette*>(dialog);
            if (!palette) return wxID_NONE;
            palette->SetQuery("grid tags hide");
            palette->CallAfter([palette] { EXPECT_TRUE(palette->Accept()); });
            return wxID_NONE;
        }
        void Exit(wxDialog *) override {}
    } choose;
    cmd::call("app/command_palette", c.get());
    EXPECT_EQ(2, OPT_GET("Subtitle/Grid/Hide Overrides")->GetInt());
    EXPECT_EQ("grid/tags/hide", OPT_GET("App/Command Palette/Recent")->GetListString().at(0));

    // Recent first when nothing is typed; a setting that is on is marked.
    CommandPalette::Remember("app/exit");
    CommandPalette again(c.get());
    auto order = again.Shown();
    ASSERT_GE(order.size(), 2u);
    EXPECT_EQ("app/exit", order[0]);
    EXPECT_EQ("grid/tags/hide", order[1]);
    EXPECT_TRUE(again.ShownItem(1).checkable && again.ShownItem(1).checked);

    // While the comparison tab is in front only application commands run.
    auto page = new testing_comparison_page(tabs->Book());
    tabs->OpenComparison(page, "other.ass");
    CommandPalette locked(c.get());
    locked.SetQuery("hide tags");
    ASSERT_FALSE(locked.Shown().empty());
    EXPECT_FALSE(locked.ShownItem(0).enabled);
    locked.SetQuery("app/exit");
    ASSERT_FALSE(locked.Shown().empty());
    EXPECT_TRUE(locked.ShownItem(0).enabled);
    tabs->ComparisonEnded(false);
}

double MeanLightness(wxImage const& image, bool *any_dark_inside = nullptr) {
    double total = 0, sum = 0;
    auto alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
    int w = image.GetWidth(), h = image.GetHeight();
    if (any_dark_inside) *any_dark_inside = false;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int i = y * w + x, a = alpha ? alpha[i] : 255;
        if (a < 128) continue;
        auto rgb = image.GetData() + i * 3;
        double l = (std::max({rgb[0], rgb[1], rgb[2]}) + std::min({rgb[0], rgb[1], rgb[2]})) / 510.0;
        total += 1; sum += l;
        if (any_dark_inside && l < 0.25 && x > 0 && y > 0 && x < w - 1 && y < h - 1) {
            bool inside = true;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (alpha && alpha[(y + dy) * w + x + dx] < 128) inside = false;
            if (inside) *any_dark_inside = true;
        }
    }
    return total ? sum / total : 0;
}

wxImage IconImage(wxBitmapBundle const& bundle, int size) {
    return bundle.GetBitmap(wxSize(size, size)).ConvertToImage();
}

TEST_F(ordinary_edit, dark_appearance_lightens_dark_icons_and_keeps_coloured_ones) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Theme")->SetInt(1); OPT_SET("App/Classic Icons")->SetBool(false); };
    OPT_SET("App/Theme")->SetInt(1);
    OPT_SET("App/Classic Icons")->SetBool(true); // The coloured bitmaps
    theme::Init();
    ASSERT_FALSE(libresrc_dark());
    // Only Windows with wxWidgets 3.3 draws its own controls dark; elsewhere
    // borders are left to the toolkit's theme.
#if !defined(__WXMSW__)
    EXPECT_FALSE(theme::NativeDark());
#endif
    if (!theme::NativeDark()) EXPECT_EQ(long(wxBORDER_SUNKEN), theme::Border(wxBORDER_SUNKEN));
    auto bold_light = IconImage(cmd::get("edit/style/bold")->Icon(16), 16);
    auto save_light = IconImage(cmd::get("subtitle/save")->Icon(24), 24);
    EXPECT_LT(MeanLightness(bold_light), 0.3);

    OPT_SET("App/Theme")->SetInt(2);
    ASSERT_TRUE(libresrc_dark()) << "the appearance reaches the icon loader before windows are told";
    auto bold_dark = IconImage(cmd::get("edit/style/bold")->Icon(16), 16);
    EXPECT_GT(MeanLightness(bold_dark), 0.6) << "a dark glyph becomes light";

    // A coloured icon keeps its colours: only near-black parts at its edge change.
    auto save_dark = IconImage(cmd::get("subtitle/save")->Icon(24), 24);
    ASSERT_EQ(save_light.GetSize(), save_dark.GetSize());
    int changed = 0, opaque = 0;
    for (int i = 0; i < save_light.GetWidth() * save_light.GetHeight(); ++i) {
        if (save_light.GetAlpha()[i] < 128) continue;
        ++opaque;
        if (memcmp(save_light.GetData() + i * 3, save_dark.GetData() + i * 3, 3)) ++changed;
    }
    EXPECT_LT(changed, opaque / 3);

    // A black mark drawn on a light part (the plus on the zoom lens) keeps its contrast.
    bool dark_inside = false;
    MeanLightness(IconImage(cmd::get("video/zoom/in")->Icon(24), 24), &dark_inside);
    EXPECT_TRUE(dark_inside);

    // The rule itself, on a black square with a black dot inside a white one.
    wxImage glyph(8, 8); glyph.SetAlpha(); memset(glyph.GetData(), 0, 8 * 8 * 3); memset(glyph.GetAlpha(), 255, 64);
    EXPECT_TRUE(libresrc_is_glyph(glyph));
    libresrc_adapt_for_dark(glyph, true);
    EXPECT_GT(MeanLightness(glyph), 0.8);
    wxImage marked(8, 8); marked.SetAlpha(); memset(marked.GetData(), 255, 8 * 8 * 3); memset(marked.GetAlpha(), 255, 64);
    memset(marked.GetAlpha(), 0, 8); // a transparent top row
    for (int x = 0; x < 8; ++x) { auto p = marked.GetData() + (8 + x) * 3; p[0] = p[1] = p[2] = 0; } // black outline under it
    auto dot = marked.GetData() + (4 * 8 + 4) * 3; dot[0] = dot[1] = dot[2] = 0;
    EXPECT_FALSE(libresrc_is_glyph(marked));
    libresrc_adapt_for_dark(marked, false);
    EXPECT_GT(marked.GetData()[(8 + 3) * 3], 200) << "the black edge is lightened";
    EXPECT_EQ(0, marked.GetData()[(4 * 8 + 4) * 3]) << "the dot inside is kept";

    // The shape tool icon follows at once.
    OPT_SET("App/Theme")->SetInt(1);
    auto shape = DrawingShapeIcon(DrawingShape::Triangle, 24).GetBitmap(wxSize(48, 48)).ConvertToImage();
    ASSERT_TRUE(shape.HasAlpha());
    EXPECT_EQ(48, shape.GetWidth());
    EXPECT_EQ(0, shape.GetAlpha()[0]) << "transparent around the shape";
    EXPECT_GT(shape.GetAlpha()[24 * 48 + 24], 200);
}

TEST_F(ordinary_edit, icons_are_monochrome_in_the_appearance_colour_unless_classic) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Theme")->SetInt(1); OPT_SET("App/Classic Icons")->SetBool(false); };
    OPT_SET("App/Theme")->SetInt(1);
    OPT_SET("App/Classic Icons")->SetBool(false);
    theme::Init();
    ASSERT_FALSE(libresrc_classic());

    // Every toolbar's commands have one, and so do the sub-tools and shapes.
    for (auto name : {"visual_move_conv_move", "visual_move_conv_pos", "shape_rectangle", "shape_ellipse",
                      "shape_triangle", "shape_polygon", "visual_vector_clip_freehand_smooth"})
        EXPECT_NE(nullptr, libresrc_modern_icon(name)) << name;
    // And every other bitmap: dialogs, the edit box, menus
    for (auto name : {"eyedropper_tool", "button_bold", "button_color_four", "arrow_sort", "kara_split_accept",
                      "save_timecodes_menu", "use_dummy_video_menu", "website_button", "toggle_audio_link"})
        EXPECT_NE(nullptr, libresrc_modern_icon(name)) << name;
    EXPECT_EQ(nullptr, libresrc_modern_icon("wxicon")) << "the application's own icon stays";
    EXPECT_EQ(nullptr, libresrc_modern_icon("visual_rectang")) << "exact names only";

    // One colour: dark grey on light, light grey on dark, at any size.
    auto colours = [](wxImage image) {
        if (!image.HasAlpha()) image.InitAlpha();
        std::set<std::tuple<int, int, int>> found;
        const unsigned char *rgb = image.GetData(), *alpha = image.GetAlpha();
        for (int i = 0, n = image.GetWidth() * image.GetHeight(); i < n; ++i)
            // To the nearest 8: antialiased edges are a unit or two off
            if (alpha[i] > 200) found.emplace((rgb[i * 3] + 4) / 8 * 8, (rgb[i * 3 + 1] + 4) / 8 * 8, (rgb[i * 3 + 2] + 4) / 8 * 8);
        return found;
    };
    for (auto name : {"subtitle/save", "audio/play/selection", "audio/opt/autocommit", "video/play",
                      "video/tool/drag", "video/tool/vclip/line", "help/video", "app/options", "time/snap/scene",
                      "edit/style/bold", "edit/undo", "subtitle/find", "audio/close", "keyframe/save"})
        EXPECT_EQ(1u, colours(IconImage(cmd::get(name)->Icon(16), 16)).size()) << name;
    for (int size : {16, 24, 32}) {
        auto save = IconImage(cmd::get("subtitle/save")->Icon(size), size);
        ASSERT_TRUE(save.HasAlpha());
        EXPECT_EQ(size, save.GetWidth());
        EXPECT_EQ(0, save.GetAlpha()[0]) << "transparent around the drawing";
        auto found = colours(save);
        ASSERT_EQ(1u, found.size()) << size;
        EXPECT_EQ(std::make_tuple(0x28, 0x28, 0x28), *found.begin());
    }
    OPT_SET("App/Theme")->SetInt(2);
    auto dark = colours(IconImage(cmd::get("subtitle/save")->Icon(24), 24));
    ASSERT_EQ(1u, dark.size());
    EXPECT_EQ(std::make_tuple(0xd8, 0xd8, 0xd8), *dark.begin());
    OPT_SET("App/Theme")->SetInt(1);

    // Hinted to the pixel grid: a whole number of pixels for the stroke, so
    // lines are solid pixels rather than two half-covered columns.
    auto partial = [](wxImage const& image) {
        int on = 0, soft = 0;
        for (int i = 0; i < image.GetWidth() * image.GetHeight(); ++i) {
            int a = image.GetAlpha()[i];
            if (a > 30) ++on;
            if (a > 30 && a < 225) ++soft;
        }
        return double(soft) / on;
    };
    for (int size : {16, 20, 24, 32})
        EXPECT_LT(partial(IconImage(cmd::get("subtitle/save")->Icon(size), size)), size == 16 ? 0.25 : 0.2) << size << " (unhinted, about half)";
    auto body = libresrc_modern_icon("save_toolbutton");
    // On Lucide's 24 grid: 1 pixel at 16 is 1.5 units
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 16).find("stroke-width='1.5'"));
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 16).find("V20.25A1.5 1.5 0 0 1 20.25 21.75H5.25")) << "on pixel centres, the corner as round as the lines moved";
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 20).find("stroke-width='2.4'")) << "2 pixels";
    EXPECT_NE(std::string::npos, libresrc_modern_svg(body, 32).find("stroke-width='2.25'")) << "3 pixels";
    // Symmetric about the middle: the eye's pupil between its corners, at
    // 16 px too, where every line is moved half a pixel the same way
    {
        auto eye = libresrc_modern_svg(libresrc_modern_icon("select_visible_button"), 16);
        EXPECT_NE(std::string::npos, eye.find("M2.25 12.75")) << eye;
        EXPECT_NE(std::string::npos, eye.find("23.25 12.75")) << eye;
        EXPECT_NE(std::string::npos, eye.find("cx='12.75' cy='12.75' r='3'")) << eye;
    }
    // Lucide's dots (zero-length lines it draws round) are small squares of pixels at 16
    EXPECT_NE(std::string::npos, libresrc_modern_svg(libresrc_modern_icon("about_menu"), 16).find("<rect")) << "the i's dot";

    // Right to left, the drawing is mirrored.
    auto next = IconImage(cmd::get("time/next")->Icon(32), 32);
    auto next_rtl = IconImage(cmd::get("time/next")->Icon(32, wxLayout_RightToLeft), 32);
    // Compared as shapes: antialiasing differs a little either way round
    auto shape_of = [](wxImage const& image) {
        std::string mask;
        for (int i = 0; i < image.GetWidth() * image.GetHeight(); ++i) mask += image.GetAlpha()[i] > 128 ? '#' : '.';
        return mask;
    };
    EXPECT_EQ(shape_of(next.Mirror()), shape_of(next_rtl));
    EXPECT_NE(shape_of(next), shape_of(next_rtl)) << "not symmetric";

    // The shape tool's icon has the flyout corner.
    auto shape = DrawingShapeIcon(DrawingShape::Ellipse, 32).GetBitmap(wxSize(32, 32)).ConvertToImage();
    EXPECT_GT(shape.GetAlpha()[29 * 32 + 29], 200);

    // Classic: the coloured bitmaps again, and toolbars are rebuilt with them.
    auto bar = toolbar::GetToolbar(window, "main", c.get(), "Default");
    auto first_icon = [&] { return bar->GetToolByPos(0)->GetNormalBitmap().ConvertToImage(); };
    EXPECT_EQ(1u, colours(first_icon()).size());
    OPT_SET("App/Classic Icons")->SetBool(true);
    EXPECT_TRUE(libresrc_classic());
    EXPECT_GT(colours(first_icon()).size(), 1u) << "the bitmap's own colours";
    OPT_SET("App/Classic Icons")->SetBool(false);
    EXPECT_EQ(1u, colours(first_icon()).size());
    bar->Destroy();
}

TEST_F(ordinary_edit, main_toolbar_groups_dialogs_in_dropdowns_and_has_a_classic_layout) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Classic Toolbar")->SetBool(false); };
    OPT_SET("App/Classic Toolbar")->SetBool(false);
    OPT_SET("App/Theme")->SetInt(1);
    theme::Init();
    auto bar = toolbar::GetToolbar(window, "main", c.get(), "Default");
    auto buttons = [&] {
        std::vector<wxToolBarToolBase const*> found;
        for (size_t i = 0; i < bar->GetToolsCount(); ++i)
            if (auto tool = bar->GetToolByPos(i); !tool->IsSeparator()) found.push_back(tool);
        return found;
    };
    auto grouped = buttons();
    EXPECT_EQ(13u, grouped.size()) << "zoom in and out live on the zoom control below the video";
    int dropdowns = 0;
    wxToolBarToolBase const *jump = nullptr;
    size_t menu_entries = 0;
    for (auto tool : grouped)
        if (tool->GetKind() == wxITEM_DROPDOWN) {
            ++dropdowns;
            ASSERT_NE(nullptr, tool->GetDropdownMenu());
            menu_entries += tool->GetDropdownMenu()->GetMenuItemCount();
            if (tool->GetLabel() == cmd::get("video/jump")->StrDisplay(c.get())) jump = tool;
        }
    EXPECT_EQ(3, dropdowns);
    EXPECT_EQ(14u, menu_entries) << "every grouped command, the button's own one first";
    ASSERT_NE(nullptr, jump);
    auto jump_menu = jump->GetDropdownMenu();
    ASSERT_EQ(3u, jump_menu->GetMenuItemCount());
    EXPECT_EQ(cmd::get("video/jump/start")->StrDisplay(c.get()), jump_menu->FindItemByPosition(1)->GetItemLabelText());

    // Opening a dropdown enables its entries as a menu would: no video is open.
    for (auto item : jump_menu->GetMenuItems()) item->Enable(true);
    wxCommandEvent open(wxEVT_TOOL_DROPDOWN, jump->GetId());
    open.SetEventObject(bar);
    bar->GetEventHandler()->ProcessEvent(open);
    EXPECT_FALSE(jump_menu->FindItemByPosition(1)->IsEnabled());

    // The classic layout: every command a button of its own.
    OPT_SET("App/Classic Toolbar")->SetBool(true);
    auto classic = buttons();
    EXPECT_EQ(26u, classic.size());
    EXPECT_TRUE(std::none_of(classic.begin(), classic.end(), [](auto tool) { return tool->GetKind() == wxITEM_DROPDOWN; }));

    // The appearance rebuilds it with icons for the new background.
    OPT_SET("App/Theme")->SetInt(2);
    EXPECT_EQ(26u, buttons().size());
    OPT_SET("App/Theme")->SetInt(1);
    OPT_SET("App/Classic Toolbar")->SetBool(false);
    EXPECT_EQ(13u, buttons().size());
    bar->Destroy();
}

TEST_F(ordinary_edit, preferences_search_highlights_labels_and_shows_a_matching_page) {
    OPT_SET("Tool/Preferences/Page")->SetInt(0);
    Preferences prefs(window);
    ASSERT_EQ(0u, prefs.CurrentPage());

    prefs.Search("undo levels");
    ASSERT_FALSE(prefs.MatchingPages().empty());
    EXPECT_EQ(0u, prefs.MatchingPages().front());
    EXPECT_EQ(0u, prefs.CurrentPage()) << "the page shown has a match: stay";
    auto controls = prefs.MatchingControls(0);
    ASSERT_EQ(1u, controls.size());
    EXPECT_EQ("Undo Levels", controls[0]->GetLabel());
    EXPECT_EQ(wxFONTWEIGHT_BOLD, controls[0]->GetFont().GetWeight());
    EXPECT_FALSE(prefs.SearchStatus().empty());

    // Words in any case and without spaces; the shown page moves to a match.
    prefs.Search("MOUSE WHEEL");
    ASSERT_FALSE(prefs.MatchingPages().empty());
    EXPECT_NE(0u, prefs.CurrentPage());
    EXPECT_EQ(prefs.MatchingPages().front(), prefs.CurrentPage());
    EXPECT_EQ(wxFONTWEIGHT_NORMAL, controls[0]->GetFont().GetWeight()) << "no longer matched";

    // Page names count: the hotkeys page has no labels of its own.
    prefs.Search("hotkeys");
    EXPECT_FALSE(prefs.MatchingPages().empty());

    prefs.Search("qqqzzzxx");
    EXPECT_TRUE(prefs.MatchingPages().empty());
    EXPECT_EQ(_("No matching settings"), prefs.SearchStatus());

    prefs.Search("");
    EXPECT_TRUE(prefs.MatchingPages().empty());
    EXPECT_TRUE(prefs.SearchStatus().empty());
    for (size_t page = 0; page < 20; ++page) EXPECT_TRUE(prefs.MatchingControls(page).empty());

    // The box is drawn as the other fields are; Escape empties it first
    auto box = dynamic_cast<wxTextCtrl*>(wxWindow::FindWindowByName("preferences_search", &prefs));
    ASSERT_NE(nullptr, box);
    EXPECT_NE(nullptr, dynamic_cast<FlatFrame*>(box->GetParent()));
    box->SetValue("undo");
    EXPECT_FALSE(prefs.MatchingPages().empty()) << "typing searches";
    wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode = WXK_ESCAPE; escape.SetEventObject(box);
    box->GetEventHandler()->ProcessEvent(escape);
    EXPECT_TRUE(box->IsEmpty());
    EXPECT_TRUE(prefs.MatchingPages().empty());
}

TEST_F(ordinary_edit, preferences_rows_are_drawn_controls_that_set_their_options) {
    auto undo_levels = OPT_GET("Limits/Undo Levels")->GetInt();
    ASSERT_NE(81, undo_levels);
    auto linked = OPT_GET("App/Auto/Load Linked Files")->GetInt();
    auto autosave = OPT_GET("App/Auto/Save")->GetBool();
    Preferences prefs(window);
    auto book = prefs.Book();
    // The page holding @p option, and its control
    auto page_of = [&](const char *option) -> OptionPage* {
        for (size_t i = 0; i < book->GetPageCount(); ++i)
            if (auto page = dynamic_cast<OptionPage*>(book->GetPage(i)); page && page->ControlOf(option)) return page;
        return nullptr;
    };
    auto control = [&](const char *option) { auto page = page_of(option); return page ? page->ControlOf(option) : nullptr; };
    // What OK would set
    auto pending = [&](const char *name) { auto value = prefs.Pending(name); EXPECT_TRUE(value) << name; return value; };

    // A number: typed, then a turn of the wheel; out of range is not taken
    auto undo = dynamic_cast<FlatFrame*>(control("Limits/Undo Levels"));
    ASSERT_TRUE(undo);
    auto undo_text = dynamic_cast<wxTextCtrl*>(undo->GetContent());
    ASSERT_TRUE(undo_text);
    undo_text->SetValue("80");
    undo->Wheel(1);
    EXPECT_EQ("81", undo_text->GetValue());
    undo_text->SetValue("1");
    if (auto v = pending("Limits/Undo Levels")) EXPECT_EQ(81, v->GetInt());

    // A list
    auto list = dynamic_cast<FlatChoice*>(control("App/Auto/Load Linked Files"));
    ASSERT_TRUE(list);
    list->Choose(linked == 0 ? 1 : 0);
    if (auto v = pending("App/Auto/Load Linked Files")) EXPECT_EQ(linked == 0 ? 1 : 0, v->GetInt());

    // A check box, its label its own, enabling the settings that depend on it
    auto enable = dynamic_cast<FlatCheck*>(control("App/Auto/Save"));
    ASSERT_TRUE(enable);
    EXPECT_EQ("Enable", enable->GetLabel());
    EXPECT_EQ(autosave, enable->GetValue());
    auto interval = control("App/Auto/Save Every Seconds");
    ASSERT_TRUE(interval);
    EXPECT_EQ(autosave, interval->IsThisEnabled());
    enable->Toggle();
    EXPECT_EQ(!autosave, interval->IsThisEnabled());
    if (auto v = pending("App/Auto/Save")) EXPECT_EQ(!autosave, v->GetBool());
    EXPECT_EQ(undo_levels, OPT_GET("Limits/Undo Levels")->GetInt()) << "only once OK is pressed";

    // Changed from the default: marked, and the mark's ↺ brings the default back
    auto general = page_of("Limits/Undo Levels");
    EXPECT_TRUE(general->IsModified("Limits/Undo Levels"));
    general->Reset("Limits/Undo Levels");
    EXPECT_FALSE(general->IsModified("Limits/Undo Levels"));
    auto fallback = pending("Limits/Undo Levels");
    ASSERT_TRUE(fallback);
    EXPECT_EQ(wxString::Format("%d", (int)fallback->GetInt()), undo_text->GetValue());

    // A check box goes back through its event: what depends on it follows
    auto backup = page_of("App/Auto/Save");
    bool modified = backup->IsModified("App/Auto/Save");
    backup->Reset("App/Auto/Save");
    EXPECT_FALSE(backup->IsModified("App/Auto/Save"));
    EXPECT_EQ(enable->GetValue(), interval->IsThisEnabled());
    (void)modified;

    // Restore Page Defaults: every setting of the page shown, and no other
    list->Choose(linked == 0 ? 1 : 0);
    for (size_t i = 0; i < book->GetPageCount(); ++i) if (book->GetPage(i) == general) book->ChangeSelection(i);
    prefs.RestorePageDefaults();
    for (auto const& option : general->Options()) EXPECT_FALSE(general->IsModified(option)) << option;
    EXPECT_TRUE(enable->GetValue() == pending("App/Auto/Save")->GetBool());
}

TEST_F(ordinary_edit, preferences_pages_are_listed_by_use_with_their_matches) {
    Preferences prefs(window);
    auto book = prefs.Book();
    std::vector<wxString> names;
    for (size_t i = 0; i < book->GetPageCount(); ++i) names.push_back(book->GetPageText(i));
    std::vector<wxString> expected{"General", "Default styles", "Appearance", "Colors", "Editing", "Subtitle Grid",
        "Audio", "Video", "Hotkeys", "Backup", "Automation", "Advanced", "Audio", "Video"};
    EXPECT_EQ(expected, names);

    // Searching: the pages with a match show how many
    prefs.Search("font");
    ASSERT_FALSE(prefs.MatchingPages().empty());
    for (size_t i = 0; i < book->GetPageCount(); ++i) {
        bool match = std::find(prefs.MatchingPages().begin(), prefs.MatchingPages().end(), i) != prefs.MatchingPages().end();
        EXPECT_EQ(match, !book->MatchText(i).empty()) << i;
        if (match) EXPECT_EQ(wxString::Format("%zu", prefs.MatchingControls(i).size()), book->MatchText(i));
    }
    prefs.Search("");
    for (size_t i = 0; i < book->GetPageCount(); ++i) EXPECT_TRUE(book->MatchText(i).empty());

    // Choosing a page in the list remembers it
    book->SetSelection(4);
    EXPECT_EQ(4u, prefs.CurrentPage());
    EXPECT_EQ(4, OPT_GET("Tool/Preferences/Page")->GetInt());
}

TEST_F(ordinary_edit, status_bar_shows_the_current_line_and_the_file_state) {
    std::vector<int> ids;
    {
        auto write = c->subtitleAccess->TryWrite();
        auto& file = LiveWriteForTest(*c);
        for (int i = 0; i < 2; ++i) {
            auto line = new AssDialogue;
            line->Start = 1000 * (i + 1); line->End = 1000 * (i + 2);
            file.Events.push_back(*line);
        }
        file.Commit("", AssFile::COMMIT_DIAG_ADDREM);
    }
    for (auto const& line : LiveRead(*c).Events) ids.push_back(line.Id);
    ASSERT_EQ(3u, ids.size());

    c->selectionController->SetSelectionAndActiveIDs({ids[1]}, ids[1]);
    EXPECT_EQ(wxString::FromUTF8("Line 2 of 3 · 0:00:01.00 → 0:00:02.00"), status_info::Line(c.get()));
    c->selectionController->SetSelectionAndActiveIDs({ids[1], ids[2]}, ids[2]);
    EXPECT_EQ(wxString::FromUTF8("Line 3 of 3 (2 selected) · 0:00:02.00 → 0:00:03.00"), status_info::Line(c.get()));
    c->selectionController->SetSelectionAndActiveIDs({}, 0);
    EXPECT_TRUE(status_info::Line(c.get()).empty());

    EXPECT_EQ(c->subsController->IsModified() ? _("Unsaved changes") : _("No unsaved changes"), status_info::File(c.get()));

    // The tabs announce a comparison opening and ending.
    int changes = 0;
    auto connection = tabs->AddChangeListener([&] { ++changes; });
    auto page = new testing_comparison_page(tabs->Book());
    tabs->OpenComparison(page, "other.ass");
    EXPECT_GT(changes, 0);
    EXPECT_EQ("Comparing with other.ass", status_info::File(c.get()));
    changes = 0;
    tabs->ComparisonEnded(false);
    EXPECT_GT(changes, 0);
    EXPECT_NE("Comparing with other.ass", status_info::File(c.get()));
}

TEST_F(ordinary_edit, visual_tools_sit_to_the_right_of_the_video) {
    for (bool detached : {false, true}) {
        auto video_box = new VideoBox(window, detached, c.get());
        VideoDisplay *display = nullptr;
        std::vector<wxToolBar*> bars;
        std::function<void(wxWindow*)> find = [&](wxWindow *w) {
            for (auto child : w->GetChildren()) {
                if (auto d = dynamic_cast<VideoDisplay*>(child)) display = d;
                if (auto bar = dynamic_cast<wxToolBar*>(child); bar && bar->HasFlag(wxTB_VERTICAL)) bars.push_back(bar);
                find(child);
            }
        };
        find(video_box);
        ASSERT_NE(nullptr, display);
        ASSERT_EQ(2u, bars.size()) << "the visual tools and their sub-tools";
        display->SetMinSize(wxSize(320, 240));
        video_box->SetSize(0, 0, 600, 400);
        video_box->Layout();
        auto x_in_box = [&](wxWindow *w) { int x = 0; for (; w && w != video_box; w = w->GetParent()) x += w->GetPosition().x; return x; };
        for (auto bar : bars) {
            EXPECT_GE(x_in_box(bar), x_in_box(display) + display->GetSize().x) << "detached: " << detached;
            // No spare width on one side of the tools only
            if (!bar->IsShown()) continue;
            int left = bar->GetPosition().x, right = bar->GetParent()->GetClientSize().x - left - bar->GetSize().x;
            EXPECT_LE(std::abs(left - right), 1) << left << " / " << right;
        }
        if (!detached) {
            // Narrower than the controls below: in the middle of its block,
            // not leaving the whole gap between it and the tools
            auto area = display->GetParent();
            ASSERT_LT(display->GetSize().x, area->GetSize().x);
            int left = display->GetPosition().x, right = area->GetSize().x - left - display->GetSize().x;
            EXPECT_LE(std::abs(left - right), 1) << left << " / " << right;
        }
        video_box->Destroy();
    }
}

// Fetch a frame with the main dispatch queue running, so libass finishes its
// setup on it and closing the video does not wait for it (see visual_edit).
void PumpFirstFrame(agi::Context *c, wxWindow *window) {
    wxEventLoop loop;
    wxEventLoopActivator activate(&loop);
    auto provider=c->project->VideoProvider();
    std::thread reader([&] {
        try { provider->GetFrame(0,0); } catch (...) {}
        window->CallAfter([&] { loop.Exit(); });
    });
    loop.Run();reader.join();
}
TEST_F(ordinary_edit, video_slider_and_readouts_are_flat_and_hold_still) {
    OPT_SET("Video/Subtitle Sync")->SetBool(false);
    agi::fs::path kf((std::filesystem::current_path() / "slider-keyframes.txt").wstring());
    {std::ofstream out(kf.string());out<<"# keyframe format v1\nfps 0\n0\n50\n150\n";}
    {auto scope=c->subtitleAccess->TryWrite();
     auto& row=LiveWriteForTest(*c).Events.front();row.Start=2000;row.End=4000;
     LiveWriteForTest(*c).Commit("line",AssFile::COMMIT_DIAG_TIME);}
    auto video_box=new VideoBox(window,false,c.get());
    BOOST_SCOPE_EXIT_ALL(&) { video_box->Destroy(); c->project->CloseVideo(); c->project->CloseKeyframes(); };
    VideoSlider *slider=nullptr;std::vector<VideoTimeLabel*> labels;VideoZoomControl *zoom=nullptr;VideoDisplay *display=nullptr;
    std::vector<wxToolBar*> rows;int shown_text_ctrls=0,combos=0;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto ch:w->GetChildren()){
        if(auto s=dynamic_cast<VideoSlider*>(ch))slider=s;
        if(auto l=dynamic_cast<VideoTimeLabel*>(ch))labels.push_back(l);
        if(auto z=dynamic_cast<VideoZoomControl*>(ch))zoom=z;
        if(auto d=dynamic_cast<VideoDisplay*>(ch))display=d;
        if(auto b=dynamic_cast<wxToolBar*>(ch);b&&!b->HasFlag(wxTB_VERTICAL))rows.push_back(b);
        if(dynamic_cast<wxComboBox*>(ch))++combos;
        else if(dynamic_cast<wxTextCtrl*>(ch)&&ch->IsShown())++shown_text_ctrls;
        find(ch);}};
    find(video_box);
    ASSERT_NE(nullptr,slider);ASSERT_EQ(2u,labels.size());ASSERT_NE(nullptr,zoom);ASSERT_NE(nullptr,display);
    EXPECT_EQ(0,shown_text_ctrls) << "the readouts are drawn, not text boxes";
    EXPECT_EQ(0,combos) << "the zoom is a drawn control";
    auto position=labels[0],relative=labels[1];

    // Play and play line, no stop: play pauses while playing.
    ASSERT_EQ(2u,rows.size());
    wxToolBar *play_bar=nullptr,*options_bar=nullptr;
    for(auto bar:rows)(bar->GetToolsCount()==2?play_bar:options_bar)=bar;
    ASSERT_NE(nullptr,play_bar);ASSERT_NE(nullptr,options_bar);
    EXPECT_EQ(cmd::get("video/play")->StrDisplay(c.get()),play_bar->GetToolByPos(0)->GetLabel());
    EXPECT_EQ(cmd::get("video/play/line")->StrDisplay(c.get()),play_bar->GetToolByPos(1)->GetLabel());
    EXPECT_TRUE(cmd::get("video/play")->Type()&cmd::COMMAND_DYNAMIC_ICON);

    // Without video: dashes and no line span.
    EXPECT_EQ(wxString(wxUniChar(0x2014)),position->GetMain());
    EXPECT_EQ(-1,slider->GetLineStart());
    position->StartEditing();
    EXPECT_FALSE(position->IsEditing()) << "nothing to type without a video";

    c->project->LoadVideo("?dummy:25:250:640:480:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    c->project->LoadKeyframes(kf);
    display->SetMinSize(wxSize(640,360));
    video_box->SetSize(0,0,900,600);video_box->Layout();

    // The active line's frames are marked on the slider.
    EXPECT_EQ(c->videoController->FrameAtTime(2000,agi::vfr::START),slider->GetLineStart());
    EXPECT_EQ(c->videoController->FrameAtTime(4000,agi::vfr::END),slider->GetLineEnd());
    EXPECT_LT(slider->GetLineStart(),slider->GetLineEnd());

    // The track runs between equal margins, the last frame at the far end.
    int w=slider->GetClientSize().x;
    EXPECT_EQ(w-slider->XAtFrame(249),slider->XAtFrame(0));
    EXPECT_GT(slider->XAtFrame(0),0);

    // Time, frame number and a keyframe badge instead of a coloured box;
    // the width does not change from frame to frame.
    c->videoController->JumpToFrame(50);
    EXPECT_EQ("0:00:02.000",position->GetMain());
    EXPECT_EQ("50",position->GetSecondary());
    EXPECT_TRUE(position->BadgeShown());
    int width=position->GetSize().x;
    c->videoController->JumpToFrame(51);
    EXPECT_FALSE(position->BadgeShown());
    EXPECT_EQ("51",position->GetSecondary());
    EXPECT_EQ(width,position->GetSize().x);

    // Offsets from the line's start and end, a true minus for negatives.
    c->videoController->JumpToFrame(c->videoController->FrameAtTime(3000,agi::vfr::START));
    EXPECT_EQ("+1000ms",relative->GetMain());
    EXPECT_EQ(wxString(wxUniChar(0x2212))+"1000ms",relative->GetSecondary());

    // Clicking the time turns it into a text box only while typing. The
    // click is handled and the label never takes the focus, or the toolkit
    // would focus it after the click and close the box straight away.
    ASSERT_NE(nullptr,position->EditBox());
    EXPECT_FALSE(position->IsEditing());
    EXPECT_FALSE(position->AcceptsFocus());
    {wxMouseEvent click(wxEVT_LEFT_DOWN);click.SetPosition({20,position->GetSize().y/2});click.SetEventObject(position);
     EXPECT_TRUE(position->GetEventHandler()->ProcessEvent(click)) << "not skipped";
     wxYield();
     EXPECT_TRUE(position->IsEditing()) << "the box opens once the click is over";
     position->CancelEditing();}
    int height=position->GetSize().y;
    position->StartEditing();
    EXPECT_TRUE(position->IsEditing());
    EXPECT_EQ(position->GetMain(),position->EditBox()->GetValue());
    EXPECT_EQ(height,position->GetSize().y) << "the label is as tall as the box already";
    position->EditBox()->ChangeValue("100");position->CommitEditing();
    EXPECT_EQ(100,c->videoController->GetFrameN()) << "digits are a frame number";
    EXPECT_FALSE(position->IsEditing());
    position->StartEditing();position->EditBox()->ChangeValue("0:00:06.000");position->CommitEditing();
    EXPECT_EQ(c->videoController->FrameAtTime(6000,agi::vfr::START),c->videoController->GetFrameN());
    position->StartEditing();position->EditBox()->ChangeValue("4.5");position->CommitEditing();
    EXPECT_EQ(c->videoController->FrameAtTime(4500,agi::vfr::START),c->videoController->GetFrameN()) << "seconds";
    position->StartEditing();position->EditBox()->ChangeValue("99999");position->CommitEditing();
    EXPECT_EQ(249,c->videoController->GetFrameN()) << "past the end: the last frame";
    position->StartEditing();position->EditBox()->ChangeValue("abc");position->CommitEditing();
    EXPECT_TRUE(position->IsEditing()) << "bad input keeps the box open";
    EXPECT_EQ(249,c->videoController->GetFrameN());
    position->CancelEditing();
    EXPECT_FALSE(position->IsEditing());
    EXPECT_EQ(249,c->videoController->GetFrameN());

    // The frame number is edited on its own: clicking it opens a box over
    // it alone, holding the frame number, which takes only digits.
    {int frame_x=position->GetSize().x-position->FromDIP(4);
     EXPECT_EQ(0,position->PartAt(position->FromDIP(12)));
     EXPECT_EQ(1,position->PartAt(frame_x));
     wxMouseEvent click(wxEVT_LEFT_DOWN);click.SetPosition({frame_x,position->GetSize().y/2});click.SetEventObject(position);
     position->GetEventHandler()->ProcessEvent(click);
     wxYield();
     ASSERT_EQ(1,position->EditingPart());
     EXPECT_EQ("249",position->EditBox()->GetValue());
     EXPECT_GT(position->EditBox()->GetPosition().x,position->FromDIP(40)) << "the box covers the frame number only";
     position->EditBox()->ChangeValue("0:00:01.000");position->CommitEditing();
     EXPECT_EQ(1,position->EditingPart()) << "not a frame number";
     position->EditBox()->ChangeValue(" 120 ");position->CommitEditing();
     EXPECT_EQ(120,c->videoController->GetFrameN());
     EXPECT_FALSE(position->IsEditing());}

    // A click anywhere else closes the box, even on something that never
    // takes the keyboard, so the box never loses the focus.
    position->StartEditing();
    ASSERT_EQ(0,position->EditingPart());
    {wxMouseEvent away(wxEVT_LEFT_DOWN);away.SetEventObject(relative);
     relative->GetEventHandler()->ProcessEvent(away);}
    EXPECT_FALSE(position->IsEditing());
    EXPECT_EQ(120,c->videoController->GetFrameN());
    // A click in the box itself keeps it; one on the other part switches.
    position->StartEditing();
    {wxMouseEvent inside(wxEVT_LEFT_DOWN);inside.SetEventObject(position->EditBox());
     position->EditBox()->GetEventHandler()->ProcessEvent(inside);}
    EXPECT_EQ(0,position->EditingPart());
    {wxMouseEvent other(wxEVT_LEFT_DOWN);other.SetPosition({position->GetSize().x-position->FromDIP(4),position->GetSize().y/2});other.SetEventObject(position);
     position->GetEventHandler()->ProcessEvent(other);
     wxYield();}
    EXPECT_EQ(1,position->EditingPart());
    position->CancelEditing();
    EXPECT_EQ(-1,position->EditingPart());

    // The slider is tall enough for the handle at its largest: hovered,
    // with the focus ring.
    EXPECT_GE(slider->GetClientSize().y,slider->FromDIP(26));

    // − and + keep the 12.5% steps; the presets are whole percentages.
    display->SetWindowZoom(.75);
    EXPECT_EQ("75%",zoom->ZoomText());
    zoom->Press(VideoZoomControl::Part::In);
    EXPECT_DOUBLE_EQ(.875,display->GetWindowZoom());
    EXPECT_EQ("87.5%",zoom->ZoomText());
    zoom->Press(VideoZoomControl::Part::Out);zoom->Press(VideoZoomControl::Part::Out);
    EXPECT_DOUBLE_EQ(.625,display->GetWindowZoom());
    EXPECT_DOUBLE_EQ(.25,VideoZoomControl::StepIn(.125));
    EXPECT_DOUBLE_EQ(.125,VideoZoomControl::StepOut(.125));
    for(int p:VideoZoomControl::Presets())EXPECT_EQ(0,p%25);
    cmd::call("video/zoom/in",c.get());
    EXPECT_DOUBLE_EQ(.75,display->GetWindowZoom()) << "the zoom commands step the same way";
    // The wheel steps the same way too, up zooming in
    wxMouseEvent wheel(wxEVT_MOUSEWHEEL); wheel.m_wheelRotation=120; wheel.m_wheelDelta=120; wheel.SetEventObject(zoom);
    zoom->GetEventHandler()->ProcessEvent(wheel);
    EXPECT_DOUBLE_EQ(.875,display->GetWindowZoom());
    zoom->Wheel(-2); EXPECT_DOUBLE_EQ(.625,display->GetWindowZoom());
    zoom->Wheel(1);

    // Readouts at the left, play buttons centred, the zoom at the right.
    wxWindow *block=zoom->GetParent();
    int gap=theme::BlockGap(video_box);
    auto right=[](wxWindow*w){return w->GetPosition().x+w->GetSize().x;};
    for(int video_w:{960,480}){
        display->SetMinSize(wxSize(video_w,video_w*9/16));
        video_box->SetSize(0,0,1400,800);video_box->Layout();
        int block_w=block->GetClientSize().x;
        EXPECT_EQ(gap,position->GetPosition().x);
        EXPECT_EQ(block_w-gap,right(zoom));
        EXPECT_LT(right(relative),play_bar->GetPosition().x) << video_w;
        EXPECT_LT(right(play_bar),options_bar->GetPosition().x) << video_w;
        int centre=play_bar->GetPosition().x+play_bar->GetSize().x/2;
        if(video_w==960) EXPECT_NEAR(block_w/2,centre,1) << "centred on the video";
        else EXPECT_NEAR((right(relative)+options_bar->GetPosition().x)/2,centre,gap) << "narrow: centred between the groups";
    }

    // Offsets longer than the sample widen the readout, and the whole row
    // makes room, so nothing is cut off or covered.
    int relative_w=relative->GetSize().x;
    {auto scope=c->subtitleAccess->TryWrite();
     auto& row=LiveWriteForTest(*c).Events.front();row.Start=3000000;row.End=3600000;
     LiveWriteForTest(*c).Commit("line",AssFile::COMMIT_DIAG_TIME);}
    EXPECT_EQ(10u,relative->GetSecondary().length()) << "seven digits, more than the sample's five";
    EXPECT_GT(relative->GetSize().x,relative_w);
    EXPECT_GE(relative->GetSize().x,relative->GetMinSize().x);
    EXPECT_LT(right(relative),play_bar->GetPosition().x);
}

TEST_F(ordinary_edit, style_look_applies_the_tags_before_a_point_of_the_line) {
    AssStyle base; base.name="Base"; base.font="Arial"; base.fontsize=40; base.outline_w=2; base.shadow_w=3; base.alignment=2;
    base.primary=agi::Color(255,255,255); base.outline=agi::Color(0,0,0);
    AssStyle other; other.name="Other"; other.font="Other Font"; other.fontsize=20; other.alignment=8;
    StyleLookup find=[&](std::string const& name)->AssStyle const* { return name=="Base"?&base:name=="Other"?&other:nullptr; };
    AssDialogue line; line.Style="Base";
    line.Text="{\\an7\\fs50\\b1}ab{\\i1\\1c&H0000FF&\\1a&H80&}cd{\\r}ef{\\rOther\\an3}gh";
    auto at=[&](int plain) { return LookAt(line,plain,find); };
    // Leading tags count from the start; the first alignment counts everywhere
    EXPECT_EQ(50,at(0).size); EXPECT_EQ(700,at(0).weight); EXPECT_EQ(7,at(0).alignment); EXPECT_FALSE(at(0).italic);
    EXPECT_FALSE(at(1).italic);
    // The tags in front of a character apply to it
    EXPECT_TRUE(at(2).italic); EXPECT_EQ(agi::Color(255,0,0,0x80),at(2).primary); EXPECT_EQ(50,at(2).size);
    // \r goes back to the line's style, \rName to that one, alignment stays
    EXPECT_EQ(40,at(4).size); EXPECT_FALSE(at(4).italic); EXPECT_EQ(400,at(4).weight); EXPECT_EQ(agi::Color(255,255,255),at(4).primary);
    EXPECT_EQ("Other Font",at(6).font); EXPECT_EQ(20,at(6).size); EXPECT_EQ(7,at(6).alignment); EXPECT_EQ("Base",at(6).style);
    // Shadow, border, alpha for all colours, weights, a tag without value
    line.Text="{\\xshad4\\yshad-2\\xbord1\\ybord5\\alpha&HFF&\\b600\\fsp3\\blur1.5\\fs}x";
    auto look=at(0);
    EXPECT_EQ(4,look.shadow_x); EXPECT_EQ(-2,look.shadow_y); EXPECT_EQ(5,look.border); EXPECT_EQ(600,look.weight); EXPECT_FALSE(look.Bold());
    EXPECT_EQ(255,look.primary.a); EXPECT_EQ(255,look.outline.a); EXPECT_EQ(255,look.shadow.a);
    EXPECT_EQ(3,look.spacing); EXPECT_EQ(1.5,look.blur); EXPECT_EQ(40,look.size);
    // Missing styles look like a new style; comments are not text
    line.Style="Missing"; line.Text="{note}ab{\\u1}c";
    EXPECT_EQ(AssStyle().font,at(0).font); EXPECT_FALSE(at(1).underline); EXPECT_TRUE(at(2).underline);
    // Tags inside \t are where an animation ends, not how the text looks
    line.Style="Base"; line.Text="{\\t(\\fs99\\b1)}x";
    EXPECT_EQ(40,at(0).size); EXPECT_EQ(400,at(0).weight);
    EXPECT_EQ(0,PlainPosition("{\\b1}abc",5)); EXPECT_EQ(2,PlainPosition("{\\b1}abc",7)); EXPECT_EQ(3,PlainPosition("a{b",3));
}

TEST_F(ordinary_edit, ctrl_z_from_anywhere_in_the_style_panel_undoes) {
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     file.Events.front().Text="Hello world";
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto line=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    auto id=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({id},id);
    c->textSelectionController->SetSelection(0,0);
    // As the main window has its hotkeys
    auto frame_keys=[&](wxKeyEvent &e){ hotkey::check("Main Frame",c.get(),e); };
    window->Bind(wxEVT_CHAR_HOOK,frame_keys);
    BOOST_SCOPE_EXIT_ALL(&){ window->Unbind(wxEVT_CHAR_HOOK,frame_keys); };
    auto panel=new StylePanel(window,c.get());
    window->SetSize(500,900); window->Show(); window->Layout(); panel->SetSize(window->GetClientSize());
    BOOST_SCOPE_EXIT_ALL(&){ window->Hide(); };
    panel->UpdateNow(); wxYield();
    auto ctrl_z=[&](wxWindow *at){
        wxKeyEvent e(wxEVT_CHAR_HOOK); e.m_keyCode='Z'; e.SetControlDown(true); e.SetEventObject(at);
        at->GetEventHandler()->ProcessEvent(e);
    };
    // From each part Tab stops at, on every page
    for(int page=0;page<3;++page){
        panel->ShowPage(static_cast<StylePanel::Page>(page)); panel->UpdateNow(); wxYield();
        auto parts=panel->KeyboardParts();
        ASSERT_FALSE(parts.empty());
        for(auto part:parts){
            panel->EnterNumber("size",52,true);
            ASSERT_EQ("{\\fs52}Hello world",line());
            ctrl_z(part);
            EXPECT_EQ("Hello world",line()) << "page " << page << ": " << typeid(*part).name();
            panel->UpdateNow();
        }
        panel->ShowPage(StylePanel::Page::Style);
    }
    // From the box a number is typed in
    panel->UpdateNow();
    panel->EnterNumber("size",52,true);
    ctrl_z(panel);
    EXPECT_EQ("Hello world",line()) << "from the panel itself";
}

TEST_F(ordinary_edit, style_panel_changes_the_text_through_its_controls) {
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->fontsize=40;style->alignment=2;file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="{\\b1}Hello world";
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto line=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    auto id=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({id},id);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Buttons
    panel->UpdateNow(); panel->Press("bold");
    EXPECT_EQ("Hello world",line()) << "not bold is the style's own";
    panel->UpdateNow(); panel->Press("italic");
    EXPECT_EQ("{\\i1}Hello world",line());
    panel->UpdateNow(); panel->Press("align_top");
    EXPECT_EQ("{\\i1\\an8}Hello world",line());
    panel->UpdateNow(); panel->Press("align_right");
    EXPECT_EQ("{\\i1\\an9}Hello world",line());
    EXPECT_EQ("on",shown()["align_right"]);

    // Dragging a number: one undo step
    panel->EnterNumber("size",41,false); panel->EnterNumber("size",45,false); panel->EnterNumber("size",52,true);
    EXPECT_EQ("{\\i1\\an9\\fs52}Hello world",line());
    EXPECT_EQ("52",shown()["size"]);
    c->subsController->Undo();
    EXPECT_EQ("{\\i1\\an9}Hello world",line());
    panel->EnterNumber("size",5000,true);
    EXPECT_EQ("{\\i1\\an9\\fs1000}Hello world",line()) << "kept in range";
    c->subsController->Undo();

    // Typing a number
    panel->UpdateNow();
    panel->StartEditing("spacing");
    auto box=panel->EditBox("spacing");
    ASSERT_NE(nullptr,box); EXPECT_TRUE(box->IsShown()); EXPECT_EQ("0",box->GetValue());
    box->ChangeValue("1,5");
    wxCommandEvent enter(wxEVT_TEXT_ENTER,box->GetId()); enter.SetEventObject(box); box->GetEventHandler()->ProcessEvent(enter);
    EXPECT_FALSE(box->IsShown());
    EXPECT_EQ("{\\i1\\an9\\fsp1.5}Hello world",line());
    panel->StartEditing("spacing"); box->ChangeValue("abc");
    box->GetEventHandler()->ProcessEvent(enter);
    EXPECT_TRUE(box->IsShown()) << "not a number: stays open";
    wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode=WXK_ESCAPE; escape.SetEventObject(box); box->GetEventHandler()->ProcessEvent(escape);
    EXPECT_FALSE(box->IsShown());
    EXPECT_EQ("{\\i1\\an9\\fsp1.5}Hello world",line());

    // Font and weight
    panel->ChooseFont("Test Font");
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello world",line());
    panel->ChooseWeight(700,false);
    EXPECT_EQ("{\\an9\\fsp1.5\\fnTest Font\\b1}Hello world",line());
    c->subsController->Undo();
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello world",line()) << "weight and slant are one step";

    // Selected text
    std::string text=line(); int w=static_cast<int>(text.find("world"));
    c->textSelectionController->SetSelection(w,w+5);
    EXPECT_EQ("The selected text",shown()["scope"]);
    panel->Press("underline");
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello {\\u1}world",line());
    EXPECT_EQ("on",shown()["underline"]) << "the selection follows the text";
    panel->EnterNumber("size",60,false); panel->EnterNumber("size",70,true);
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font}Hello {\\u1\\fs70}world",line());
    text=line(); int h=static_cast<int>(text.find("Hello"));
    c->textSelectionController->SetSelection(h,h+5);
    panel->UpdateNow(); panel->Press("strikeout");
    EXPECT_EQ("{\\i1\\an9\\fsp1.5\\fnTest Font\\s1}Hello{\\s0} {\\u1\\fs70}world",line());
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_changes_fill_outline_shadow_and_background) {
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->outline_w=3;style->shadow_w=0;
     style->primary=agi::Color(255,255,255);style->outline=agi::Color(0,0,0);style->shadow=agi::Color(0,0,0,0x80);file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="Hello";
     auto other=new AssDialogue();other->Style="Look";other->Text="Other";file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto line=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    auto id=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({id},id);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Check boxes: hide, and show again with the style's value
    panel->Check("fill",false); EXPECT_EQ("{\\1a&HFF&}Hello",line()); EXPECT_EQ("off #FFFFFF",shown()["fill"]);
    panel->Check("fill",true); EXPECT_EQ("Hello",line());
    panel->Check("outline",false); EXPECT_EQ("{\\bord0}Hello",line()); EXPECT_EQ("off #000000",shown()["outline"]);
    panel->Check("outline",true); EXPECT_EQ("Hello",line()) << "the style's width comes back";
    panel->Check("shadow",true); EXPECT_EQ("{\\shad2}Hello",line()); EXPECT_EQ("on #000000",shown()["shadow"]);
    EXPECT_EQ("135",shown()["shadow_angle"]); EXPECT_EQ("2.83",shown()["shadow_distance"]);
    // Distance keeps the angle, angle keeps the distance
    panel->EnterNumber("shadow_distance",4,true);
    EXPECT_EQ("{\\shad2.83}Hello",line()) << "equal offsets are one tag";
    panel->UpdateNow(); panel->EnterNumber("shadow_angle",90,true);
    EXPECT_EQ("{\\xshad4\\yshad0}Hello",line());
    panel->UpdateNow(); panel->EnterNumber("shadow_angle",180,true);
    EXPECT_EQ("{\\xshad0\\yshad4}Hello",line());
    panel->Check("shadow",false); EXPECT_EQ("Hello",line()) << "no shadow is the style's";

    // Numbers at the right of the colours
    panel->EnterNumber("fill_opacity",50,true); EXPECT_EQ("{\\1a&H80&}Hello",line());
    EXPECT_EQ("50",shown()["fill_opacity"]);
    panel->EnterNumber("fill_opacity",100,true); EXPECT_EQ("Hello",line());
    panel->EnterNumber("outline_width",1.5,false); panel->EnterNumber("outline_width",4,true);
    EXPECT_EQ("{\\bord4}Hello",line());
    c->subsController->Undo(); EXPECT_EQ("Hello",line()) << "one drag, one step";
    panel->EnterNumber("shadow_opacity",0,true); EXPECT_EQ("{\\4a&HFF&}Hello",line());
    c->subsController->Undo();
    panel->EnterNumber("blur",0.8,true); EXPECT_EQ("{\\blur0.8}Hello",line());
    c->subsController->Undo();

    // Colours: only what changed is written
    panel->UpdateNow();
    panel->ChooseColour("fill",agi::Color(255,0,0));
    EXPECT_EQ("{\\c&H0000FF&}Hello",line());
    panel->UpdateNow();
    panel->ChooseColour("fill",agi::Color(255,0,0,0x40));
    EXPECT_EQ("{\\c&H0000FF&\\1a&H40&}Hello",line());
    panel->UpdateNow();
    panel->ChooseColour("outline",agi::Color(0,0,255));
    EXPECT_EQ("{\\c&H0000FF&\\1a&H40&\\3c&HFF0000&}Hello",line());
    panel->UpdateNow();
    panel->ChooseColour("shadow",agi::Color(0,255,0,0x80));
    EXPECT_EQ("{\\c&H0000FF&\\1a&H40&\\3c&HFF0000&\\4c&H00FF00&}Hello",line()) << "the same alpha needs no tag";
    EXPECT_EQ("off #00FF00",shown()["shadow"]) << "the colour of a shadow turned off";

    // Only a style has a background (an opaque box, and then no outline):
    // the line gets a copy of its style with one; the style and its other
    // lines stay as they are.
    auto style_of=[&](int i) { return std::string(std::next(LiveRead(*c).Events.begin(),i)->Style.get()); };
    size_t styles=LiveRead(*c).Styles.size();
    panel->Check("background",true);
    EXPECT_EQ("Look Box",style_of(0));
    EXPECT_EQ("Look",style_of(1)) << "the other line keeps its style";
    EXPECT_EQ(1,LiveRead(*c).GetStyle("Look")->borderstyle) << "the style stays";
    ASSERT_NE(nullptr,LiveRead(*c).GetStyle("Look Box"));
    EXPECT_EQ(3,LiveRead(*c).GetStyle("Look Box")->borderstyle);
    {AssStyle a(*LiveRead(*c).GetStyle("Look")),b(*LiveRead(*c).GetStyle("Look Box"));
     a.name=b.name="";b.borderstyle=1;a.UpdateData();b.UpdateData();
     EXPECT_EQ(a.GetEntryData(),b.GetEntryData()) << "a copy, but for the box";}
    EXPECT_EQ(styles+1,LiveRead(*c).Styles.size());
    EXPECT_EQ("on #0000FF",shown()["background"]);
    std::string before=line();
    panel->Check("outline",true);
    EXPECT_EQ(before,line()) << "a box has no outline to show";
    // Off: back to the style it came from; on again: the copy made before
    panel->Check("background",false);
    EXPECT_EQ("Look",style_of(0));
    panel->Check("background",true);
    EXPECT_EQ("Look Box",style_of(0));
    EXPECT_EQ(styles+1,LiveRead(*c).Styles.size()) << "no second copy";
    // A line whose style has a box of its own gets a copy without one
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto boxed=new AssStyle();boxed->name="Boxed";boxed->fontsize=33;boxed->borderstyle=3;file.Styles.push_back(*boxed);
     file.Events.front().Style="Boxed";file.Commit("boxed",AssFile::COMMIT_STYLES|AssFile::COMMIT_DIAG_META);}
    panel->Check("background",false);
    EXPECT_EQ("Boxed No Box",style_of(0));
    EXPECT_EQ(1,LiveRead(*c).GetStyle("Boxed No Box")->borderstyle);
    EXPECT_EQ(3,LiveRead(*c).GetStyle("Boxed")->borderstyle);
    c->subsController->Undo();
    EXPECT_EQ("Boxed",style_of(0));
    EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("Boxed No Box")) << "one undo step takes the copy away too";
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_sets_updates_resets_and_makes_styles) {
    int second;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->fontsize=40;style->alignment=2;style->bold=false;file.Styles.push_back(*style);
     style=new AssStyle();style->name="Other";style->fontsize=20;file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="{\\fs60\\b1\\an8\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small";
     auto other=new AssDialogue();other->Style="Look";other->Text="{\\fs60}two";second=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto text=[&](int n) { return std::string(std::next(LiveRead(*c).Events.begin(),n)->Text.get()); };
    auto style=[&](std::string const& name) { return LiveRead(*c).GetStyle(name); };
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };
    EXPECT_EQ("enabled",shown()["style_update"]);

    // Choosing a style
    panel->ChooseStyle("Other");
    EXPECT_EQ("Other",std::string(LiveRead(*c).Events.front().Style.get()));
    EXPECT_EQ("Other",shown()["style"]);
    c->subsController->Undo();

    // Update the style from the line: what a style can hold moves into it
    panel->Press("style_update");
    ASSERT_NE(nullptr,style("Look"));
    EXPECT_EQ(60,style("Look")->fontsize); EXPECT_TRUE(style("Look")->bold); EXPECT_EQ(8,style("Look")->alignment);
    EXPECT_EQ("{\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small",text(0)) << "blur stays: a style has none; tags inside the line stay";
    EXPECT_EQ("{\\fs60}two",text(1)) << "lines not selected keep their tags";
    EXPECT_EQ("60",shown()["size"]);
    c->subsController->Undo();
    EXPECT_EQ(40,style("Look")->fontsize);
    EXPECT_EQ("{\\fs60\\b1\\an8\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small",text(0));

    // Reset to the style: the panel's tags go, others stay
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    panel->Press("style_reset");
    EXPECT_EQ("{\\pos(10,20)}Big small",text(0));
    EXPECT_EQ("two",text(1));
    c->subsController->Undo();

    // A new style from the current line, for the selected lines
    EXPECT_TRUE(panel->NewStyle("Big"));
    ASSERT_NE(nullptr,style("Big"));
    EXPECT_EQ(60,style("Big")->fontsize); EXPECT_TRUE(style("Big")->bold); EXPECT_EQ(8,style("Big")->alignment);
    EXPECT_EQ(40,style("Look")->fontsize) << "the old style stays";
    EXPECT_EQ("Big",std::string(LiveRead(*c).Events.front().Style.get()));
    EXPECT_EQ("Big",std::string(LiveRead(*c).Events.back().Style.get()));
    EXPECT_EQ("{\\blur2\\pos(10,20)}Big {\\fs30\\alpha&H40&}small",text(0));
    EXPECT_EQ("two",text(1)) << "its \\fs60 is now the style's";
    EXPECT_FALSE(panel->NewStyle("big")) << "names are unique without regard to case";
    c->subsController->Undo();
    EXPECT_EQ(nullptr,style("Big"));
    EXPECT_EQ("Look",std::string(LiveRead(*c).Events.front().Style.get()));

    // Nothing to do: no undo step
    c->selectionController->SetSelectionAndActiveIDs({},0);
    EXPECT_EQ("disabled",shown()["style_update"]);
    EXPECT_EQ(-1,style_edit::ResetToStyle(c.get()));
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_moves_scales_turns_and_fades_lines) {
    int moving;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     file.SetScriptInfo("PlayResX","640");file.SetScriptInfo("PlayResY","480");
     auto style=new AssStyle();style->name="Look";style->alignment=2;style->Margin={{10,20,30}};
     style->primary.a=0;style->outline.a=0;style->shadow.a=0;file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="Hello";
     auto other=new AssDialogue();other->Style="Look";other->Text="{\\move(1,2,3,4)}moving";moving=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto text=[&](int n) { return std::string(std::next(LiveRead(*c).Events.begin(),n)->Text.get()); };
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };
    EXPECT_TRUE(panel->IsSectionShown("transform")) << "open at first";

    // Without \pos: where alignment and margins put it
    auto s=shown();
    EXPECT_EQ("315",s["position_x"]); EXPECT_EQ("450",s["position_y"]);
    EXPECT_EQ("100",s["scale_x"]); EXPECT_EQ("0",s["rotation"]); EXPECT_EQ("100",s["opacity"]);
    panel->EnterNumber("position_x",100,true);
    EXPECT_EQ("{\\pos(100,450)}Hello",text(0));
    panel->UpdateNow(); panel->EnterNumber("position_y",200,true);
    EXPECT_EQ("{\\pos(100,200)}Hello",text(0));
    EXPECT_EQ("200",shown()["position_y"]);

    panel->EnterNumber("scale_x",150,true); panel->UpdateNow(); panel->EnterNumber("rotation",-10,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10}Hello",text(0));
    panel->UpdateNow(); panel->EnterNumber("opacity",50,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10\\alpha&H80&}Hello",text(0));
    EXPECT_EQ("50",shown()["opacity"]); EXPECT_EQ("50",shown()["fill_opacity"]);
    panel->EnterNumber("fill_opacity",100,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10\\alpha&H80&\\1a&H00&}Hello",text(0)) << "\\alpha would still apply without it";
    EXPECT_EQ(wxS("—"),shown()["opacity"]) << "fill, outline and shadow differ";
    panel->EnterNumber("opacity",100,true);
    EXPECT_EQ("{\\pos(100,200)\\fscx150\\frz-10}Hello",text(0)) << "the style's opacity needs no tag";

    // A moving line keeps its \move
    c->selectionController->SetSelectionAndActiveIDs({first,moving},first);
    panel->UpdateNow(); panel->EnterNumber("position_x",5,true);
    EXPECT_EQ("{\\fscx150\\frz-10\\pos(5,200)}Hello",text(0));
    EXPECT_EQ("{\\move(1,2,3,4)}moving",text(1));
    // Updating the style takes scale and rotation, not the position
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    panel->Press("style_update");
    EXPECT_EQ(150,LiveRead(*c).GetStyle("Look")->scalex); EXPECT_EQ(-10,LiveRead(*c).GetStyle("Look")->angle);
    EXPECT_EQ("{\\pos(5,200)}Hello",text(0));
    panel->Destroy();
}

TEST_F(ordinary_edit, edit_box_has_line_fields_below_the_text_and_a_time_unit_switch) {
    BOOST_SCOPE_EXIT_ALL(&) { OPT_SET("Subtitle/Edit Box/Fields Below Text")->SetBool(true); c->project->CloseTimecodes(); c->subsGrid=nullptr; };
    int bitmap_buttons=0; bool margins=false, edit_button=false; TimeUnitToggle *unit=nullptr; TimeEdit *end=nullptr;
    wxWindow *comment=nullptr,*actor=nullptr,*effect=nullptr,*original=nullptr; FlatChoice *style=nullptr; FlatFrame *layer=nullptr;
    for (auto child:descendants(box)) {
        if (dynamic_cast<wxBitmapButton*>(child)) ++bitmap_buttons;
        if (child->GetToolTipText().Contains("Margin")) margins=true;
        if (auto b=dynamic_cast<wxButton*>(child); b && b->GetLabel()=="Edit") edit_button=true;
        if (auto t=dynamic_cast<TimeUnitToggle*>(child)) unit=t;
        if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="End time") end=t;
        if (auto s=dynamic_cast<FlatChoice*>(child)) style=s;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetLabel()=="Layer") layer=f;
        if (auto b=dynamic_cast<FlatCheck*>(child)) (b->GetLabel()=="Show Original" ? original : comment)=b;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetToolTipText().StartsWith("Actor")) actor=f;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetToolTipText().StartsWith("Effect")) effect=f;
    }
    EXPECT_EQ(0,bitmap_buttons) << "text and colours are in the style panel, Enter goes to the next line";
    EXPECT_FALSE(margins) << "the margins are in the style panel";
    EXPECT_FALSE(edit_button) << "the style panel opens the style editor";
    ASSERT_NE(nullptr,unit); ASSERT_NE(nullptr,end); ASSERT_NE(nullptr,style); ASSERT_NE(nullptr,layer);
    ASSERT_NE(nullptr,comment); ASSERT_NE(nullptr,actor); ASSERT_NE(nullptr,effect); ASSERT_NE(nullptr,original);
    auto end_frame=end->GetParent();
    wxWindow *start_frame=nullptr;
    for (auto child:descendants(box)) if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="Start time") start_frame=t->GetParent();
    ASSERT_NE(nullptr,start_frame);

    // Where things are: what the line is in the first row, the original at
    // its end; when and on which layer in the second, the layer at its end
    box->SetSize(0,0,900,300); box->Layout();
    auto at=[&](wxWindow *w) { wxPoint p; for(;w&&w!=box;w=w->GetParent()) p+=w->GetPosition(); return p; };
    auto centre_y=[&](wxWindow *w) { return at(w).y+w->GetSize().y/2; };
    for (wxWindow *w:{static_cast<wxWindow*>(style),actor,effect,original}) EXPECT_NEAR(centre_y(comment),centre_y(w),2);
    EXPECT_LT(at(comment).x,at(style).x); EXPECT_LT(at(style).x,at(actor).x); EXPECT_LT(at(actor).x,at(effect).x); EXPECT_LT(at(effect).x,at(original).x);
    for (wxWindow *w:{static_cast<wxWindow*>(unit),static_cast<wxWindow*>(layer)}) EXPECT_NEAR(centre_y(end_frame),centre_y(w),2);
    EXPECT_LT(at(layer).x,at(start_frame).x) << "the layer comes first"; EXPECT_LT(at(end_frame).x,at(unit).x);
    EXPECT_LT(centre_y(comment),centre_y(end_frame)) << "the times are next to the grid";
    EXPECT_LE(at(effect).x+effect->GetSize().x,at(original).x) << "the effect is narrower than the actor";
    EXPECT_LT(effect->GetSize().x,actor->GetSize().x);

    // The times are labelled when there is room, and only then
    auto end_label=static_cast<FlatFrame*>(end_frame);
    EXPECT_TRUE(end_label->IsLabelShown());
    box->SetSize(0,0,480,300); box->Layout();
    EXPECT_FALSE(end_label->IsLabelShown()) << "the arrow and the tooltips still say which is which";
    EXPECT_LE(at(unit).x+unit->GetSize().x,box->GetClientSize().x) << "nothing is cut off";
    box->SetSize(0,0,900,300); box->Layout();
    EXPECT_TRUE(end_label->IsLabelShown());

    // The fields sit below the text, next to the grid; the option puts them back above
    EXPECT_GT(at(end).y,at(editor).y);
    OPT_SET("Subtitle/Edit Box/Fields Below Text")->SetBool(false);
    EXPECT_LT(at(end).y,at(editor).y);

    // Frames need timecodes
    c->subsGrid=new BaseGrid(window,c.get());
    EXPECT_FALSE(unit->FramesAllowed());
    unit->Choose(1); EXPECT_FALSE(unit->ByFrame());
    {std::ofstream timecodes("timecodes.txt"); timecodes << "# timecode format v1\nAssume 25\n";}
    c->project->LoadTimecodes("timecodes.txt");
    ASSERT_TRUE(unit->FramesAllowed());
    wxString time=end->GetValue();
    unit->Choose(1); EXPECT_TRUE(unit->ByFrame());
    EXPECT_EQ("124",end->GetValue()) << "the last frame shown before 5 s at 25 fps";
    unit->Choose(0); EXPECT_EQ(time,end->GetValue());
    unit->Choose(1); c->project->CloseTimecodes();
    EXPECT_FALSE(unit->FramesAllowed()); EXPECT_FALSE(unit->ByFrame()); EXPECT_EQ(time,end->GetValue());

    // Enter on the last line makes a new one after it and goes there
    ASSERT_EQ(1u,LiveRead(*c).Events.size());
    editor->SetFocus();
    wxKeyEvent enter(wxEVT_CHAR_HOOK); enter.m_keyCode=WXK_RETURN; enter.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(enter);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());
    EXPECT_EQ(std::next(LiveRead(*c).Events.begin())->Id,c->selectionController->ActiveID());
}

TEST_F(ordinary_edit, edit_box_style_layer_and_character_count) {
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("Subtitle/Character Limit")->SetInt(40); };
    int second;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     for (auto name:{"Alpha","Beta"}) {auto style=new AssStyle();style->name=name;file.Styles.push_back(*style);}
     auto other=new AssDialogue();other->Layer=4;second=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    FlatChoice *style=nullptr; FlatFrame *layer_frame=nullptr; FlatBadge *count=nullptr; FlatCheck *original=nullptr; wxStaticText *caption=nullptr;
    for (auto child:descendants(box)) {
        if (auto s=dynamic_cast<FlatChoice*>(child)) style=s;
        if (auto f=dynamic_cast<FlatFrame*>(child); f && f->GetLabel()=="Layer") layer_frame=f;
        if (auto b=dynamic_cast<FlatBadge*>(child)) count=b;
        if (auto b=dynamic_cast<FlatCheck*>(child); b && b->GetLabel()=="Show Original") original=b;
        if (auto t=dynamic_cast<wxStaticText*>(child); t && t->GetLabel().StartsWith("Original")) caption=t;
    }
    ASSERT_NE(nullptr,style); ASSERT_NE(nullptr,layer_frame); ASSERT_NE(nullptr,count); ASSERT_NE(nullptr,original); ASSERT_NE(nullptr,caption);
    auto layer=static_cast<wxTextCtrl*>(layer_frame->GetContent());
    auto row=[&](int n) { return &*std::next(LiveRead(*c).Events.begin(),n); };

    // The style list: the file's styles, the wheel goes down them, turns close together are one undo step
    EXPECT_EQ((std::vector<wxString>{"Default","Alpha","Beta"}),style->GetItems());
    EXPECT_EQ("Default",style->GetStringSelection());
    EXPECT_EQ("Default",style->GetToolTipText()) << "hovering shows the style's name";
    wxMouseEvent wheel(wxEVT_MOUSEWHEEL); wheel.m_wheelRotation=-120; wheel.m_wheelDelta=120; wheel.SetEventObject(style);
    style->GetEventHandler()->ProcessEvent(wheel);
    EXPECT_EQ("Alpha",row(0)->Style.get());
    style->Step(1); EXPECT_EQ("Beta",row(0)->Style.get()); EXPECT_EQ("Beta",style->GetStringSelection()); EXPECT_EQ("Beta",style->GetToolTipText());
    style->Step(1); EXPECT_EQ("Beta",row(0)->Style.get()) << "stays at the end";
    c->subsController->Undo(); EXPECT_EQ("Default",row(0)->Style.get()) << "one undo step";
    wxKeyEvent up(wxEVT_KEY_DOWN); up.m_keyCode=WXK_DOWN; up.SetEventObject(style); style->GetEventHandler()->ProcessEvent(up);
    EXPECT_EQ("Alpha",row(0)->Style.get());

    // The layer: typed, or moved from each line's own by a drag of the label, the wheel or the keys
    EXPECT_EQ("0",layer->GetValue());
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    EXPECT_EQ(wxS("—"),layer->GetValue()) << "the lines are on different layers";
    layer_frame->Scrub(1,false); layer_frame->Scrub(2,true);
    EXPECT_EQ(2,row(0)->Layer); EXPECT_EQ(6,row(1)->Layer) << "each from its own";
    c->subsController->Undo(); EXPECT_EQ(0,row(0)->Layer); EXPECT_EQ(4,row(1)->Layer) << "a drag is one undo step";
    layer_frame->Scrub(-3,true); EXPECT_EQ(0,row(0)->Layer); EXPECT_EQ(1,row(1)->Layer) << "not below 0";
    layer_frame->Wheel(1); EXPECT_EQ(1,row(0)->Layer); EXPECT_EQ(2,row(1)->Layer);
    wxKeyEvent key(wxEVT_CHAR_HOOK); key.m_keyCode=WXK_DOWN; key.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(key);
    EXPECT_EQ(0,row(0)->Layer); EXPECT_EQ(1,row(1)->Layer);
    layer->ChangeValue("5");
    wxCommandEvent typed(wxEVT_TEXT_ENTER,layer->GetId()); typed.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(typed);
    EXPECT_EQ(5,row(0)->Layer); EXPECT_EQ(5,row(1)->Layer); EXPECT_EQ("5",layer->GetValue());
    layer->ChangeValue("x"); layer->GetEventHandler()->ProcessEvent(typed);
    EXPECT_EQ(5,row(0)->Layer); EXPECT_EQ("5",layer->GetValue()) << "not a number: back to the layer";
    c->selectionController->SetSelectionAndActiveIDs({first},first);

    // Enter in a field is done with it: it does not go to the next line
    wxTextCtrl *actor=nullptr;
    for (auto child:descendants(box)) if (auto a=dynamic_cast<wxTextCtrl*>(child); a && a->GetToolTipText().StartsWith("Actor name")) actor=a;
    ASSERT_NE(nullptr,actor);
    size_t lines=LiveRead(*c).Events.size();
    for (wxTextCtrl *field:{actor,layer}) {
        wxKeyEvent enter(wxEVT_CHAR_HOOK); enter.m_keyCode=WXK_RETURN; enter.SetEventObject(field);
        field->GetEventHandler()->ProcessEvent(enter);
        EXPECT_EQ(lines,LiveRead(*c).Events.size());
        EXPECT_EQ(first,c->selectionController->ActiveID());
    }
    // ...and goes back to the text, as a click anywhere else does
    window->Show(); box->SetSize(0,0,900,300); box->Layout();
    for (int i=0;i<20;++i) wxYield();
    actor->SetFocus(); for (int i=0;i<20;++i) wxYield();
    if (wxWindow::FindFocus()==actor) {
        wxKeyEvent enter(wxEVT_CHAR_HOOK); enter.m_keyCode=WXK_RETURN; enter.SetEventObject(actor);
        actor->GetEventHandler()->ProcessEvent(enter);
        EXPECT_EQ(static_cast<wxWindow*>(editor),wxWindow::FindFocus());
        layer->SetFocus(); for (int i=0;i<20;++i) wxYield();
        ASSERT_EQ(static_cast<wxWindow*>(layer),wxWindow::FindFocus());
        wxMouseEvent click(wxEVT_LEFT_DOWN); click.SetEventObject(box); box->GetEventHandler()->ProcessEvent(click);
        EXPECT_EQ(static_cast<wxWindow*>(editor),wxWindow::FindFocus()) << "a click on the box's background";
        layer->SetFocus(); for (int i=0;i<20;++i) wxYield();
        wxMouseEvent in_field(wxEVT_LEFT_DOWN); in_field.SetEventObject(layer); layer->GetEventHandler()->ProcessEvent(in_field);
        EXPECT_EQ(static_cast<wxWindow*>(layer),wxWindow::FindFocus()) << "a click in the field stays in it";
    }
    else
        std::cerr << "focus not available here: skipped the focus checks\n";
    window->Hide();

    // The character count in the text box's corner: used of the limit, red over it
    Input("Hello");
    EXPECT_EQ("5 / 40",count->GetText()); EXPECT_FALSE(count->IsWarning());
    EXPECT_TRUE(count->GetToolTipText().Contains("40"));
    Input(std::string(41,'a')+"\\Nshort");
    EXPECT_EQ("41 / 40",count->GetText()); EXPECT_TRUE(count->IsWarning()) << "the longest line counts";
    OPT_SET("Subtitle/Character Limit")->SetInt(0);
    Input("{\\b1}abc");
    EXPECT_EQ("3",count->GetText()) << "no limit; tags are not counted";
    EXPECT_GT(count->GetPosition().x,editor->GetClientSize().x/2); EXPECT_GT(count->GetPosition().y,0);

    // The original text, under a caption saying what it is
    EXPECT_FALSE(caption->IsShown());
    original->SetValue(true);
    wxCommandEvent check(wxEVT_CHECKBOX,original->GetId()); check.SetInt(1); check.SetEventObject(original);
    original->GetEventHandler()->ProcessEvent(check);
    EXPECT_TRUE(caption->IsShown());
    original->SetValue(false); check.SetInt(0); original->GetEventHandler()->ProcessEvent(check);
    OPT_SET("Subtitle/Show Original")->SetBool(false);
}
TEST_F(ordinary_edit, edit_box_style_box_is_typed_in_and_completes_a_style) {
    int second;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     for (auto name:{"Sign","Song","Default Alt","正文"}) {auto style=new AssStyle();style->name=name;file.Styles.push_back(*style);}
     auto other=new AssDialogue();second=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    FlatChoice *style=nullptr;
    for (auto child:descendants(box)) if (auto s=dynamic_cast<FlatChoice*>(child)) style=s;
    ASSERT_NE(nullptr,style);
    auto editor=style->GetEditor(); ASSERT_NE(nullptr,editor);
    auto line_style=[&](int n) { return std::next(LiveRead(*c).Events.begin(),n)->Style.get(); };
    // Typing replaces what is chosen in the field, as a key does
    auto type=[&](wxString const& text) { editor->WriteText(text); wxTheApp->ProcessPendingEvents(); };
    auto chosen=[&] { long from,to; editor->GetSelection(&from,&to); return editor->GetValue().Mid(from,to-from); };
    EXPECT_EQ("Default",editor->GetValue()) << "the field shows the line's style";

    // A letter: the first style beginning with it, ignoring case, written at once and completed in the field
    editor->SelectAll(); type("S");
    EXPECT_EQ("Sign",line_style(0)); EXPECT_EQ("Sign",editor->GetValue()); EXPECT_EQ("ign",chosen());
    // The next letter types over the completed rest
    type("o"); EXPECT_EQ("Song",line_style(0)); EXPECT_EQ("Song",editor->GetValue()); EXPECT_EQ("ng",chosen());
    // Spaces and other scripts are typed as any letter
    editor->SelectAll(); type("default a"); EXPECT_EQ("Default Alt",line_style(0));
    editor->SelectAll(); type(wxString::FromUTF8("正")); EXPECT_EQ(wxString::FromUTF8("正文"),wxString::FromUTF8(line_style(0).c_str()));
    // Nothing beginning so: the line keeps its style; leaving the field shows it again
    editor->SelectAll(); type("x"); EXPECT_EQ(wxString::FromUTF8("正文"),wxString::FromUTF8(line_style(0).c_str())); EXPECT_EQ("x",editor->GetValue());
    {wxKeyEvent enter(wxEVT_CHAR_HOOK); enter.m_keyCode=WXK_RETURN; enter.SetEventObject(editor); editor->GetEventHandler()->ProcessEvent(enter);}
    EXPECT_EQ(wxString::FromUTF8("正文"),editor->GetValue()) << "Enter is done with it";
    // Backspace with the rest completed takes off a typed letter
    editor->SelectAll(); type("so"); editor->SetSelection(2,4);
    {wxKeyEvent back(wxEVT_KEY_DOWN); back.m_keyCode=WXK_BACK; back.SetEventObject(editor); editor->GetEventHandler()->ProcessEvent(back);}
    long from,to; editor->GetSelection(&from,&to); EXPECT_EQ(1,from); EXPECT_EQ(4,to);
    // Up and Down go through the styles from the field
    {wxKeyEvent down(wxEVT_KEY_DOWN); down.m_keyCode=WXK_DOWN; down.SetEventObject(editor); editor->GetEventHandler()->ProcessEvent(down);}
    EXPECT_EQ("Default Alt",line_style(0)); EXPECT_EQ("Default Alt",editor->GetValue());

    // Ctrl+Enter's command goes to the next line, the keyboard in the style field
    EXPECT_EQ(std::vector<std::string>({"Ctrl-Enter","Ctrl-KP_Enter"}),hotkey::get_hotkey_strs("Subtitle Edit Box","grid/line/next/style"));
    cmd::call("grid/line/next/style",c.get());
    EXPECT_EQ(second,c->selectionController->ActiveID());
    EXPECT_EQ("Default",editor->GetValue()) << "the new line's style";
    box->FocusStyle();
    if (wxWindow::FindFocus()) EXPECT_EQ(static_cast<wxWindow*>(editor),wxWindow::FindFocus());
    editor->SelectAll(); type("s"); EXPECT_EQ("Sign",line_style(1)); EXPECT_EQ("Default Alt",line_style(0));
}
TEST_F(ordinary_edit, dialog_buttons_are_drawn_and_answer_the_keys_the_system_buttons_do) {
    wxDialog dialog(window,-1,"buttons");
    auto field=new wxTextCtrl(&dialog,-1,"");
    auto notes=new wxTextCtrl(&dialog,-1,"",wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE);
    auto bar=new flat::DialogButtons(&dialog,wxOK|wxCANCEL|wxAPPLY|wxHELP);
    auto restore=bar->AddLeft("&Restore");
    auto sizer=new wxBoxSizer(wxVERTICAL); sizer->Add(field); sizer->Add(notes); sizer->Add(bar,0,wxEXPAND); dialog.SetSizerAndFit(sizer);
    std::map<int,int> pressed;
    for(int id:{int(wxID_OK),int(wxID_CANCEL),int(wxID_APPLY),int(wxID_HELP),restore->GetId()})
        dialog.Bind(wxEVT_BUTTON,[&pressed,id](wxCommandEvent&){++pressed[id];},id);
    // The standard ids and labels, the affirmative one filled
    for(int id:{int(wxID_OK),int(wxID_CANCEL),int(wxID_APPLY),int(wxID_HELP)}) ASSERT_NE(nullptr,bar->Get(id));
    EXPECT_TRUE(bar->Get(wxID_OK)->IsAccent()); EXPECT_FALSE(bar->Get(wxID_CANCEL)->IsAccent());
    EXPECT_EQ(flat::StockLabel(wxID_OK),bar->Get(wxID_OK)->GetLabel());
    EXPECT_EQ(wxGETTEXT_IN_CONTEXT("button","&Apply"),bar->Get(wxID_APPLY)->GetLabel()) << "our own words, which are translated even where wxWidgets' are not installed";
    EXPECT_EQ(dynamic_cast<FlatButton*>(dialog.FindWindow(wxID_OK)),bar->Get(wxID_OK));
    dialog.Show(); for(int i=0;i<10;++i) wxYield();
    auto key=[&](int code,int modifiers=0){wxKeyEvent e(wxEVT_CHAR_HOOK);e.m_keyCode=code;e.m_altDown=modifiers&wxMOD_ALT;e.m_controlDown=modifiers&wxMOD_CONTROL;e.SetEventObject(&dialog);dialog.GetEventHandler()->ProcessEvent(e);};
    // Escape presses Cancel
    key(WXK_ESCAPE); EXPECT_EQ(1,pressed[wxID_CANCEL]);
    // Enter in a one-line field presses OK; in a text of several lines it is the text's
    field->SetFocus(); for(int i=0;i<5;++i) wxYield();
    if (wxWindow::FindFocus()==field) { key(WXK_RETURN); EXPECT_EQ(1,pressed[wxID_OK]); }
    notes->SetFocus(); for(int i=0;i<5;++i) wxYield();
    if (wxWindow::FindFocus()==notes) { int before=pressed[wxID_OK]; key(WXK_RETURN); EXPECT_EQ(before,pressed[wxID_OK]); }
    key(WXK_RETURN,wxMOD_CONTROL); // not plain Enter
    // Alt with the underlined letter
    key('R',wxMOD_ALT); EXPECT_EQ(1,pressed[restore->GetId()]);
    // A disabled button is not pressed
    bar->Get(wxID_CANCEL)->Disable(); key(WXK_ESCAPE); EXPECT_EQ(1,pressed[wxID_CANCEL]); bar->Get(wxID_CANCEL)->Enable();
    // The title bar's close button presses Cancel
    {wxCloseEvent close(wxEVT_CLOSE_WINDOW,dialog.GetId()); close.SetCanVeto(true); close.SetEventObject(&dialog); dialog.GetEventHandler()->ProcessEvent(close);}
    EXPECT_EQ(2,pressed[wxID_CANCEL]);
    // Without a handler of its own, Cancel ends the dialog as the system's does
    wxDialog plain(window,-1,"plain"); auto plain_bar=new flat::DialogButtons(&plain,wxOK|wxCANCEL); plain.SetSizer(plain_bar); plain.Show();
    plain_bar->Get(wxID_CANCEL)->Press(); EXPECT_FALSE(plain.IsShown());
    // Close alone accepts, as CreateStdDialogButtonSizer has it
    wxDialog closing(window,-1,"close"); auto close_bar=new flat::DialogButtons(&closing,wxCLOSE); closing.SetSizer(close_bar);
    EXPECT_EQ(wxID_CLOSE,closing.GetAffirmativeId()); EXPECT_TRUE(close_bar->Get(wxID_CLOSE)->IsAccent());
}
TEST_F(ordinary_edit, number_field_is_typed_stepped_and_dragged_in_range) {
    auto field=flat::NumberField::Create(window,"Width",10,0,100,0.5,1);
    std::vector<double> taken; field->on_change=[&](double v){taken.push_back(v);};
    EXPECT_EQ("Width",field->Frame()->GetLabel()); EXPECT_EQ("10",field->Text()->GetValue());
    field->Text()->SetValue("12.25"); EXPECT_DOUBLE_EQ(12.3,field->GetValue()) << "one decimal kept";
    field->Text()->SetValue("500"); EXPECT_DOUBLE_EQ(12.3,field->GetValue()) << "out of range is not taken";
    field->Frame()->Wheel(2); EXPECT_DOUBLE_EQ(13.3,field->GetValue()); EXPECT_EQ("13.3",field->Text()->GetValue());
    field->Frame()->Scrub(4,false); field->Frame()->Scrub(-30,true); EXPECT_DOUBLE_EQ(0,field->GetValue()) << "clamped, from where the drag began";
    EXPECT_EQ((std::vector<double>{12.3,13.3,15.3,0}),taken);
    field->SetValue(7); EXPECT_EQ("7",field->Text()->GetValue()); EXPECT_EQ(4u,taken.size()) << "setting it is not the user's change";
    EXPECT_EQ("-0.5",flat::FormatNumber(-0.5,1)); EXPECT_EQ("0",flat::FormatNumber(-0.01,1));
    field->Frame()->Destroy();
}
TEST_F(ordinary_edit, flat_list_is_chosen_and_ticked_as_the_system_lists_are) {
    auto list=new FlatList(window,true,true,"Nothing");
    list->Set({{"Alpha","first","1"},{"Beta","",""},{"Gamma","",""}});
    std::vector<std::pair<wxEventType,int>> seen;
    for(auto type:{wxEVT_LISTBOX,wxEVT_LISTBOX_DCLICK,wxEVT_CHECKLISTBOX}) list->Bind(type,[&seen,type](wxCommandEvent&e){seen.emplace_back(type,e.GetInt());});
    EXPECT_EQ(3u,list->GetCount()); EXPECT_EQ("Beta",list->GetString(1)); EXPECT_EQ(wxNOT_FOUND,list->GetSelection());
    list->Click(0); list->Click(2,false,true); wxArrayInt chosen; EXPECT_EQ(3,list->GetSelections(chosen)) << "Shift reaches from the first";
    list->Click(1,true); EXPECT_FALSE(list->IsSelected(1)) << "Ctrl takes one away";
    list->Click(1,false,false,true); EXPECT_TRUE(list->IsChecked(1)); EXPECT_FALSE(list->IsSelected(1)) << "the box ticks without choosing";
    list->DoubleClick(2); EXPECT_EQ(2,list->GetSelection()); EXPECT_EQ("Gamma",list->GetStringSelection());
    EXPECT_EQ((std::vector<std::pair<wxEventType,int>>{{wxEVT_LISTBOX,0},{wxEVT_LISTBOX,2},{wxEVT_LISTBOX,1},{wxEVT_CHECKLISTBOX,1},{wxEVT_LISTBOX,2},{wxEVT_LISTBOX_DCLICK,2}}),seen);
    seen.clear(); list->SetSelection(0); list->Check(2); EXPECT_TRUE(seen.empty()) << "setting is not the user's change";
    // One at a time: Ctrl and Shift choose only the row
    auto single=new FlatList(window); single->Set(wxArrayString(3,std::array<wxString,3>{"a","b","c"}.data()));
    single->Click(0); single->Click(2,false,true); EXPECT_EQ(2,single->GetSelection()); EXPECT_FALSE(single->IsSelected(0));
    EXPECT_TRUE(single->SetStringSelection("b")); EXPECT_EQ(1,single->GetSelection());
    list->Destroy(); single->Destroy();
}
TEST_F(ordinary_edit, choice_chooses_an_item_by_its_first_letters) {
    auto choice=new FlatChoice(window,"");
    choice->Set({"Default","Sign","Song","Default Alt"});
    std::vector<int> chosen; choice->on_choose=[&](int i){chosen.push_back(i);};
    auto type=[&](wxChar ch,bool ctrl=false) { wxKeyEvent e(wxEVT_CHAR); e.m_uniChar=ch; e.m_keyCode=ch; e.m_controlDown=ctrl; e.SetEventObject(choice); choice->GetEventHandler()->ProcessEvent(e); };
    auto pause=[] { wxMilliSleep(1050); };
    type('S'); EXPECT_EQ("Sign",choice->GetStringSelection());
    type('o'); EXPECT_EQ("Song",choice->GetStringSelection()) << "letters within a second narrow it";
    pause(); type('d'); type('e');
    wxKeyEvent space(wxEVT_KEY_DOWN); space.m_keyCode=WXK_SPACE; space.SetEventObject(choice); choice->GetEventHandler()->ProcessEvent(space);
    EXPECT_TRUE(space.GetSkipped()) << "a space typed in a name is part of it";
    type('f'); type('a'); type('u'); type('l'); type('t'); type(' '); type('a'); EXPECT_EQ("Default Alt",choice->GetStringSelection());
    pause(); type('s'); type('s'); EXPECT_EQ("Song",choice->GetStringSelection()) << "the same letter again goes on";
    type('s'); EXPECT_EQ("Sign",choice->GetStringSelection());
    pause(); type('x'); EXPECT_EQ("Sign",choice->GetStringSelection());
    pause(); type('d',true); EXPECT_EQ("Sign",choice->GetStringSelection()) << "Ctrl with a letter is a hotkey";
    EXPECT_EQ((std::vector<int>{1,2,0,3,1,2,1}),chosen);
    choice->Destroy();
}

TEST_F(ordinary_edit, style_panel_wheel_goes_through_lists_and_transform_has_titles) {
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     for (auto name:{"Alpha","Beta"}) {auto style=new AssStyle();style->name=name;file.Styles.push_back(*style);}
     file.Events.front().Text="Hello";
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto row=[&] { return &LiveRead(*c).Events.front(); };
    int first=row()->Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Styles in the file's order: down the list, then back up; one undo step
    panel->Wheel("style",-1); EXPECT_EQ("Alpha",row()->Style.get()); EXPECT_EQ("Alpha",shown()["style"]);
    panel->Wheel("style",-1); EXPECT_EQ("Beta",row()->Style.get());
    panel->Wheel("style",-5); EXPECT_EQ("Beta",row()->Style.get()) << "stays at the end";
    panel->Wheel("style",1); EXPECT_EQ("Alpha",row()->Style.get());
    c->subsController->Undo(); EXPECT_EQ("Default",row()->Style.get()) << "turns close together are one undo step";

    // Weight: heavier going up, each line keeping its slant
    panel->UpdateNow(); panel->Wheel("weight",1); EXPECT_EQ("{\\b500}Hello",std::string(row()->Text.get()));
    panel->UpdateNow(); panel->Wheel("weight",2); EXPECT_EQ("{\\b1}Hello",std::string(row()->Text.get()));
    panel->UpdateNow(); panel->Wheel("weight",-3); EXPECT_EQ("Hello",std::string(row()->Text.get()));

    // Fonts: the installed ones, in order
    wxString before=shown()["font"];
    panel->Wheel("font",-1);
    wxString after=shown()["font"];
    EXPECT_NE(before,after);
    EXPECT_EQ("{\\fn"+after+"}Hello",wxString::FromUTF8(row()->Text.get().c_str()));
    panel->UpdateNow(); panel->Wheel("font",-1); EXPECT_NE(after,shown()["font"]);
    panel->UpdateNow(); panel->Wheel("font",1); EXPECT_EQ(after,shown()["font"]) << "and back up";

    // Transform: a title over each group
    auto s=shown();
    EXPECT_EQ("Position",s["title_position"]); EXPECT_EQ("Margins",s["title_margins"]); EXPECT_EQ("Scale",s["title_scale"]);
    EXPECT_EQ("Rotation",s["title_rotation"]); EXPECT_EQ("Opacity",s["title_opacity"]);
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_sets_karaoke_colour_and_margins) {
    int placed;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->Margin={{10,20,30}};style->secondary=agi::Color(0,0,255);file.Styles.push_back(*style);
     auto &row=file.Events.front();row.Style="Look";row.Text="Hello";
     auto other=new AssDialogue();other->Style="Look";other->Text="{\\pos(1,2)}placed";placed=other->Id;file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto text=[&](int n) { return std::string(std::next(LiveRead(*c).Events.begin(),n)->Text.get()); };
    auto margin=[&](int n, int i) { return std::next(LiveRead(*c).Events.begin(),n)->Margin[i]; };
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    c->textSelectionController->SetSelection(0,0);
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    // Karaoke colour: \2c and \2a, hidden like the fill
    EXPECT_EQ("on #0000FF",shown()["secondary"]); EXPECT_EQ("100",shown()["secondary_opacity"]);
    panel->Check("secondary",false); EXPECT_EQ("{\\2a&HFF&}Hello",text(0)); EXPECT_EQ("off #0000FF",shown()["secondary"]);
    panel->Check("secondary",true); EXPECT_EQ("Hello",text(0));
    panel->ChooseColour("secondary",agi::Color(0,255,0)); EXPECT_EQ("{\\2c&H00FF00&}Hello",text(0));
    panel->UpdateNow(); panel->EnterNumber("secondary_opacity",50,true);
    EXPECT_EQ("{\\2c&H00FF00&\\2a&H80&}Hello",text(0)); EXPECT_EQ("50",shown()["secondary_opacity"]);

    // Margins: 0 shows the style's, dim
    auto s=shown();
    EXPECT_EQ("10 (style)",s["margin_left"]); EXPECT_EQ("20 (style)",s["margin_right"]); EXPECT_EQ("30 (style)",s["margin_vertical"]);
    EXPECT_EQ(0u,s.count("margin_left disabled"));
    panel->EnterNumber("margin_left",5,true); EXPECT_EQ(5,margin(0,0)); EXPECT_EQ("5",shown()["margin_left"]);
    EXPECT_EQ(_("left margin change"),c->subsController->GetUndoDescription());
    // A drag is one undo step
    panel->EnterNumber("margin_vertical",31,false); panel->EnterNumber("margin_vertical",40,false); panel->EnterNumber("margin_vertical",42,true);
    EXPECT_EQ(42,margin(0,2));
    c->subsController->Undo(); EXPECT_EQ(0,margin(0,2)); EXPECT_EQ(5,margin(0,0));
    panel->EnterNumber("margin_left",0,true); EXPECT_EQ("10 (style)",shown()["margin_left"]);
    panel->EnterNumber("margin_right",-4,true); EXPECT_EQ(0,margin(0,1)) << "not below 0";

    // Lines differ; a line placed by \pos doesn't use them
    panel->EnterNumber("margin_left",7,true);
    c->selectionController->SetSelectionAndActiveIDs({first,placed},first);
    EXPECT_EQ(wxS("—"),shown()["margin_left"]); EXPECT_EQ("30 (style)",shown()["margin_vertical"]);
    EXPECT_EQ(0u,shown().count("margin_left disabled"));
    c->selectionController->SetSelectionAndActiveIDs({placed},placed);
    EXPECT_EQ(0u,shown().count("margin_left disabled")) << "a line placed by \\pos can still have its margins changed";
    EXPECT_EQ("{\\pos(1,2)}placed",text(1));
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_folds_away_in_narrow_windows_and_opens_from_its_edge) {
    OPT_SET("App/Style Panel/Show")->SetBool(true);
    auto frame=new wxFrame(nullptr,-1,"edge");
    auto root=new wxPanel(frame);
    auto panel=new StylePanel(root,c.get());
    auto edge=new StylePanelEdge(root,panel);
    auto sizer=new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(new wxPanel(root),1,wxEXPAND);sizer->Add(edge,0,wxEXPAND);sizer->Add(panel,0,wxEXPAND);
    root->SetSizer(sizer);
    auto resize=[&](int width) { root->SetSize(width,600); edge->Update(); root->Layout(); };
    resize(root->FromDIP(1400));
    EXPECT_FALSE(edge->Folded());
    EXPECT_GE(panel->GetSize().x,panel->FromDIP(StylePanel::min_width));
    resize(root->FromDIP(900));
    EXPECT_TRUE(edge->Folded()) << "narrow: folded";
    EXPECT_GT(edge->GetSize().x,theme::BlockGap(root)) << "a strip wide enough to click";
    edge->Open();
    EXPECT_FALSE(edge->Folded()) << "opened by hand";
    resize(root->FromDIP(1000));
    EXPECT_FALSE(edge->Folded()) << "stays open while narrow";
    resize(root->FromDIP(1400)); resize(root->FromDIP(900));
    EXPECT_TRUE(edge->Folded()) << "folds again after being wide";
    // The option hides it everywhere; asking for it opens it even when narrow
    resize(root->FromDIP(1400));
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    EXPECT_TRUE(edge->Folded());
    resize(root->FromDIP(900));
    OPT_SET("App/Style Panel/Show")->SetBool(true);
    EXPECT_FALSE(edge->Folded());
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    edge->Open();
    EXPECT_TRUE(OPT_GET("App/Style Panel/Show")->GetBool()) << "the strip turns the option back on";
    EXPECT_FALSE(edge->Folded());
    // The » button folds it away: the option off when wide, back to folded
    // when it was opened by hand in a narrow window
    resize(root->FromDIP(1400));
    panel->Press("collapse");
    EXPECT_FALSE(OPT_GET("App/Style Panel/Show")->GetBool());
    EXPECT_TRUE(edge->Folded());
    edge->Open();
    EXPECT_FALSE(edge->Folded());
    resize(root->FromDIP(900));
    edge->Open();
    ASSERT_FALSE(edge->Folded());
    panel->Press("collapse");
    EXPECT_TRUE(edge->Folded());
    EXPECT_TRUE(OPT_GET("App/Style Panel/Show")->GetBool()) << "still shown once the window is wide";
    resize(root->FromDIP(1400));
    EXPECT_FALSE(edge->Folded());
    // A shape line asks for the panel: it opens
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    ASSERT_TRUE(edge->Folded());
    panel->EditShape();
    EXPECT_FALSE(edge->Folded());
    // Folded, the strip names the pages: each opens the panel on its own
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    ASSERT_TRUE(edge->Folded());
    int x=edge->GetClientSize().x-edge->FromDIP(10);
    int style_at=edge->PageAt(wxPoint(x,edge->FromDIP(40)));
    EXPECT_EQ(0,style_at) << "the first name, Style";
    int timing_at=-1, animation_at=-1;
    for(int y=0;y<edge->FromDIP(400);y+=2){int p=edge->PageAt(wxPoint(x,y));if(p==1&&animation_at<0)animation_at=y;if(p==2&&timing_at<0)timing_at=y;}
    ASSERT_GT(animation_at,0); ASSERT_GT(timing_at,animation_at) << "Style, Animation, Timing down the strip";
    EXPECT_EQ(-1,edge->PageAt(wxPoint(x,edge->FromDIP(500)))) << "below the names: none";
    edge->Open(StylePanel::Page::Timing);
    EXPECT_FALSE(edge->Folded());
    EXPECT_EQ(StylePanel::Page::Timing,panel->ShownPage());
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    edge->Open();
    EXPECT_EQ(StylePanel::Page::Timing,panel->ShownPage()) << "elsewhere on the strip: the page last shown";
    panel->ShowPage(StylePanel::Page::Style);
    delete frame; // now: it watches an option the next test changes
}

TEST(style_list, samples_dark_letters_on_a_light_field) {
    AssStyle white; white.primary=agi::Color(255,255,255); white.outline=agi::Color(0,0,0); white.outline_w=2;
    auto light=[](wxColour c){ return c.Red()+c.Green()+c.Blue()>3*128; };
    EXPECT_TRUE(light(StyleList::SampleField(white))) << "a black border: the edge is black";
    white.outline_w=0;
    EXPECT_FALSE(light(StyleList::SampleField(white))) << "white letters: the dark field, as on video";
    AssStyle black; black.primary=agi::Color(0,0,0); black.outline_w=0;
    EXPECT_TRUE(light(StyleList::SampleField(black))) << "black letters, no border";
    black.outline=agi::Color(0,0,0); black.outline_w=2;
    EXPECT_TRUE(light(StyleList::SampleField(black))) << "black letters, black border";
    black.outline=agi::Color(255,255,255);
    EXPECT_FALSE(light(StyleList::SampleField(black))) << "a white border shows on the dark field";
    AssStyle navy; navy.primary=agi::Color(20,30,90); navy.outline_w=0;
    EXPECT_TRUE(light(StyleList::SampleField(navy))) << "dark blue too";
}

TEST(tag_summary, says_what_each_block_of_tags_does) {
    using namespace tag_summary;
    auto kinds=[](Piece const& p){ std::vector<Kind> k; for(auto const& m:p.marks)k.push_back(m.kind); return k; };
    auto u=[](const char *text){ return wxString::FromUTF8(text); };
    auto pieces=Summarize("{\\fad(200,200)\\pos(640,80)}你好{\\c&H0000FF&}世界{译注：双关}");
    ASSERT_EQ(5u,pieces.size());
    EXPECT_TRUE(pieces[0].block);
    EXPECT_EQ((std::vector<Kind>{Kind::Fade,Kind::Pos}),kinds(pieces[0]));
    EXPECT_EQ("你好",pieces[1].text);
    ASSERT_EQ((std::vector<Kind>{Kind::Colour}),kinds(pieces[2]));
    EXPECT_EQ(1,pieces[2].marks[0].index);
    EXPECT_EQ(agi::Color(255,0,0),pieces[2].marks[0].colour) << "&HBBGGRR&";
    EXPECT_EQ((std::vector<Kind>{Kind::Note}),kinds(pieces[4])) << "a block without tags";
    EXPECT_EQ(u("[Fade·Position]"),PlainText(pieces[0]));
    auto tip=Describe(pieces[0]);
    EXPECT_NE(wxString::npos,tip.find("Fade: in 200 ms, out 200 ms")) << tip;
    EXPECT_NE(wxString::npos,tip.find("Position: (640, 80)")) << tip;
    EXPECT_TRUE(tip.EndsWith("{\\fad(200,200)\\pos(640,80)}")) << "the block as written last";
    EXPECT_EQ(u("Note: 译注：双关"),Describe(pieces[4]));
    EXPECT_EQ(u("Primary colour: #FF0000\n{\\c&H0000FF&}"),Describe(pieces[2]));

    // The longest name first: \bord not \b, \be and \blur one kind, \fscx not \fs
    auto one=Summarize("{\\bord2\\be1\\b1\\blur3\\fscx120\\fscy80\\fs40\\fsp2\\an8}x");
    EXPECT_EQ((std::vector<Kind>{Kind::Border,Kind::Blur,Kind::Bold,Kind::Scale,Kind::FontSize,Kind::Spacing,Kind::Align}),kinds(one[0]));
    EXPECT_EQ(8,one[0].marks.back().index);
    EXPECT_EQ(2u,one[0].marks[3].tags.size()) << "\\fscx and \\fscy together";
    // \t's tags are its own; \i0 is switched off
    auto t=Summarize("{\\t(0,500,\\frz30\\fscx120)\\i0\\b1}x");
    EXPECT_EQ((std::vector<Kind>{Kind::Transform,Kind::Italic,Kind::Bold}),kinds(t[0]));
    EXPECT_TRUE(t[0].marks[1].off); EXPECT_FALSE(t[0].marks[2].off);
    // Colours one a mark each, \3c and \4c told apart
    auto c=Summarize("{\\3c&H000000&\\4c&H404040&\\c&HFF0000&}x");
    ASSERT_EQ(3u,c[0].marks.size());
    EXPECT_EQ(3,c[0].marks[0].index); EXPECT_EQ(4,c[0].marks[1].index); EXPECT_EQ(1,c[0].marks[2].index);
    EXPECT_EQ(agi::Color(0,0,255),c[0].marks[2].colour);
    // A rectangle clip and a drawn one
    EXPECT_EQ(Kind::Clip,Summarize("{\\clip(0,0,640,360)}")[0].marks[0].kind);
    EXPECT_EQ(Kind::VectorClip,Summarize("{\\clip(m 0 0 l 10 0 10 10)}")[0].marks[0].kind);
    EXPECT_EQ(Kind::VectorClip,Summarize("{\\iclip(2,m 0 0 l 10 0 10 10)}")[0].marks[0].kind);
    // \a as \an; tags it doesn't know
    EXPECT_EQ(7,Summarize("{\\a5}")[0].marks[0].index);
    EXPECT_EQ(5,Summarize("{\\a10}")[0].marks[0].index);
    auto unknown=Summarize("{\\xyz9\\q2}");
    EXPECT_EQ((std::vector<Kind>{Kind::Unknown}),kinds(unknown[0])) << "one icon for those it doesn't know";
    EXPECT_EQ(2u,unknown[0].marks[0].tags.size());
    EXPECT_NE(wxString::npos,Describe(unknown[0]).find("\\xyz9 \\q2")) << "each named in the tooltip";

    // Karaoke timing alone: a bar between syllables
    auto k=Summarize("{\\k20}这{\\kf30}是{\\k10\\b1}卡");
    EXPECT_TRUE(k[0].karaoke); EXPECT_TRUE(k[2].karaoke);
    EXPECT_FALSE(k[4].karaoke) << "with other tags: a chip";
    EXPECT_EQ("|",PlainText(k[0]));

    // A drawing: its points counted, the block ending it left out
    auto d=Summarize("{\\an7\\pos(10,10)\\p1}m 0 0 l 100 0 100 100 0 100{\\p0}after");
    ASSERT_EQ(3u,d.size());
    EXPECT_EQ((std::vector<Kind>{Kind::Align,Kind::Pos,Kind::Drawing}),kinds(d[0]));
    EXPECT_EQ(4,d[1].drawing_points);
    EXPECT_EQ("after",d[2].text);
    EXPECT_EQ("(4 points)",PlainText(d[1]));

    // Braces left open: text as written
    auto open=Summarize("abc{\\b1");
    ASSERT_EQ(1u,open.size()); EXPECT_EQ("abc{\\b1",open[0].text);
    EXPECT_TRUE(Summarize("").empty());

    // Every kind drawn from an icon has one
    for(int i=0;i<=static_cast<int>(Kind::Unknown);++i){
        auto kind=static_cast<Kind>(i);
        if(kind==Kind::Colour||kind==Kind::Align){EXPECT_EQ(nullptr,IconName(kind));continue;}
        ASSERT_NE(nullptr,IconName(kind)) << i;
        EXPECT_NE(nullptr,libresrc_modern_icon(IconName(kind))) << IconName(kind);
        EXPECT_FALSE(KindName(kind).empty());
    }
}

TEST_F(ordinary_edit, grid_text_column_draws_tag_chips_with_tooltips) {
    auto columns=GetGridColumns();
    auto &text=*columns.back();
    ASSERT_EQ(_("Text"),text.Header());
    AssDialogue line;
    line.Text=std::string("{\\fad(200,200)\\pos(640,80)}Hello{\\i1}there");
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Subtitle/Grid/Hide Overrides")->SetInt(1); };
    OPT_SET("Subtitle/Grid/Hide Overrides")->SetInt(1);
    wxBitmap bitmap(600,40);
    wxMemoryDC dc(bitmap);
    dc.SetFont(*wxNORMAL_FONT);
    dc.SetBrush(*wxWHITE_BRUSH);dc.SetTextForeground(*wxBLACK);
    dc.Clear();
    text.Paint(dc,0,0,&line,c.get());
    // Chips are drawn: something other than the text's black and the row's white
    wxImage image=bitmap.ConvertToImage();
    bool grey=false;
    for(int y=0;y<image.GetHeight()&&!grey;++y)for(int x=0;x<GridColumn::cell_padding+40&&!grey;++x){
        int r=image.GetRed(x,y);grey=r>200&&r<250&&image.GetGreen(x,y)==r;
    }
    EXPECT_TRUE(grey) << "the chip's fill";
    // Its tooltip says what it does; between the chips, none
    auto tip=text.Tooltip(dc,GridColumn::cell_padding+4,&line,c.get());
    EXPECT_NE(wxString::npos,tip.find("Fade")) << tip;
    EXPECT_TRUE(text.Tooltip(dc,2,&line,c.get()).empty()) << "left of it";
    int hello=dc.GetTextExtent("Hel").x;
    // Past the first chip, in "Hello": none
    std::vector<int> tipped;
    for(int x=0;x<400;++x) if(!text.Tooltip(dc,x,&line,c.get()).empty()) tipped.push_back(x);
    ASSERT_FALSE(tipped.empty());
    bool gap=false;for(size_t i=1;i<tipped.size();++i)if(tipped[i]>tipped[i-1]+hello)gap=true;
    EXPECT_TRUE(gap) << "two chips with the text between";
    // Shown in full, or left out: no chips, no tooltips
    OPT_SET("Subtitle/Grid/Hide Overrides")->SetInt(0);
    EXPECT_TRUE(text.Tooltip(dc,GridColumn::cell_padding+4,&line,c.get()).empty());
    OPT_SET("Subtitle/Grid/Hide Overrides")->SetInt(2);
    EXPECT_TRUE(text.Tooltip(dc,GridColumn::cell_padding+4,&line,c.get()).empty());
    dc.SelectObject(wxNullBitmap);
}

TEST(script_text, splits_into_one_sentence_a_line) {
    using script_text::SplitSentences;
    auto u=[](const char *text){ return wxString::FromUTF8(text); };
    EXPECT_EQ(u("今天天气很好。\n我们去公园吧！\n好吗？"),SplitSentences(u("今天天气很好。我们去公园吧！好吗？")));
    EXPECT_EQ(u("他说：“好。”\n然后走了。"),SplitSentences(u("他说：“好。”然后走了。"))) << "the closing quote with its sentence";
    EXPECT_EQ(u("真的吗？！\n嗯……\n我想想。"),SplitSentences(u("真的吗？！嗯……我想想。"))) << "a run of marks together";
    EXPECT_EQ(u("Hello there.\nIt costs 3.14 dollars!\nReally?!"),SplitSentences(u("Hello there. It costs 3.14 dollars! Really?!")))
        << "half-width marks only before a space";
    EXPECT_EQ(u("Wait...\n\"Why?\"\nNo."),SplitSentences(u("Wait... \"Why?\" No.")));
    EXPECT_EQ(u("e.g.this stays"),SplitSentences(u("e.g.this stays")));
    EXPECT_EQ(u("第一行没有标点\n第二行。"),SplitSentences(u("第一行没有标点\n\n  第二行。　"))) << "lines kept, empty ones and spaces gone";
    EXPECT_EQ(u("a\nb"),SplitSentences(u("a\r\nb")));
    EXPECT_EQ(u(""),SplitSentences(u("   \n")));
    auto once=SplitSentences(u("一。二！三"));
    EXPECT_EQ(once,SplitSentences(once)) << "split again: as it was";
    EXPECT_EQ(3,script_text::CountLines(u("a\n\nb\n c \n")));
    EXPECT_EQ(0,script_text::CountLines(u("")));
}

TEST_F(ordinary_edit, script_text_is_saved_in_the_ass_file_and_marks_it_changed) {
    using Field=SubtitleProjectAccess::MetadataField;
    auto path=agi::fs::path((std::filesystem::current_path()/"script-text.ass").wstring());
    c->subsController->Save(path,"utf-8");ASSERT_FALSE(c->subsController->IsModified());
    auto undo=c->subsController->GetUndoDescription();
    // The characters the section uses, a line break and a line starting with "["
    std::string script="第一句，有逗号: 冒号 # 井号 | 竖线。\n[Events]\n  indented\n";
    c->subtitleAccess->SetMetadata(Field::ScriptText,script);
    EXPECT_TRUE(c->subsController->IsModified());
    EXPECT_EQ(undo,c->subsController->GetUndoDescription()) << "not a step to undo";
    c->subsController->Save(path,"utf-8");EXPECT_FALSE(c->subsController->IsModified());
    {
        std::ifstream in(path.string());std::string all((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        EXPECT_NE(std::string::npos,all.find("\nScript Text: ")) << "in [Aegisub Project Garbage]";
        EXPECT_EQ(std::string::npos,all.find("\n[Events]\n  indented")) << "on one line";
    }
    c->subsController->Close();
    EXPECT_EQ("",LiveRead(*c).Properties.script_text);
    c->subsController->Load(path,"utf-8");
    EXPECT_EQ(script,LiveRead(*c).Properties.script_text);
    EXPECT_FALSE(c->subsController->IsModified());
    EXPECT_EQ(1u,static_cast<size_t>(std::distance(LiveRead(*c).Events.begin(),LiveRead(*c).Events.end()))) << "no line read from it";
    // Emptied: a change too
    c->subtitleAccess->SetMetadata(Field::ScriptText,"");
    EXPECT_TRUE(c->subsController->IsModified());
}

TEST_F(ordinary_edit, script_panel_takes_the_style_panels_place_and_writes_the_script) {
    auto u=[](const char *text){ return wxString::FromUTF8(text); };
    OPT_SET("App/Style Panel/Show")->SetBool(true);
    OPT_SET("App/Script Panel/Show")->SetBool(false);
    auto frame=new wxFrame(nullptr,-1,"script");
    BOOST_SCOPE_EXIT_ALL(&){ delete frame; OPT_SET("App/Script Panel/Show")->SetBool(false); OPT_SET("App/Style Panel/Show")->SetBool(true); };
    auto root=new wxPanel(frame);
    auto panel=new StylePanel(root,c.get());
    auto script=new ScriptPanel(root,c.get());
    auto edge=new StylePanelEdge(root,panel,script);
    auto sizer=new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(new wxPanel(root),1,wxEXPAND);sizer->Add(edge,0,wxEXPAND);sizer->Add(panel,0,wxEXPAND);sizer->Add(script,0,wxEXPAND);
    root->SetSizer(sizer);
    frame->SetSize(frame->FromDIP(1400),700);frame->Show();
    root->SetSize(frame->GetClientSize());edge->Update();root->Layout();
    panel->ShowPage(StylePanel::Page::Timing);
    EXPECT_FALSE(script->IsShown());
    EXPECT_TRUE(panel->IsShown());
    EXPECT_EQ(0u,panel->Shown().count("script")) << "no button for it on the style panel";

    // It takes the style panel's place, as wide; the script panel's own
    // button puts the style panel back on its page
    edge->OpenScript();
    wxYield();
    EXPECT_TRUE(edge->ScriptShown());
    EXPECT_TRUE(script->IsShown());
    EXPECT_FALSE(panel->IsShown()) << "not a page of the style panel: one or the other";
    EXPECT_TRUE(OPT_GET("App/Script Panel/Show")->GetBool());
    EXPECT_EQ(2,OPT_GET("App/Style Panel/Page")->GetInt()) << "the style panel's page left as it was";
    EXPECT_EQ(panel->GetMinSize().x,script->GetSize().x);
    EXPECT_GT(script->TextBox()->GetSize().y,script->GetClientSize().y/2) << "the text down the panel";
    EXPECT_EQ("One sentence a line disabled",script->Shown()["split"]) << "nothing to split";
    script->Press("style");
    EXPECT_FALSE(edge->ScriptShown());
    EXPECT_FALSE(script->IsShown());
    EXPECT_TRUE(panel->IsShown());
    EXPECT_EQ(StylePanel::Page::Timing,panel->ShownPage());
    edge->ToggleScript();
    EXPECT_TRUE(script->IsShown()) << "as the View menu does";
    EXPECT_FALSE(panel->IsShown());
    edge->ToggleScript();
    EXPECT_TRUE(panel->IsShown()) << "and again: back";
    EXPECT_FALSE(script->IsShown());
    // A page asked for, as a shape line does, brings the style panel back
    edge->OpenScript();
    ASSERT_TRUE(script->IsShown());
    panel->EditShape();
    EXPECT_TRUE(panel->IsShown());
    EXPECT_FALSE(script->IsShown());
    // » folds it away; opened again, it is the script panel still
    edge->OpenScript();
    script->Press("collapse");
    EXPECT_TRUE(edge->Folded());
    EXPECT_FALSE(OPT_GET("App/Style Panel/Show")->GetBool());
    edge->Open();
    EXPECT_TRUE(script->IsShown());
    EXPECT_FALSE(panel->IsShown());

    // Typed: in the file at once, which is changed but has nothing to undo
    auto undo=c->subsController->GetUndoDescription();
    script->Type(u("第一句。第二句！\n第三句"));
    EXPECT_EQ("第一句。第二句！\n第三句",LiveRead(*c).Properties.script_text);
    EXPECT_TRUE(c->subsController->IsModified());
    EXPECT_EQ(undo,c->subsController->GetUndoDescription());
    EXPECT_EQ("2 lines",script->Shown()["count"]);
    EXPECT_EQ("One sentence a line",script->Shown()["split"]);

    // Split, then taken back by the same button
    script->Press("split");
    EXPECT_EQ(u("第一句。\n第二句！\n第三句"),script->TextBox()->GetValue());
    EXPECT_EQ("第一句。\n第二句！\n第三句",LiveRead(*c).Properties.script_text);
    EXPECT_EQ("3 lines",script->Shown()["count"]);
    EXPECT_EQ("Undo the split",script->Shown()["split"]);
    script->Press("split");
    EXPECT_EQ("第一句。第二句！\n第三句",LiveRead(*c).Properties.script_text);
    EXPECT_EQ("One sentence a line",script->Shown()["split"]);
    // Typing after a split keeps it
    script->Press("split");
    script->Type(script->TextBox()->GetValue()+"\n");
    EXPECT_EQ("One sentence a line",script->Shown()["split"]);
    EXPECT_EQ("第一句。\n第二句！\n第三句\n",LiveRead(*c).Properties.script_text);

    // Keys editing text stay in the box; the window's others go on to it
    std::vector<int> reached;
    {
        frame->Bind(wxEVT_CHAR_HOOK,[&](wxKeyEvent &e){ reached.push_back(e.GetKeyCode()); });
        auto press=[&](int key,int modifiers){
            wxKeyEvent e(wxEVT_CHAR_HOOK);e.m_keyCode=key;
            e.SetControlDown(modifiers&wxMOD_CONTROL);e.SetShiftDown(modifiers&wxMOD_SHIFT);e.SetAltDown(modifiers&wxMOD_ALT);
            e.SetEventObject(script->TextBox());script->TextBox()->GetEventHandler()->ProcessEvent(e);
        };
        press('Z',wxMOD_CONTROL);press('V',wxMOD_CONTROL);press(WXK_DELETE,wxMOD_CONTROL);press(WXK_SPACE,0);press('A',wxMOD_SHIFT);
        EXPECT_TRUE(reached.empty()) << "undo, paste, Ctrl+Delete, Space and typing are the box's";
        press('S',wxMOD_CONTROL);press(WXK_F6,0);press('O',wxMOD_ALT);
        EXPECT_EQ((std::vector<int>{'S',WXK_F6,'O'}),reached) << "save, F6 and Alt keys are the window's";
    }

    // Compared: the file's script can't change, nor the box
    {
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        script->Type("changed");
        EXPECT_EQ("第一句。\n第二句！\n第三句\n",LiveRead(*c).Properties.script_text);
        EXPECT_EQ(u("第一句。\n第二句！\n第三句\n"),script->TextBox()->GetValue());
        EXPECT_FALSE(script->Shown()["note"].empty()) << "says why";
    }
    script->Type("after");
    EXPECT_EQ("after",LiveRead(*c).Properties.script_text);
    EXPECT_TRUE(script->Shown()["note"].empty());

    // Another file: its script in the box
    {
        auto write=c->subtitleAccess->TryWrite();
        LiveWriteForTest(*c).LoadDefault();LiveWriteForTest(*c).Commit("",AssFile::COMMIT_NEW);
    }
    c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::ScriptText,"other");
    EXPECT_EQ("other",script->TextBox()->GetValue());

    // Folded, the strip names it after the pages and opens it
    edge->Open(StylePanel::Page::Style);
    EXPECT_TRUE(panel->IsShown()) << "a page's name: the style panel";
    EXPECT_FALSE(script->IsShown());
    OPT_SET("App/Style Panel/Show")->SetBool(false);
    ASSERT_TRUE(edge->Folded());
    int x=edge->GetClientSize().x-edge->FromDIP(10), script_at=-1, timing_at=-1;
    for(int y=0;y<edge->FromDIP(500);y+=2){int p=edge->PageAt(wxPoint(x,y));if(p==2&&timing_at<0)timing_at=y;if(p==StylePanelEdge::script_name&&script_at<0)script_at=y;}
    ASSERT_GT(timing_at,0); EXPECT_GT(script_at,timing_at);
    edge->OpenScript();
    EXPECT_FALSE(edge->Folded());
    EXPECT_TRUE(script->IsShown());
    EXPECT_FALSE(panel->IsShown());

    // Once a page of the style panel: opened on the script panel instead
    OPT_SET("App/Script Panel/Show")->SetBool(false);
    OPT_SET("App/Style Panel/Page")->SetInt(3);
    auto again=new StylePanel(root,c.get());
    EXPECT_TRUE(OPT_GET("App/Script Panel/Show")->GetBool());
    EXPECT_EQ(StylePanel::Page::Style,again->ShownPage());
    EXPECT_EQ(0,OPT_GET("App/Style Panel/Page")->GetInt());
    again->Destroy();
}

TEST_F(ordinary_edit, style_edit_writes_one_property_for_lines_or_selected_text) {
    using namespace style_edit;
    auto fs=TagNames(Property::Size), b=TagNames(Property::Weight), i=TagNames(Property::Italic);
    // Whole lines: one tag at the start, the others gone, \t and comments kept
    EXPECT_EQ("{\\b1\\fs50}ab",SetForLine("{\\b1\\fs20}a{\\fs30}b",fs,"\\fs50"));
    EXPECT_EQ("{\\b1}abc",SetForLine("abc",b,"\\b1"));
    EXPECT_EQ("abc",SetForLine("{\\fs20}abc",fs,""));
    EXPECT_EQ("{\\t(\\fs80)\\fs50}x",SetForLine("{\\t(\\fs80)\\fs20}x",fs,"\\fs50"));
    EXPECT_EQ("{\\fs50}{note}abc",SetForLine("{note}abc",fs,"\\fs50"));
    EXPECT_EQ("{\\fscx120\\fsp2\\fs50}x",SetForLine("{\\fscx120\\fs+2\\fsp2}x",fs,"\\fs50")) << "\\fsp and \\fscx are other tags";
    EXPECT_EQ("{\\pos(1,2)\\an7}x",SetForLine("{\\pos(1,2)\\a5}x{\\an3}",TagNames(Property::Alignment),"\\an7"));
    // Selected text: the tag in front, the old value after
    auto r=SetForRange("hello world",0,5,b,"\\b1","\\b0");
    EXPECT_EQ("{\\b1}hello{\\b0} world",r.text); EXPECT_EQ(5,r.start); EXPECT_EQ(10,r.end);
    r=SetForRange("hello world",6,11,b,"\\b1","\\b0");
    EXPECT_EQ("hello {\\b1}world",r.text) << "nothing follows"; EXPECT_EQ(11,r.start); EXPECT_EQ(16,r.end);
    std::string t="a{\\b1}bc{\\b0}d";
    r=SetForRange(t,6,8,i,"\\i1","\\i0");
    EXPECT_EQ("a{\\b1\\i1}bc{\\b0\\i0}d",r.text); EXPECT_EQ("bc",r.text.substr(r.start,r.end-r.start));
    t="ab{\\fs10}cd{\\fs20}ef";
    r=SetForRange(t,0,static_cast<int>(t.find("e")),fs,"\\fs50","\\fs10");
    EXPECT_EQ("{\\fs50}abcd{\\fs20}ef",r.text) << "inside goes, the tag at the end already restores";
    r=SetForRange("{\\fs30}abc",7,8,fs,"\\fs50","\\fs30");
    EXPECT_EQ("{\\fs50}a{\\fs30}bc",r.text) << "the first character: into the block before it";
    EXPECT_EQ("a",r.text.substr(r.start,r.end-r.start));

    // Through the editing gate, with undo
    auto line=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\fs20}Hello world";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    double style_size=LiveRead(*c).GetStyle("Default")->fontsize;
    c->textSelectionController->SetSelection(0,0);
    int id=Apply(c.get(),Property::Size,[](StyleLook &l){ l.size=50; });
    EXPECT_NE(-1,id); EXPECT_EQ("{\\fs50}Hello world",line());
    // Dragging: every step amends the same undo step
    id=Apply(c.get(),Property::Size,[](StyleLook &l){ l.size=60; },id);
    id=Apply(c.get(),Property::Size,[&](StyleLook &l){ l.size=style_size; },id);
    EXPECT_EQ("Hello world",line()) << "the style's own size needs no tag";
    c->subsController->Undo();
    EXPECT_EQ("{\\fs20}Hello world",line());
    // Selected text, then the selection still covers it
    std::string text=line(); int w=static_cast<int>(text.find("world"));
    c->textSelectionController->SetSelection(w,w+5);
    Apply(c.get(),Property::Italic,[](StyleLook &l){ l.italic=true; });
    EXPECT_EQ("{\\fs20}Hello {\\i1}world",line());
    text=line();
    EXPECT_EQ("world",text.substr(c->textSelectionController->GetSelectionStart(),c->textSelectionController->GetSelectionEnd()-c->textSelectionController->GetSelectionStart()));
    // Alignment is for the whole line even then
    Apply(c.get(),Property::Alignment,[](StyleLook &l){ l.alignment=8; });
    EXPECT_EQ("{\\fs20\\an8}Hello {\\i1}world",line());
    c->textSelectionController->SetSelection(0,0);
    // Several lines, each from its own value
    int second;
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue();row->Text="{\\an1}two";second=row->Id;LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    int first=LiveRead(*c).Events.front().Id;
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    Apply(c.get(),Property::Alignment,[](StyleLook &l){ l.alignment=(l.alignment-1)/3*3+3; });
    EXPECT_EQ("{\\fs20\\an9}Hello {\\i1}world",line());
    EXPECT_EQ("{\\an3}two",std::string(LiveRead(*c).Events.back().Text.get()));
    // Nothing changes: no undo step
    EXPECT_EQ(-1,Apply(c.get(),Property::Alignment,[](StyleLook &l){ l.alignment=(l.alignment-1)/3*3+3; }));
    c->subsController->Undo();
    EXPECT_EQ("{\\an1}two",std::string(LiveRead(*c).Events.back().Text.get())) << "undo goes to before the last real change";
}

TEST_F(ordinary_edit, style_panel_shows_the_selected_text_lines_and_where_they_differ) {
    std::vector<int> ids;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto style=new AssStyle();style->name="Look";style->font="Look Sans";style->fontsize=40;style->outline_w=2;style->shadow_w=0;
     style->primary=agi::Color(255,255,255);style->outline=agi::Color(0,0,0);style->shadow=agi::Color(0,0,0,0x80);style->alignment=2;
     file.Styles.push_back(*style);
     for(auto text:{"{\\b1}one","{\\an8\\fs60}two {\\i1}more","{\\bord0\\xshad2\\yshad2}three"}){
        auto row=new AssDialogue();row->Style="Look";row->Text=text;ids.push_back(row->Id);file.Events.push_back(*row);}
     file.Commit("fixture",AssFile::COMMIT_NEW);}
    auto panel=new StylePanel(window,c.get());
    auto shown=[&] { panel->UpdateNow(); return panel->Shown(); };

    c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0]);
    auto s=shown();
    EXPECT_EQ("The current line",s["scope"]);
    EXPECT_EQ("Look",s["style"]); EXPECT_EQ("Look Sans",s["font"]); EXPECT_EQ("40",s["size"]); EXPECT_EQ("Bold",s["weight"]);
    EXPECT_EQ("on",s["bold"]); EXPECT_EQ("off",s["italic"]);
    EXPECT_EQ("on",s["align_centre"]); EXPECT_EQ("on",s["align_bottom"]); EXPECT_EQ("off",s["align_top"]);
    EXPECT_EQ("on #FFFFFF",s["fill"]); EXPECT_EQ("100",s["fill_opacity"]);
    EXPECT_EQ("on #000000",s["outline"]); EXPECT_EQ("2",s["outline_width"]);
    EXPECT_EQ("off #000000",s["background"]);
    EXPECT_EQ("off #000000",s["shadow"]); EXPECT_EQ("50",s["shadow_opacity"]);
    EXPECT_EQ("0",s["shadow_distance"]);

    // Several lines: what they share, and "—" where they differ
    c->selectionController->SetSelectionAndActiveIDs({ids[0],ids[1],ids[2]},ids[0]);
    s=shown();
    EXPECT_EQ("3 selected lines",s["scope"]);
    EXPECT_EQ("Look Sans",s["font"]); EXPECT_EQ(wxS("—"),s["size"]); EXPECT_EQ(wxS("—"),s["weight"]);
    EXPECT_EQ("mixed",s["bold"]); EXPECT_EQ("off",s["italic"]);
    EXPECT_EQ("mixed",s["align_top"]); EXPECT_EQ("mixed",s["align_bottom"]); EXPECT_EQ("on",s["align_centre"]);
    EXPECT_EQ("mixed #000000",s["outline"]); EXPECT_EQ(wxS("—"),s["outline_width"]);
    EXPECT_EQ("mixed #000000",s["shadow"]); EXPECT_EQ("50",s["shadow_opacity"]);
    EXPECT_EQ(wxS("—"),s["shadow_distance"]);

    // Text selected in the editor: how that text looks
    c->selectionController->SetSelectionAndActiveIDs({ids[1]},ids[1]);
    std::string two="{\\an8\\fs60}two {\\i1}more";
    int more=static_cast<int>(two.find("more"));
    c->textSelectionController->SetSelection(more,more+4);
    s=shown();
    EXPECT_EQ("The selected text",s["scope"]); EXPECT_EQ("on",s["italic"]); EXPECT_EQ("Italic",s["weight"]); EXPECT_EQ("60",s["size"]);
    EXPECT_EQ("on",s["align_top"]);
    c->textSelectionController->SetSelection(0,3);
    s=shown();
    EXPECT_EQ("The current line",s["scope"]) << "a selection inside the tags is no text";
    int word=static_cast<int>(two.find("two"));
    c->textSelectionController->SetSelection(word,word+3);
    s=shown();
    EXPECT_EQ("The selected text",s["scope"]); EXPECT_EQ("off",s["italic"]);
    c->textSelectionController->SetSelection(0,0);

    // Shadow distance and angle: 0° up, clockwise
    c->selectionController->SetSelectionAndActiveIDs({ids[2]},ids[2]);
    s=shown();
    EXPECT_EQ("2.83",s["shadow_distance"]); EXPECT_EQ("135",s["shadow_angle"]);
    EXPECT_EQ("on #000000",s["shadow"]); EXPECT_EQ("off #000000",s["outline"]); EXPECT_EQ("0",s["outline_width"]);

    // An opaque box is the style's background, and then there is no outline
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).GetStyle("Look")->borderstyle=3;LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_STYLES);}
    c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0]);
    s=shown();
    EXPECT_EQ("on #000000",s["background"]); EXPECT_EQ("off #000000",s["outline"]);

    // Edits show without asking, once the events have run
    {auto write=c->subtitleAccess->TryWrite();
     for(auto &line:LiveWriteForTest(*c).Events) if(line.Id==ids[0]) line.Text="{\\fs33}one";
     LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    wxTheApp->ProcessPendingEvents();
    EXPECT_EQ("33",panel->Shown()["size"]);

    c->selectionController->SetSelectionAndActiveIDs({},0);
    s=shown();
    EXPECT_EQ("No line selected",s["scope"]); EXPECT_EQ(wxS("—"),s["font"]); EXPECT_EQ("off",s["bold"]);

    // Sections fold away
    EXPECT_TRUE(panel->IsSectionShown("appearance"));
    panel->ShowSection("appearance",false);
    EXPECT_FALSE(panel->IsSectionShown("appearance"));
    panel->ShowSection("appearance",true);
    EXPECT_TRUE(panel->IsSectionShown("appearance"));
    panel->Destroy();
}

TEST_F(ordinary_edit, style_panel_is_shown_and_hidden_from_the_view_menu) {
    auto command=cmd::get("app/toggle/style_panel");
    ASSERT_NE(nullptr,command);
    bool before=OPT_GET("App/Style Panel/Show")->GetBool();
    EXPECT_EQ(before?_("Hide Style Panel"):_("Show Style Panel"),command->StrMenu(c.get()));
    (*command)(c.get());
    EXPECT_NE(before,OPT_GET("App/Style Panel/Show")->GetBool());
    (*command)(c.get());
    EXPECT_EQ(before,OPT_GET("App/Style Panel/Show")->GetBool());
    std::ifstream menu(AEGISUB_SOURCE_ROOT "/src/libresrc/default_menu.json");
    std::string json((std::istreambuf_iterator<char>(menu)),std::istreambuf_iterator<char>());
    EXPECT_NE(std::string::npos,json.find("\"app/toggle/style_panel\""));
}

class audio_edit : public ordinary_edit {
protected:
    agi::ass::Karaoke karaoke;
    agi::signal::Connection file_slot;
    AudioTimingController* Timing() { return c->audioController->GetTimingController(); }
    void SetUp() override {
        ordinary_edit::SetUp();
        OPT_SET("Audio/Auto/Commit")->SetBool(false);
        OPT_SET("Audio/Snap/Enable")->SetBool(false);
        OPT_SET("Audio/Auto/Scroll")->SetBool(false);
        OPT_SET("Audio/Inactive Lines Display Mode")->SetInt(0);
        OPT_SET("Timing/Link Adjacent Lines")->SetBool(false);
        auto write=c->subtitleAccess->TryWrite();
        auto& row=LiveWriteForTest(*c).Events.front();row.Start=1001;row.End=2002;
        row.Text="{\\k50}ab{\\k50}cd";
        LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_NEW);
        c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    }
    void TearDown() override {
        c->audioController->SetTimingController(nullptr);
        ordinary_edit::TearDown();
    }
    void Karaoke() {
        SetKaraokeLine(karaoke,&LiveRead(*c).Events.front(),true);
        c->audioController->SetTimingController(CreateKaraokeTimingController(c.get(),&karaoke,file_slot));
    }
    AudioDisplay* Display() {
        auto display=new AudioDisplay(window,c->audioController.get(),c.get());
        c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
        display->SetSize(0,0,900,160);
        RealizeForCapture(display);
        return display;
    }
    void Mouse(AudioDisplay* display,wxEventType type,int ms,bool down=false) {
        wxMouseEvent event(type);event.SetLeftDown(down);
        event.SetPosition({display->RelativeXFromTime(ms),80});event.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(event);
    }
};
TEST_F(audio_edit, studio_waveform_shades_only_selected_ranges_and_draws_a_db_scale) {
    // The dB scale: 0 dB at the edge, -6 dB at half the height, linear amplitude.
    auto levels = AudioWaveformRenderer::DbGrid(200, 1.f, 16);
    ASSERT_GE(levels.size(), 3u);
    EXPECT_EQ(0, levels[0].db);
    EXPECT_EQ(100, levels[0].offset);
    auto minus6 = std::find_if(levels.begin(), levels.end(), [](auto const& l) { return l.db == -6; });
    ASSERT_NE(levels.end(), minus6);
    EXPECT_EQ(50, minus6->offset);
    for (size_t i = 1; i < levels.size(); ++i) {
        EXPECT_LT(levels[i].db, levels[i - 1].db);
        EXPECT_GE(levels[i - 1].offset - levels[i].offset, 16);
    }
    EXPECT_GE(levels.back().offset, 16);
    // Zoomed in vertically, 0 dB is off the display.
    auto zoomed = AudioWaveformRenderer::DbGrid(200, 4.f, 16);
    ASSERT_FALSE(zoomed.empty());
    EXPECT_LT(zoomed.front().db, 0);
    EXPECT_TRUE(AudioWaveformRenderer::DbGrid(20, 1.f, 16).empty());

    c->project->LoadAudio("dummy-audio:noise?sr=44100&bd=16&ch=1&ln=441000");
    // Spread of lightness over the waveform's colours (not the black
    // background or the dark scale lines): one shade, or peaks and averages.
    // Lines are antialiased on some platforms, so blends of a shade appear too.
    auto spread = [&](std::string const& scheme, AudioRenderingStyle style) {
        AudioWaveformRenderer renderer(scheme);
        renderer.SetProvider(c->project->AudioProvider());
        renderer.SetMillisecondsPerPixel(10);
        renderer.SetAmplitudeScale(0.5f);
        wxBitmap bmp(32, 200, 24);
        renderer.Render(bmp, 0, style);
        auto image = bmp.ConvertToImage();
        int low = 255, high = 0;
        for (int y = 0; y < image.GetHeight(); ++y)
            for (int x = 0; x < image.GetWidth(); ++x) {
                int r = image.GetRed(x, y), g = image.GetGreen(x, y), b = image.GetBlue(x, y);
                int lightness = (std::max({r, g, b}) + std::min({r, g, b})) / 2;
                if (lightness < 80) continue;
                low = std::min(low, lightness);
                high = std::max(high, lightness);
            }
        return high < low ? 0 : high - low;
    };
    OPT_SET("Audio/Display/Waveform Style")->SetInt(0);
    OPT_SET("Audio/Display/Draw/dB Scale")->SetBool(true);
    EXPECT_LT(spread("Studio", AudioStyle_Normal), 40) << "one shade outside the selection";
    EXPECT_LT(spread("Studio", AudioStyle_Inactive), 40);
    EXPECT_GT(spread("Studio", AudioStyle_Primary), 80) << "peaks and averages in the selection";
    EXPECT_GT(spread("Studio", AudioStyle_Selected), 80);
    // The earlier schemes keep following the waveform style.
    EXPECT_LT(spread("Green", AudioStyle_Primary), 40);

    // Seconds lines: like the dB scale with Studio, unless the user chose a colour.
    {
        OPT_SET("Audio/Spectrum")->SetBool(false);
        OPT_SET("Audio/Display/Draw/Seconds")->SetBool(true);
        OPT_SET("Colour/Audio Display/Seconds Line")->Reset();
        OPT_SET("Colour/Audio Display/Waveform")->SetString("Studio");
        SecondsMarkerProvider seconds;
        AudioMarkerVector markers;
        seconds.GetMarkers(TimeRange(0, 3000), markers);
        ASSERT_FALSE(markers.empty());
        auto colour = [&] { return markers[0]->GetStyle().GetColour(); };
        EXPECT_EQ(AudioWaveformRenderer::DbGridColour(), colour());
        OPT_SET("Colour/Audio Display/Waveform")->SetString("Green");
        EXPECT_EQ(to_wx(OPT_GET("Colour/Audio Display/Seconds Line")->GetColor()), colour());
        OPT_SET("Colour/Audio Display/Waveform")->SetString("Studio");
        OPT_SET("Audio/Spectrum")->SetBool(true);
        EXPECT_NE(AudioWaveformRenderer::DbGridColour(), colour()) << "the spectrum keeps its colours";
        OPT_SET("Audio/Spectrum")->SetBool(false);
        OPT_SET("Colour/Audio Display/Seconds Line")->SetColor(agi::Color(200, 10, 10));
        EXPECT_EQ(wxColour(200, 10, 10), colour()) << "a colour the user chose wins";
        OPT_SET("Colour/Audio Display/Seconds Line")->Reset();
    }

    // A configuration saved before the scheme existed gets it once.
    OPT_SET("Audio/Colour Schemes")->SetListString({"Green", "Icy Blue"});
    OPT_SET("Colour/Audio Display/Waveform")->SetString("Green");
    MigrateAudioColourSchemes();
    EXPECT_EQ("Studio", OPT_GET("Audio/Colour Schemes")->GetListString().at(0));
    EXPECT_EQ("Studio", OPT_GET("Colour/Audio Display/Waveform")->GetString());
    OPT_SET("Colour/Audio Display/Waveform")->SetString("Green");
    MigrateAudioColourSchemes();
    EXPECT_EQ("Green", OPT_GET("Colour/Audio Display/Waveform")->GetString()) << "a later choice stays";
    EXPECT_EQ(3u, OPT_GET("Audio/Colour Schemes")->GetListString().size());

    // The first Studio's green ruler and scrollbar turn grey; a colour the
    // user picked stays.
    OPT_SET("Colour/Schemes/Studio/UI/Light")->SetColor(agi::Color(64, 220, 152));
    OPT_SET("Colour/Schemes/Studio/UI/Dark")->SetColor(agi::Color(0, 0, 0));
    OPT_SET("Colour/Schemes/Studio/UI Focused/Light")->SetColor(agi::Color(1, 2, 3));
    MigrateAudioColourSchemes();
    EXPECT_EQ(agi::Color(150, 150, 150), OPT_GET("Colour/Schemes/Studio/UI/Light")->GetColor());
    EXPECT_EQ(agi::Color(38, 38, 38), OPT_GET("Colour/Schemes/Studio/UI/Dark")->GetColor());
    EXPECT_EQ(agi::Color(1, 2, 3), OPT_GET("Colour/Schemes/Studio/UI Focused/Light")->GetColor());
    auto grey = OPT_GET("Colour/Schemes/Studio/UI/Light")->GetColor();
    EXPECT_TRUE(grey.r == grey.g && grey.g == grey.b) << "the ruler's ticks and seconds are grey";
}

TEST_F(audio_edit, db_scale_values_never_overlap) {
    // Any height, any vertical zoom, a large font (high display scale): the
    // values sit on their lines, inside the display and apart.
    OPT_SET("Audio/Spectrum")->SetBool(false);
    OPT_SET("Audio/Display/Draw/dB Scale")->SetBool(true);
    auto display=Display();
    wxClientDC dc(display);
    for (int point : {9, 16}) {
        wxFont font = display->GetFont(); font.SetPointSize(point); display->SetFont(font);
        dc.SetFont(font.Scaled(0.8f));
        for (int height = 60; height <= 400; height += 7) {
            for (float scale : {0.5f, 1.f, 1.7f, 4.f}) {
                display->SetSize(0,0,900,height+40);
                display->SetAmplitudeScale(scale);
                auto labels = display->DbLabelRects(dc);
                int top = display->GetClientSize().y; int bottom = 0;
                for (size_t i = 0; i < labels.size(); ++i) {
                    for (size_t j = i + 1; j < labels.size(); ++j)
                        EXPECT_FALSE(labels[i].rect.Intersects(labels[j].rect)) << point << "pt, height " << height << ", scale " << scale << ": " << labels[i].text << " and " << labels[j].text;
                    top = std::min(top, labels[i].rect.y); bottom = std::max(bottom, labels[i].rect.GetBottom());
                }
                if (!labels.empty()) EXPECT_LT(bottom, display->GetClientSize().y - 10) << "above the scrollbar";
            }
        }
        // Lines are at least a value apart, so the lines and values agree.
        EXPECT_GE(display->DbLabelGap(), dc.GetCharHeight());
    }
}

TEST_F(audio_edit, dialogue_manual_drag_cancel_never_writes_or_adds_history) {
    auto description=c->subsController->GetUndoDescription();
    auto token=Timing()->MarkerLifetime();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);ASSERT_EQ(1u,markers.size());
    Timing()->OnMarkerDrag(markers,1103,0);
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->EndDrag(true);
    EXPECT_TRUE(token.expired());
    EXPECT_EQ(1001,Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, dialogue_manual_commit_and_history_preserve_exact_milliseconds_and_identity) {
    auto id=LiveRead(*c).Events.front().Id;
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->subsController->Redo();EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    EXPECT_EQ(id,c->selectionController->ReadActiveLine()->Id);
}
TEST_F(audio_edit, continuous_autocommit_drag_keeps_marker_borrow_and_coalesces_undo) {
    OPT_SET("Audio/Auto/Commit")->SetBool(true);
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);ASSERT_FALSE(markers.empty());
    auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1051,0);Timing()->OnMarkerDrag(markers,1101,0);
    EXPECT_FALSE(token.expired());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    OPT_SET("Audio/Auto/Commit")->SetBool(false); // preference changed during a completed auto drag
    Timing()->EndDrag(false);EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1101,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_EQ(1101,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, selection_change_invalidates_markers_and_releases_pending_task) {
    AssDialogue* next;
    {auto write=c->subtitleAccess->TryWrite();next=new AssDialogue;
     next->Start=3001;next->End=4002;LiveWriteForTest(*c).Events.push_back(*next);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_NEW);}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1111,0);
    c->selectionController->SetSelectionAndActiveIDs({next->Id}, next->Id);
    EXPECT_TRUE(token.expired());EXPECT_EQ(3001,Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, multiple_selected_rows_share_pending_lifetime_and_undo) {
    AssDialogue* next;
    {auto write=c->subtitleAccess->TryWrite();next=new AssDialogue;
     next->Start=3001;next->End=4002;LiveWriteForTest(*c).Events.push_back(*next);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_NEW);
     c->selectionController->SetSelectedIDs({(LiveRead(*c).Events.front()).Id,next->Id});}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnRightClick(4503,false,0,0);ASSERT_EQ(2u,markers.size());
    Timing()->EndDrag(false);Timing()->Commit();
    for(auto const& row:LiveRead(*c).Events)EXPECT_EQ(4503,row.End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ(4002,LiveRead(*c).Events.back().End.GetMilliseconds());
    EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
}
TEST_F(audio_edit, comparison_rejects_dialogue_and_karaoke_cache_changes) {
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    auto original=Timing()->GetActiveLineRange();
    EXPECT_FALSE(Timing()->BeginDrag());Timing()->ModifyStart(1);Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(original.begin(),Timing()->GetActiveLineRange().begin());
    comparison.reset();Karaoke();
    comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    auto text=karaoke.GetText();original=Timing()->GetActiveLineRange();
    EXPECT_FALSE(Timing()->BeginDrag());Timing()->AddLeadIn();Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_EQ(text,karaoke.GetText());EXPECT_EQ(original.begin(),Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, karaoke_lead_is_staged_cancelled_then_committed_with_undo) {
    Karaoke();auto text=LiveRead(*c).Events.front().Text.get();
    auto lead=OPT_GET("Audio/Lead/IN")->GetInt();
    Timing()->AddLeadIn();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->Revert();EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(1001-lead,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ(1001-lead,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, karaoke_drag_and_controller_switch_cancel_pending_cache) {
    Karaoke();auto text=LiveRead(*c).Events.front().Text.get();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1500,false,false,10,0);ASSERT_EQ(1u,markers.size());
    auto token=Timing()->MarkerLifetime();Timing()->OnMarkerDrag(markers,1600,0);
    EXPECT_NE(text,karaoke.GetText());EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    EXPECT_TRUE(token.expired());EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, real_wx_audio_drag_release_and_destroyed_controller_borrow) {
    auto display=Display();
    Mouse(display,wxEVT_LEFT_DOWN,1001,true);EXPECT_TRUE(display->HasCapture());
    Mouse(display,wxEVT_MOTION,1200,true);
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(display,wxEVT_LEFT_UP,1200);
    Timing()->Commit();EXPECT_GT(LiveRead(*c).Events.front().Start.GetMilliseconds(),1100);
    Mouse(display,wxEVT_LEFT_DOWN,1200,true);
    auto token=Timing()->MarkerLifetime();
    c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    EXPECT_TRUE(token.expired());EXPECT_FALSE(display->HasCapture());
    Mouse(display,wxEVT_MOTION,1400,true); // no callback into the deleted controller
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->project->CloseAudio();
}
TEST_F(audio_edit, real_wx_display_destruction_discards_uncommitted_drag) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);delete display;
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());c->project->CloseAudio();
}
TEST_F(audio_edit, real_wx_drag_exception_releases_task_and_capture) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    agi::signal::Connection observer=Timing()->AddMarkerMovedListener([] {throw std::runtime_error("marker observer");});
    EXPECT_THROW(Mouse(display,wxEVT_MOTION,1200,true),std::runtime_error);
    EXPECT_FALSE(display->HasCapture());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    observer.Disconnect();c->project->CloseAudio();
}
TEST_F(audio_edit, stale_revision_missing_target_and_document_change_never_write_back) {
    AudioEditSession edit(c.get());ASSERT_TRUE(edit.Begin());
    auto row=LiveRead(*c).Events.front(); // independent AssDialogue copy
    {auto scope=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Start=1002;LiveWriteForTest(*c).Commit("other",AssFile::COMMIT_DIAG_TIME);}
    EXPECT_FALSE(edit.Begin());
    EXPECT_EQ(1002,LiveRead(*c).Events.front().Start.GetMilliseconds());
    edit.Refresh();ASSERT_TRUE(edit.Begin());
    row.Id+=100000;
    EXPECT_THROW(edit.Update({row}),std::invalid_argument);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    edit.Refresh();c->audioController->SetTimingController(nullptr);c->subtitleEditGate.Close();EXPECT_FALSE(edit.Begin());
}
TEST_F(audio_edit, outer_time_command_scope_is_reused_without_spurious_stale_rejection) {
    (*cmd::get("time/lead/in"))(c.get());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_LT(Timing()->GetActiveLineRange().begin(),1001);
    Timing()->Commit();EXPECT_LT(LiveRead(*c).Events.front().Start.GetMilliseconds(),1001);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, real_karaoke_split_buttons_cancel_apply_and_rebuild_after_history) {
    c->audioBox=new AudioBox(window,c.get());
    c->audioBox->SetSize(900,260);
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    c->karaoke->SetEnabled(true);
    wxWindow* panel=nullptr;wxWindow* accept=nullptr;wxWindow* cancel=nullptr;
    for(auto child:c->karaoke->GetChildren()) {
        if(dynamic_cast<wxPanel*>(child))panel=child;
        if(child->GetToolTipText()=="Commit splits")accept=child;
        if(child->GetToolTipText()=="Discard all uncommitted splits")cancel=child;
    }
    ASSERT_NE(nullptr,panel);ASSERT_NE(nullptr,accept);ASSERT_NE(nullptr,cancel);
    panel->SetSize(0,0,600,35);
    auto click=[&] {wxMouseEvent event(wxEVT_LEFT_DOWN);event.SetPosition({25,10});event.SetEventObject(panel);panel->GetEventHandler()->ProcessEvent(event);};
    auto text=LiveRead(*c).Events.front().Text.get();
    click();EXPECT_TRUE(cancel->IsEnabled());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    button(cancel,cancel->GetId());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    auto token=Timing()->MarkerLifetime();
    click();button(accept,accept->GetId());
    EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(token.expired());
    auto changed=LiveRead(*c).Events.front().Text.get();
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    click();button(accept,accept->GetId());
    EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
    comparison.reset();c->project->CloseAudio();
}
TEST_F(audio_edit, audio_source_close_invalidates_active_drag_and_releases_registration) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);auto token=Timing()->MarkerLifetime();
    c->project->CloseAudio();
    EXPECT_TRUE(token.expired());EXPECT_FALSE(display->HasCapture());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, linked_neighbor_is_authorized_and_restored_by_undo) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue;
     other->Start=2002;other->End=3003;LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_DIAG_ADDREM);}
    OPT_SET("Timing/Link Adjacent Lines")->SetBool(true);
    Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_EQ(2012,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ(2012,LiveRead(*c).Events.back().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(2002,LiveRead(*c).Events.back().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    auto session=c->subtitleAccess->TryBeginInteraction({LiveRead(*c).Events.front().Id});ASSERT_TRUE(session);
    // C1-C1c-13 (audit C6): the refused link is validated before the row
    // write, so the requested value is not left behind uncommitted.
    AssDialogueBase moved=LiveRead(*c).Events.front();moved.End=2050;
    EXPECT_THROW(session->UpdateRowsLinkingAdjacent({moved},{moved.Id,false,2002,100,true}),std::logic_error);
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.back().Start.GetMilliseconds());
    EXPECT_FALSE(session->Current());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, real_wx_capture_lost_cancels_pending_drag) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);display->ReleaseMouse();
    wxMouseCaptureLostEvent lost(display->GetId());
    display->GetEventHandler()->ProcessEvent(lost);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->project->CloseAudio();
}
TEST_F(audio_edit, throwing_cancellation_observer_cannot_keep_destroyed_cache_borrower) {
    Karaoke();ASSERT_TRUE(Timing()->BeginDrag());
    auto token=Timing()->MarkerLifetime();
    agi::signal::Connection throwing=Timing()->AddMarkerMovedListener([] {throw std::runtime_error("cancel");});
    EXPECT_THROW(c->audioController->SetTimingController(nullptr),std::runtime_error);
    EXPECT_EQ(nullptr,Timing());EXPECT_TRUE(token.expired());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, karaoke_continuous_auto_drag_coalesces_text_undo_without_rounding_row_times) {
    OPT_SET("Audio/Auto/Commit")->SetBool(true);Karaoke();
    auto text=LiveRead(*c).Events.front().Text.get();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1500,false,false,10,0);ASSERT_EQ(1u,markers.size());
    auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1600,0);Timing()->OnMarkerDrag(markers,1700,0);
    EXPECT_FALSE(token.expired());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->EndDrag(false);EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    auto changed=LiveRead(*c).Events.front().Text.get();
    c->subsController->Undo();EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
}
TEST_F(audio_edit, inactive_neighbor_cache_survives_undo_node_replacement) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue;
     other->Start=3001;other->End=4002;LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_DIAG_ADDREM);}
    OPT_SET("Audio/Inactive Lines Display Mode")->SetInt(2);
    Timing()->ModifyStart(1);Timing()->Commit();
    c->subsController->Undo();c->subsController->Redo();
    EXPECT_EQ(1011,Timing()->GetActiveLineRange().begin());
    AudioMarkerVector markers;Timing()->GetMarkers(TimeRange(0,5000),markers);
    EXPECT_GE(markers.size(),4u);
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1021,LiveRead(*c).Events.front().Start.GetMilliseconds());
}

TEST_F(audio_edit, no_op_navigation_and_boundary_commands_do_not_stale_audio_cache) {
    {auto no_op=c->subtitleAccess->TryWrite();ASSERT_TRUE(no_op);}
    (*cmd::get("time/prev"))(c.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
    Karaoke();auto text=LiveRead(*c).Events.front().Text.get();
    (*cmd::get("time/prev"))(c.get());
    (*cmd::get("time/start/increase"))(c.get()); // first syllable: no movable left boundary
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    (*cmd::get("time/next"))(c.get());
    (*cmd::get("time/length/increase"))(c.get()); // last syllable: no movable right boundary
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    (*cmd::get("time/prev"))(c.get());
    Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, stale_pending_audio_task_stays_rejected_until_explicit_rebuild) {
    Timing()->ModifyStart(1);
    {auto foreign=c->subtitleEditGate.TryBeginWrite();ASSERT_TRUE(foreign);}
    Timing()->Commit();Timing()->Commit();
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Timing()->Revert();Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,LiveRead(*c).Events.front().Start.GetMilliseconds());
}

TEST_F(audio_edit, karaoke_one_millisecond_lead_is_not_lost_to_tag_rounding) {
    auto lead=OPT_GET("Audio/Lead/IN")->GetInt();
    OPT_SET("Audio/Lead/IN")->SetInt(1);Karaoke();
    Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(1000,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,LiveRead(*c).Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());
    OPT_SET("Audio/Lead/IN")->SetInt(lead);
}

TEST_F(audio_edit, cancelling_an_untimed_row_cannot_be_committed_later) {
    {auto scope=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Start=0;LiveWriteForTest(*c).Events.front().End=0;
     LiveWriteForTest(*c).Commit("audio fixture",AssFile::COMMIT_DIAG_TIME);}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnRightClick(1500,false,0,0);ASSERT_FALSE(markers.empty());
    Timing()->EndDrag(true);Timing()->Commit();
    EXPECT_EQ(0,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_EQ(0,LiveRead(*c).Events.front().End.GetMilliseconds());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

class visual_edit : public ordinary_edit {
protected:
    VideoDisplay* display=nullptr;
    wxToolBar* toolbar=nullptr;
    void SetUp() override {
        ordinary_edit::SetUp();
        OPT_SET("Video/Subtitle Sync")->SetBool(false);
        c->project->LoadVideo("?dummy:25:250:640:480:64:64:64:");
        ASSERT_NE(nullptr,c->project->VideoProvider());
        {auto scope=c->subtitleAccess->TryWrite();
         auto& row=LiveWriteForTest(*c).Events.front();row.Start=0;row.End=5000;
         row.Text="{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail";
         LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_FULL);}
        toolbar=new wxToolBar(window,wxID_ANY);
        auto zoom=new VideoZoomControl(window);
        display=new VideoDisplay(toolbar,false,zoom,window,c.get());
        display->SetSize(640,480);
        RealizeForCapture(display);
        FrameData(); // finish renderer initialization with the main dispatch queue running
    }
    void TearDown() override {
        if(display) {delete display;display=nullptr;c->videoDisplay=nullptr;}
        c->project->CloseVideo();
        ordinary_edit::TearDown();
    }
    template<class Tool> Tool* Install() {
        auto tool=std::make_unique<Tool>(display,c.get());auto ptr=tool.get();
        display->SetTool(std::move(tool));
        ptr->SetCanvasSize(640,480);ptr->SetDisplayArea(0,0,640,480);
        return ptr;
    }
    void Mouse(wxEventType type,int x,int y,bool down=false,bool ctrl=false) {
        wxMouseEvent event(type);event.SetLeftDown(down);event.SetControlDown(ctrl);
        event.SetPosition({x,y});event.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(event);
    }
    void Drag(int x,int y,int x2,int y2) {
        Mouse(wxEVT_LEFT_DOWN,x,y,true);Mouse(wxEVT_MOTION,x2,y2,true);Mouse(wxEVT_LEFT_UP,x2,y2);
    }
    std::string Text() { return LiveRead(*c).Events.front().Text.get(); }
    std::string Effect() { return LiveRead(*c).Events.front().Effect.get(); }
    void SetEffect(std::string const& effect) {
        auto scope=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Effect=effect;
        LiveWriteForTest(*c).Commit("effect fixture",AssFile::COMMIT_DIAG_META);
    }
    std::vector<unsigned char> FrameData() {
        // Libass font setup may need the main dispatch queue. Keep the normal
        // wx loop responsive rather than synchronously waiting on that worker.
        wxEventLoop loop;
        wxEventLoopActivator activate(&loop);
        std::vector<unsigned char> data;
        std::exception_ptr error;
        auto provider=c->project->VideoProvider();
        std::thread reader([&] {
            try { data=provider->GetFrame(0,0)->data; }
            catch (...) { error=std::current_exception(); }
            window->CallAfter([&] { loop.Exit(); });
        });
        loop.Run();reader.join();
        if(error)std::rethrow_exception(error);
        return data;
    }
};
TEST_F(visual_edit, position_drag_previews_without_model_write_then_commits_once_and_undoes) {
    Install<VisualToolDrag>();auto text=Text();auto id=LiveRead(*c).Events.front().Id;
    auto before=FrameData();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);ASSERT_TRUE(display->HasCapture());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_MOTION,140,160,true);EXPECT_EQ(text,Text());
    auto preview=FrameData();
    EXPECT_NE(before,preview);
    Mouse(wxEVT_LEFT_UP,140,160);
    EXPECT_NE(text,Text());EXPECT_NE(std::string::npos,Text().find("\\pos(140,160)"));
    EXPECT_NE(std::string::npos,Text().find("\\bord3"));EXPECT_NE(std::string::npos,Text().find("中 文 {\\b1} tail"));
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    c->subsController->Undo();EXPECT_EQ(text,Text());EXPECT_EQ("visual fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_NE(text,Text());
}
TEST_F(visual_edit, capture_loss_discards_preview_without_undo_record) {
    Install<VisualToolDrag>();auto text=Text();
    auto before=FrameData();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,200,200,true);
    display->ReleaseMouse();wxMouseCaptureLostEvent lost(display->GetId());display->GetEventHandler()->ProcessEvent(lost);
    EXPECT_EQ(text,Text());EXPECT_EQ("visual fixture",c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(before,FrameData());
}
TEST_F(visual_edit, lost_button_release_cancels_instead_of_committing) {
    Install<VisualToolClip>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,50,50,true);Mouse(wxEVT_MOTION,200,200,true);
    Mouse(wxEVT_MOTION,210,210,false);
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, all_standard_tools_reject_comparison_writes) {
    auto blocked=[&] {
        auto text=Text();auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        Drag(100,100,160,170);Mouse(wxEVT_LEFT_DCLICK,200,220,true);Mouse(wxEVT_LEFT_UP,200,220);
        EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());
    };
    Install<VisualToolCross>();blocked();Install<VisualToolDrag>();blocked();
    Install<VisualToolScale>();blocked();Install<VisualToolRotateXY>();blocked();
    Install<VisualToolRotateZ>();blocked();Install<VisualToolClip>();blocked();
    Install<VisualToolVectorClip>();blocked();Install<VisualToolRectangle>();blocked();
    Install<VisualToolScroll>();blocked();
    EXPECT_EQ(1u,LiveRead(*c).Events.size());
}
TEST(scroll_effect, reads_and_writes_the_fields_as_the_renderers_do) {
    auto up=ParseScrollEffect("Scroll up;100;300;10");ASSERT_TRUE(up);
    EXPECT_EQ(ScrollEffect::Kind::ScrollUp,up->kind);EXPECT_EQ(100,up->y1);EXPECT_EQ(300,up->y2);EXPECT_EQ(10,up->delay);EXPECT_FALSE(up->fade);
    EXPECT_EQ("Scroll up;100;300;10",FormatScrollEffect(*up));
    auto down=ParseScrollEffect("Scroll down;400;50;0;20");ASSERT_TRUE(down);
    EXPECT_EQ(50,down->Top());EXPECT_EQ(400,down->Bottom());EXPECT_EQ(20,*down->fade);
    EXPECT_EQ("Scroll down;400;50;0;20",FormatScrollEffect(*down)) << "the order written is kept";
    auto banner=ParseScrollEffect("Banner;20");ASSERT_TRUE(banner);
    EXPECT_EQ(ScrollEffect::Kind::Banner,banner->kind);EXPECT_FALSE(banner->left_to_right);EXPECT_EQ("Banner;20",FormatScrollEffect(*banner));
    auto ltr=ParseScrollEffect("Banner;5;1;30");ASSERT_TRUE(ltr);
    EXPECT_TRUE(ltr->left_to_right);EXPECT_EQ(30,*ltr->fade);EXPECT_EQ("Banner;5;1;30",FormatScrollEffect(*ltr));
    banner->left_to_right=true;EXPECT_EQ("Banner;20;1",FormatScrollEffect(*banner));
    // Anything else is not one of them, and is left alone
    for(auto other:{"","template line","karaoke","Banner","Scroll up;10;20","scroll up;1;2;3","Banner 5","code once"})
        EXPECT_FALSE(ParseScrollEffect(other)) << other;
    // The delay is whole milliseconds per pixel, at least one
    EXPECT_EQ(1,EffectiveDelay(0));EXPECT_EQ(1,EffectiveDelay(-4));EXPECT_EQ(7,EffectiveDelay(7));
    EXPECT_DOUBLE_EQ(500.,ScrollShift(*up,5000));EXPECT_DOUBLE_EQ(5000.,ScrollShift(*down,5000)) << "a delay of 0 moves as 1";
    EXPECT_EQ(25,DelayForShift(5000,200));EXPECT_EQ(1,DelayForShift(100,5000));
    EXPECT_LT(100000,DelayForShift(5000,0)) << "not moving at all";
}
TEST_F(visual_edit, scroll_area_edges_are_dragged_previewed_then_committed_once) {
    SetEffect("Scroll up;100;300;10");Install<VisualToolScroll>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,320,100,true);ASSERT_TRUE(display->HasCapture());
    Mouse(wxEVT_MOTION,320,150,true);EXPECT_EQ("Scroll up;100;300;10",Effect()) << "previewed, not written";
    Mouse(wxEVT_LEFT_UP,320,150);
    EXPECT_EQ("Scroll up;150;300;10",Effect());EXPECT_EQ(text,Text());
    EXPECT_EQ(_("scroll effect"),c->subsController->GetUndoDescription());
    c->subsController->Undo();EXPECT_EQ("Scroll up;100;300;10",Effect());
    // The other edge
    Install<VisualToolScroll>();Drag(320,300,320,420);EXPECT_EQ("Scroll up;100;420;10",Effect());
}
TEST_F(visual_edit, scroll_end_handle_sets_the_speed) {
    // 5 s at 20 ms a pixel: 250 px, the text's top from 400 up to 150
    SetEffect("Scroll up;100;400;20");Install<VisualToolScroll>();
    Drag(480,150,480,200);
    EXPECT_EQ("Scroll up;100;400;25",Effect()) << "200 px in 5 s";
    // A banner's end is across, at the line's height (\pos(100,100)): its
    // left edge from the right, 640 - 5000 / 20 = 390
    SetEffect("Banner;20");Install<VisualToolScroll>();
    Drag(390,100,140,100);EXPECT_EQ("Banner;10",Effect()) << "500 px in 5 s";
}
TEST_F(visual_edit, scroll_double_click_turns_it_round) {
    SetEffect("Scroll up;100;400;20");Install<VisualToolScroll>();
    Mouse(wxEVT_LEFT_DCLICK,50,50,true);Mouse(wxEVT_LEFT_UP,50,50);
    EXPECT_EQ("Scroll down;100;400;20",Effect());
    SetEffect("Banner;20");Install<VisualToolScroll>();
    Mouse(wxEVT_LEFT_DCLICK,50,50,true);Mouse(wxEVT_LEFT_UP,50,50);
    EXPECT_EQ("Banner;20;1",Effect());
}
TEST_F(visual_edit, scroll_area_is_dragged_out_only_where_the_field_allows) {
    // Nothing yet: an area is made, crossed by the line's end
    SetEffect("");Install<VisualToolScroll>();
    Mouse(wxEVT_LEFT_DOWN,300,250,true);Mouse(wxEVT_LEFT_UP,300,250);
    EXPECT_EQ("",Effect()) << "a click makes nothing";EXPECT_EQ("effect fixture",c->subsController->GetUndoDescription());
    Drag(300,300,300,100);EXPECT_EQ("Scroll up;100;300;25",Effect());
    // Anything else in the field, and a banner, are left alone
    for(auto other:{"template line","Banner;20"}) {
        SetEffect(other);Install<VisualToolScroll>();auto text=Text();
        Drag(300,300,300,100);EXPECT_EQ(other,Effect());EXPECT_EQ(text,Text());
    }
}
TEST_F(visual_edit, scroll_escape_cancels_without_a_record) {
    SetEffect("Scroll up;100;300;10");Install<VisualToolScroll>();
    Mouse(wxEVT_LEFT_DOWN,320,100,true);Mouse(wxEVT_MOTION,320,180,true);
    {wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;esc.SetEventObject(display);display->GetEventHandler()->ProcessEvent(esc);}
    EXPECT_FALSE(display->HasCapture());
    Mouse(wxEVT_LEFT_UP,320,180);
    EXPECT_EQ("Scroll up;100;300;10",Effect());EXPECT_EQ("effect fixture",c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, scroll_edits_the_selected_lines_with_a_scroll_too) {
    int scroll_id,other_id;
    {auto write=c->subtitleAccess->TryWrite();
     LiveWriteForTest(*c).Events.front().Effect="Scroll up;100;300;10";
     auto scroll=new AssDialogue(LiveRead(*c).Events.front());scroll->Effect="Scroll down;0;50;5";LiveWriteForTest(*c).Events.push_back(*scroll);
     auto other=new AssDialogue(LiveRead(*c).Events.front());other->Effect="template line";LiveWriteForTest(*c).Events.push_back(*other);
     scroll_id=scroll->Id;other_id=other->Id;
     LiveWriteForTest(*c).Commit("effect fixture",AssFile::COMMIT_DIAG_ADDREM);
     auto first=LiveRead(*c).Events.front().Id;
     c->selectionController->SetSelectionAndActiveIDs({first,scroll_id,other_id},first);}
    Install<VisualToolScroll>();Drag(320,100,320,150);
    std::map<int,std::string> effects;for(auto const& row:LiveRead(*c).Events)effects[row.Id]=row.Effect.get();
    EXPECT_EQ("Scroll up;150;300;10",Effect());
    EXPECT_EQ("Scroll down;150;50;5",effects[scroll_id]) << "the edge dragged is set for it too";
    EXPECT_EQ("template line",effects[other_id]);
    c->subsController->Undo();
    for(auto const& row:LiveRead(*c).Events)if(row.Id==scroll_id)EXPECT_EQ("Scroll down;0;50;5",row.Effect.get()) << "one step";
}
TEST_F(visual_edit, scale_rotation_and_rectangular_clip_preserve_unedited_tags) {
    auto text=Text();Install<VisualToolScale>();Drag(200,200,240,160);
    EXPECT_NE(text,Text());EXPECT_NE(std::string::npos,Text().find("\\fscx"));EXPECT_NE(std::string::npos,Text().find("\\fscy"));
    Install<VisualToolRotateXY>();Drag(200,200,220,230);
    EXPECT_NE(std::string::npos,Text().find("\\frx"));EXPECT_NE(std::string::npos,Text().find("\\fry"));
    Install<VisualToolRotateZ>();Drag(200,200,250,180);EXPECT_NE(std::string::npos,Text().find("\\frz"));
    Install<VisualToolClip>();Drag(30,40,300,250);EXPECT_NE(std::string::npos,Text().find("\\clip(30,40,300,250)"));
    EXPECT_NE(std::string::npos,Text().find("\\bord3"));EXPECT_NE(std::string::npos,Text().find("中 文 {\\b1} tail"));
}
TEST_F(visual_edit, drawing_finish_inserts_before_source_with_style_and_preserves_identity) {
    Install<VisualToolRectangle>();auto source=LiveRead(*c).Events.front().Id;auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,100,150,true);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_UP,100,150);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());EXPECT_NE(source,LiveRead(*c).Events.front().Id);
    EXPECT_EQ(source,LiveRead(*c).Events.back().Id);EXPECT_EQ(text,LiveRead(*c).Events.back().Text.get());
    EXPECT_NE(nullptr,LiveRead(*c).GetStyle("图形"));EXPECT_EQ("图形",LiveRead(*c).Events.front().Style.get());
    c->subsController->Undo();EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("图形"));
    c->subsController->Redo();EXPECT_EQ(2u,LiveRead(*c).Events.size());
}
TEST_F(visual_edit, vector_clip_drag_and_cancel_rebuild_path_without_losing_inverse) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\iclip(m 50 50 l 200 50 200 200)\\bord3}字幕";LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_TEXT);}
    auto tool=Install<VisualToolVectorClip>();tool->SetSubTool(VCLIP_DRAG);auto text=Text();
    Drag(50,50,70,80);EXPECT_NE(text,Text());EXPECT_NE(std::string::npos,Text().find("\\iclip("));
    auto changed=Text();Mouse(wxEVT_LEFT_DOWN,70,80,true);Mouse(wxEVT_MOTION,90,100,true);
    wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;display->GetEventHandler()->ProcessEvent(esc);
    EXPECT_EQ(changed,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->subsController->Undo();EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, tool_switch_and_window_close_cancel_pending_drag) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,200,200,true);
    Install<VisualToolScale>();EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_DOWN,200,200,true);Mouse(wxEVT_MOTION,250,160,true);
    delete display;display=nullptr;c->videoDisplay=nullptr;
    EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, selection_change_and_delete_cancel_without_writing_other_row) {
    AssDialogue* other;
    {auto write=c->subtitleAccess->TryWrite();other=new AssDialogue;
     other->Text="{\\pos(300,300)}other";LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();auto text=Text();auto other_id=other->Id;
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,150,150,true);
    c->selectionController->SetSelectionAndActiveIDs({other->Id}, other->Id);
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_DOWN,300,300,true);Mouse(wxEVT_MOTION,350,350,true);
    (*cmd::get("edit/line/delete"))(c.get());
    Mouse(wxEVT_LEFT_UP,350,350);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_NE(other_id,LiveRead(*c).Events.front().Id);EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, foreign_generation_rejects_pending_result_and_releases_registration) {
    Install<VisualToolScale>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,200,200,true);Mouse(wxEVT_MOTION,230,180,true);
    {auto foreign=c->subtitleEditGate.TryBeginWrite();ASSERT_TRUE(foreign);}
    Mouse(wxEVT_LEFT_UP,230,180);
    EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Drag(200,200,240,160);EXPECT_NE(text,Text());
}
TEST_F(visual_edit, video_seek_replacement_and_close_cancel_old_previews) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    c->videoController->JumpToFrame(2);EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    c->project->LoadVideo("?dummy:25:200:640:480:96:96:96:");
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    FrameData();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,180,190,true);
    c->project->CloseVideo();EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, continuous_drags_have_separate_undo_records_and_refresh_after_redo) {
    Install<VisualToolDrag>();auto text=Text();
    Drag(100,100,150,150);auto first=Text();Drag(150,150,200,180);auto second=Text();
    EXPECT_NE(text,first);EXPECT_NE(first,second);
    c->subsController->Undo();EXPECT_EQ(first,Text());c->subsController->Undo();EXPECT_EQ(text,Text());
    c->subsController->Redo();EXPECT_EQ(first,Text());c->subsController->Redo();EXPECT_EQ(second,Text());
    Drag(200,180,220,200);EXPECT_NE(second,Text());
}
TEST_F(visual_edit, multirow_position_changes_preserve_metadata_extras_and_binding) {
    int first_id,second_id;std::string group;
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue(LiveRead(*c).Events.front());
     row->Text="{\\pos(300,300)\\blur2}other";LiveWriteForTest(*c).Events.push_back(*row);
     first_id=LiveRead(*c).Events.front().Id;second_id=row->Id;
     auto extra=LiveWriteForTest(*c).AddExtradata("unknown-test","opaque value");
     LiveWriteForTest(*c).Events.front().Actor="actor";LiveWriteForTest(*c).Events.front().Layer=7;LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{extra};
     LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);
     c->selectionController->SetSelectedIDs({(LiveRead(*c).Events.front()).Id,row->Id});BindSelectedSubtitles(c.get());
     group=GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front());}
    auto extras=LiveRead(*c).Extradata;Install<VisualToolDrag>();
    Drag(100,100,130,140);
    EXPECT_EQ(first_id,LiveRead(*c).Events.front().Id);EXPECT_EQ(second_id,LiveRead(*c).Events.back().Id);
    EXPECT_EQ(7,LiveRead(*c).Events.front().Layer);EXPECT_EQ("actor",LiveRead(*c).Events.front().Actor.get());
    EXPECT_EQ(group,GetSubtitleBinding(LiveRead(*c),LiveRead(*c).Events.front()));
    EXPECT_NE(std::string::npos,LiveRead(*c).Events.back().Text.get().find("\\pos(330,340)"));
    ASSERT_EQ(extras.size(),LiveRead(*c).Extradata.size());
    for(size_t i=0;i<extras.size();++i){EXPECT_EQ(extras[i].key,LiveRead(*c).Extradata[i].key);EXPECT_EQ(extras[i].value,LiveRead(*c).Extradata[i].value);}
}
TEST_F(visual_edit, selecting_another_visible_line_with_subtitle_sync_keeps_feature_alive) {
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue;
     row->Start=1000;row->End=5000;row->Text="{\\pos(300,300)}other";LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    c->videoController->JumpToFrame(50);OPT_SET("Video/Subtitle Sync")->SetBool(true);
    Install<VisualToolDrag>();auto first=Text();
    Drag(300,300,340,350);
    EXPECT_EQ(first,Text());EXPECT_NE(std::string::npos,LiveRead(*c).Events.back().Text.get().find("\\pos(340,350)"));
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, vector_tools_insert_convert_remove_and_freehand_use_authorized_completion) {
    auto tool=Install<VisualToolVectorClip>();tool->SetSubTool(VCLIP_LINE);
    Drag(40,40,40,40);Drag(200,50,200,50);Drag(200,200,200,200);
    auto path=Text();EXPECT_NE(std::string::npos,path.find("\\clip("));
    tool->SetSubTool(VCLIP_CONVERT);Mouse(wxEVT_LEFT_DOWN,120,45,true);Mouse(wxEVT_LEFT_UP,120,45);
    EXPECT_NE(path,Text());auto converted=Text();
    tool->SetSubTool(VCLIP_INSERT);Mouse(wxEVT_LEFT_DOWN,120,45,true);Mouse(wxEVT_LEFT_UP,120,45);
    EXPECT_NE(converted,Text());
    tool->SetSubTool(VCLIP_REMOVE);Mouse(wxEVT_LEFT_DOWN,40,40,true);Mouse(wxEVT_LEFT_UP,40,40);
    tool->SetSubTool(VCLIP_FREEHAND_SMOOTH);Mouse(wxEVT_LEFT_DOWN,40,40,true);
    Mouse(wxEVT_MOTION,120,100,true);Mouse(wxEVT_MOTION,240,180,true);Mouse(wxEVT_LEFT_UP,260,200);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(VCLIP_DRAG,tool->GetSubTool());
}
TEST_F(visual_edit, rectangle_escape_polygon_and_existing_style_are_safe) {
    auto tool=Install<VisualToolRectangle>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,90,100,true);
    wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;display->GetEventHandler()->ProcessEvent(esc);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ(text,Text());EXPECT_EQ(nullptr,LiveRead(*c).GetStyle("图形"));
    tool->SetSubTool(static_cast<int>(DrawingShape::Polygon));
    Mouse(wxEVT_LEFT_DOWN,20,20,true);Mouse(wxEVT_LEFT_UP,20,20);
    Mouse(wxEVT_LEFT_DOWN,100,20,true);Mouse(wxEVT_LEFT_UP,100,20);
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_LEFT_UP,100,100);
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    wxKeyEvent enter(wxEVT_CHAR_HOOK);enter.m_keyCode=WXK_RETURN;display->GetEventHandler()->ProcessEvent(enter);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());auto style=LiveRead(*c).GetStyle("图形");ASSERT_NE(nullptr,style);auto data=style->GetEntryData();
    tool->SetSubTool(static_cast<int>(DrawingShape::Ellipse));Drag(200,200,240,260);
    EXPECT_EQ(3u,LiveRead(*c).Events.size());EXPECT_EQ(data,LiveRead(*c).GetStyle("图形")->GetEntryData());
}
TEST_F(visual_edit, commit_observer_exception_ends_task_and_preserves_normal_undo) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    agi::signal::Connection observer=LiveRead(*c).AddCommitListener([]{throw std::runtime_error("observer");});
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,160,170),std::runtime_error);
    EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    observer.Disconnect();c->subsController->Undo();EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, no_op_ordinary_scope_does_not_drop_next_visual_gesture) {
    Install<VisualToolScale>();auto text=Text();
    {auto ordinary=c->subtitleAccess->TryWrite();ASSERT_TRUE(ordinary);}
    Drag(200,200,230,170);EXPECT_NE(text,Text());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, toolbar_callbacks_detach_on_tool_destruction) {
    Install<VisualToolDrag>();auto id=toolbar->GetToolByPos(1)->GetId();
    wxCommandEvent toggle(wxEVT_TOOL,id);toolbar->GetEventHandler()->ProcessEvent(toggle);
    EXPECT_NE(std::string::npos,Text().find("\\move("));
    auto text=Text();Install<VisualToolCross>();
    toolbar->GetEventHandler()->ProcessEvent(toggle);EXPECT_EQ(text,Text());
    Install<VisualToolVectorClip>();Install<VisualToolCross>();
    wxCommandEvent old_clip(wxEVT_TOOL,1301);toolbar->GetEventHandler()->ProcessEvent(old_clip);
    EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, rejected_structural_insertion_does_not_add_style_or_row) {
    auto session=c->subtitleAccess->TryBeginInteraction({LiveRead(*c).Events.front().Id});ASSERT_TRUE(session);
    AssDialogue drawing;drawing.Id=LiveRead(*c).Events.front().Id;AssStyle style;style.name="must-not-be-added";
    auto count=LiveRead(*c).Styles.size();
    EXPECT_THROW(session->InsertBefore(drawing.Id,drawing,&style),std::invalid_argument);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ(count,LiveRead(*c).Styles.size());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, outside_video_rectangle_click_does_not_leave_a_write_task) {
    Install<VisualToolRectangle>();Mouse(wxEVT_LEFT_DOWN,-10,-10,true);Mouse(wxEVT_LEFT_UP,-10,-10);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(1u,LiveRead(*c).Events.size());
}
TEST_F(visual_edit, cross_double_click_preserves_move_origin_and_unrelated_tags) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\move(100,100,200,200,10,900)\\org(80,80)\\fax0.2\\i1}字幕";LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_TEXT);}
    Install<VisualToolCross>();Mouse(wxEVT_LEFT_DCLICK,150,160,true);Mouse(wxEVT_LEFT_UP,150,160);
    EXPECT_NE(std::string::npos,Text().find("\\move(150,160,250,260,10,900)"));
    EXPECT_NE(std::string::npos,Text().find("\\org(130,140)"));EXPECT_NE(std::string::npos,Text().find("\\fax0.2"));
}
TEST_F(visual_edit, replacing_document_content_invalidates_old_geometry_and_target_ids) {
    Install<VisualToolDrag>();auto previous=LiveRead(*c).Events.front().Id;
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    {auto write=c->subtitleAccess->TryWrite();AssFile replacement;replacement.LoadDefault();
     replacement.SetScriptInfo("PlayResX","640");replacement.SetScriptInfo("PlayResY","480");
     replacement.Events.front().Text="{\\pos(300,300)}new document";LiveWriteForTest(*c).swap(replacement);
     LiveWriteForTest(*c).Commit("new document",AssFile::COMMIT_NEW);
     auto active=&LiveRead(*c).Events.front();c->selectionController->SetSelectionAndActiveIDs({active->Id}, active->Id);}
    Mouse(wxEVT_LEFT_UP,160,170);
    EXPECT_NE(previous,LiveRead(*c).Events.front().Id);EXPECT_EQ("{\\pos(300,300)}new document",Text());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Drag(300,300,350,350);EXPECT_NE(std::string::npos,Text().find("\\pos(350,350)"));
}
TEST_F(visual_edit, feature_hit_order_follows_document_order_after_sort) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue(LiveRead(*c).Events.front());
     LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Events.reverse();LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_ORDER);}
    auto expected=LiveRead(*c).Events.back().Id;auto untouched=LiveRead(*c).Events.front().Text.get();
    Install<VisualToolDrag>();Drag(100,100,160,170);
    EXPECT_EQ(expected,c->selectionController->ReadActiveLine()->Id);
    EXPECT_EQ(untouched,LiveRead(*c).Events.front().Text.get());EXPECT_NE(untouched,LiveRead(*c).Events.back().Text.get());
}
TEST_F(visual_edit, missing_target_without_commit_cannot_partially_write_other_rows) {
    AssDialogue* other;
    {auto write=c->subtitleAccess->TryWrite();other=new AssDialogue;other->Text="untouched";
     LiveWriteForTest(*c).Events.push_back(*other);LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    // Deliberately simulate a not-yet-migrated C1-C caller which removes a row without Commit.
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.erase_and_dispose(LiveWriteForTest(*c).Events.begin(),[](auto row){delete row;});}
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,160,170),std::out_of_range);
    EXPECT_EQ("untouched",Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->selectionController->SetSelectionAndActiveIDs({other->Id}, other->Id);
}
class VisualAccessProbe : public VisualTool<VisualDraggableFeature> {
public:
    using VisualTool::VisualTool;
    void Draw() override {}
    void Override() { SetSelectedOverride("\\pos","(200,200)"); }
};
TEST_F(visual_edit, override_interface_rejects_calls_without_interaction) {
    auto tool=std::make_unique<VisualAccessProbe>(display,c.get());auto probe=tool.get();
    display->SetTool(std::move(tool));auto text=Text();
    EXPECT_THROW(probe->Override(),std::logic_error);EXPECT_EQ(text,Text());
}
TEST_F(visual_edit, invalid_preview_batch_changes_neither_renderer_nor_live_model) {
    auto before=FrameData();auto text=Text();
    AssDialogueBase valid=LiveRead(*c).Events.front();valid.Text="{\\pos(500,400)}different";
    auto missing=valid;missing.Id+=100000;
    c->project->VideoProvider()->PreviewSubtitles({valid,missing});
    EXPECT_EQ(before,FrameData());EXPECT_EQ(text,Text());
    c->project->VideoProvider()->PreviewSubtitles({valid,valid});EXPECT_EQ(before,FrameData());
}
TEST_F(visual_edit, ten_thousand_rows_keep_mouse_motion_local_to_edited_targets) {
    {auto write=c->subtitleAccess->TryWrite();for(int i=1;i<10000;++i){auto row=new AssDialogue;
     row->Start=10000;row->End=15000;row->Text="inactive subtitle";LiveWriteForTest(*c).Events.push_back(*row);}
     LiveWriteForTest(*c).Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);
    auto start=std::chrono::steady_clock::now();
    for(int i=1;i<=100;++i)Mouse(wxEVT_MOTION,100+i,100+i,true);
    auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("B2 10000 rows, 100 mouse moves: %.3f ms UI dispatch\n",milliseconds);
    EXPECT_LT(milliseconds,10000);EXPECT_EQ("{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail",Text());
    Mouse(wxEVT_LEFT_UP,200,200);EXPECT_EQ(10000u,LiveRead(*c).Events.size());
    EXPECT_NE(std::string::npos,Text().find("\\pos(200,200)"));
    EXPECT_EQ("inactive subtitle",LiveRead(*c).Events.back().Text.get());
}

TEST_F(visual_edit, toolbar_can_be_destroyed_before_its_tool_and_canvas) {
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,140,150,true);
    delete toolbar;toolbar=nullptr;
    delete display;display=nullptr;c->videoDisplay=nullptr;
    EXPECT_EQ("{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail",Text());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(visual_edit, changing_vector_subtool_cancels_uncommitted_spline) {
    auto tool=Install<VisualToolVectorClip>();tool->SetSubTool(VCLIP_LINE);auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,50,50,true);Mouse(wxEVT_MOTION,150,150,true);
    tool->SetSubTool(VCLIP_DRAG);Mouse(wxEVT_LEFT_UP,150,150);
    EXPECT_EQ(text,Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, ass_save_reopen_contains_only_confirmed_visual_edits_and_drawing_style) {
    Install<VisualToolRectangle>();Drag(30,40,120,150);
    ASSERT_EQ(2u,LiveRead(*c).Events.size());auto shape=LiveRead(*c).Events.front().Text.get();
    Install<VisualToolDrag>();
    auto save=agi::fs::path((std::filesystem::current_path()/"visual-result.ass").wstring());
    {auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile reopened;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&reopened,save,c->project->Timecodes(),"utf-8");
    ASSERT_EQ(2u,reopened.Events.size());EXPECT_EQ(shape,reopened.Events.front().Text.get());
    EXPECT_NE(nullptr,reopened.GetStyle("图形"));
    auto saved=LiveRead(*c).Events.front().Text.get();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,60,70,true);
    {auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile pending;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&pending,save,c->project->Timecodes(),"utf-8");
    EXPECT_EQ(saved,pending.Events.front().Text.get());
    wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;display->GetEventHandler()->ProcessEvent(esc);
    EXPECT_EQ(saved,LiveRead(*c).Events.front().Text.get());
}

TEST_F(visual_edit, entering_preview_pan_mode_cancels_visual_edit_and_releases_capture) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,140,150,true);
    display->SetPanMode(true);Mouse(wxEVT_LEFT_UP,140,150);
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, unnotified_metadata_and_one_millisecond_changes_are_not_overwritten) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,140,150,true);
    {auto legacy=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Layer=7;LiveWriteForTest(*c).Events.front().Start=1;}
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,140,150),std::logic_error);
    EXPECT_EQ(text,Text());EXPECT_EQ(7,LiveRead(*c).Events.front().Layer);
    EXPECT_EQ(1,LiveRead(*c).Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(ordinary_edit, style_panel_resizes_shapes) {
    auto Text=[&] { return std::string(LiveRead(*c).Events.front().Text.get()); };
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();
     row.Text="{\\an7\\pos(30,40)\\p1}m 0 0 l 90 0 90 110 0 110{\\p0}";
     LiveWriteForTest(*c).Commit("shape",AssFile::COMMIT_DIAG_TEXT);}
    {auto id=LiveRead(*c).Events.front().Id;c->selectionController->SetSelectionAndActiveIDs({id},id);}
    auto text=Text();
    ASSERT_TRUE(CanEditDrawing(c.get()));
    auto size=ShapeSize(c.get());ASSERT_TRUE(size);
    // The panel's Shape section, only for a shape
    auto panel=new StylePanel(window,c.get());
    BOOST_SCOPE_EXIT_ALL(&){panel->Destroy();};
    panel->UpdateNow();
    EXPECT_TRUE(panel->IsSectionVisible("shape"));
    EXPECT_EQ(wxString::Format("%g",std::round(size->first*100)/100),panel->Shown()["shape_width"]);
    // A drag is one undo step; the points are scaled, no \fscx
    panel->EnterNumber("shape_width",size->first+10,false);
    panel->EnterNumber("shape_width",size->first+20,true);
    EXPECT_NEAR(size->first+20,ShapeSize(c.get())->first,0.01);
    EXPECT_NEAR(size->second,ShapeSize(c.get())->second,0.01) << "the height stays";
    EXPECT_EQ(std::string::npos,Text().find("\\fscx"));
    panel->EnterNumber("shape_height",size->second/2,true);
    EXPECT_NEAR(size->second/2,ShapeSize(c.get())->second,0.01);
    auto changed=Text();
    c->subsController->Undo();EXPECT_NEAR(size->first+20,ShapeSize(c.get())->first,0.01);EXPECT_NEAR(size->second,ShapeSize(c.get())->second,0.01);
    c->subsController->Undo();EXPECT_EQ(text,Text()) << "the whole drag in one step";
    c->subsController->Redo();c->subsController->Redo();EXPECT_EQ(changed,Text());
    // Locked, the other side follows at the shape's proportions, the drag
    // still one step; the rounded sizes shown during it do not drift them
    c->subsController->Undo();c->subsController->Undo();ASSERT_EQ(text,Text());
    BOOST_SCOPE_EXIT_ALL(&){OPT_SET("App/Style Panel/Lock Shape Proportions")->SetBool(false);};
    panel->UpdateNow();
    panel->Press("shape_lock");
    EXPECT_TRUE(OPT_GET("App/Style Panel/Lock Shape Proportions")->GetBool());
    for(int step=1;step<=6;++step){panel->EnterNumber("shape_width",size->first+step*7.3,false);panel->UpdateNow();}
    panel->EnterNumber("shape_width",size->first*3,true);panel->UpdateNow();
    EXPECT_NEAR(size->first*3,ShapeSize(c.get())->first,0.01);
    EXPECT_NEAR(size->second*3,ShapeSize(c.get())->second,0.01) << "the height follows";
    panel->EnterNumber("shape_height",size->second,true);panel->UpdateNow();
    EXPECT_NEAR(size->first,ShapeSize(c.get())->first,0.01) << "and the width follows the height";
    EXPECT_NEAR(size->second,ShapeSize(c.get())->second,0.01);
    c->subsController->Undo();c->subsController->Undo();EXPECT_EQ(text,Text()) << "each drag one step";
    panel->UpdateNow();
    panel->Press("shape_lock");
    EXPECT_FALSE(OPT_GET("App/Style Panel/Lock Shape Proportions")->GetBool());
    panel->EnterNumber("shape_width",size->first*2,true);
    EXPECT_NEAR(size->second,ShapeSize(c.get())->second,0.01) << "unlocked, the height stays";
    c->subsController->Undo();ASSERT_EQ(text,Text());
    // Scale tags are baked into the points
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();
     row.Text="{\\an7\\pos(10,10)\\fscx200\\p1}m 0 0 l 50 0 50 20 0 20{\\p0}";
     LiveWriteForTest(*c).Commit("scaled",AssFile::COMMIT_DIAG_TEXT);}
    EXPECT_NEAR(100,ShapeSize(c.get())->first,0.01);
    EXPECT_NE(-1,ResizeShape(c.get(),60,std::nullopt));
    EXPECT_EQ(std::string::npos,Text().find("\\fscx")) << Text();
    EXPECT_NE(std::string::npos,Text().find("l 60 0 60 20 0 20")) << "the points carry the scale: " << Text();
    EXPECT_NEAR(60,ShapeSize(c.get())->first,0.01);
    EXPECT_NEAR(20,ShapeSize(c.get())->second,0.01);
    // Not while another edit holds the subtitles
    auto before=Text();
    {auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
     EXPECT_EQ(-1,ResizeShape(c.get(),40,std::nullopt));EXPECT_EQ(before,Text());}
    // Text is not a shape: no section
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="Hello";
     LiveWriteForTest(*c).Commit("text",AssFile::COMMIT_DIAG_TEXT);}
    panel->UpdateNow();
    EXPECT_FALSE(CanEditDrawing(c.get()));
    EXPECT_FALSE(panel->IsSectionVisible("shape"));
}
TEST_F(ordinary_edit, dialog_snapshot_closes_with_parent_and_rejects_duplicate_batch_before_writing) {
    SubtitleDialogEdit edit(c.get());edit.ResetActive();ASSERT_TRUE(edit.Begin());
    auto row=edit.Rows().front();row.Text="must not be written";
    EXPECT_THROW(edit.Commit({row,row},"duplicate",AssFile::COMMIT_DIAG_TEXT),std::invalid_argument);
    EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    edit.ResetActive();delete window;window=nullptr;
    EXPECT_FALSE(edit.Alive());EXPECT_FALSE(edit.Begin());
}

TEST_F(ordinary_edit, translation_dialog_preserves_tags_ids_and_per_line_undo) {
    int second;
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\i1}one";auto row=new AssDialogue();row->Text="{\\b1}two";second=row->Id;LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    auto first=LiveRead(*c).Events.front().Id;DialogTranslation dialog(c.get());
    SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;
    ASSERT_NE(nullptr,target);target->SetTextRaw("中 文");EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());dialog.Commit(true);
    EXPECT_EQ(first,LiveRead(*c).Events.front().Id);EXPECT_EQ("{\\i1}中 文",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(second,c->selectionController->ReadActiveLine()->Id);
    target->SetTextRaw("译文");dialog.Commit(false);EXPECT_EQ("{\\b1}译文",LiveRead(*c).Events.back().Text.get());
    c->subsController->Undo();EXPECT_EQ("{\\b1}two",LiveRead(*c).Events.back().Text.get());EXPECT_EQ("{\\i1}中 文",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("{\\b1}译文",LiveRead(*c).Events.back().Text.get());
}
TEST_F(ordinary_edit, translation_dialog_rejects_stale_row_and_releases_draft_on_close) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="source";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    {
        DialogTranslation dialog(c.get());dialog.InsertOriginal();EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
        {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="external";LiveWriteForTest(*c).Commit("external",AssFile::COMMIT_DIAG_TEXT);}
        AcceptMessages accept;dialog.Commit(false);EXPECT_EQ("external",LiveRead(*c).Events.front().Text.get());
    }
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(ordinary_edit, kanji_dialog_validates_all_targets_and_commits_once_or_discards) {
    std::vector<int> ids;
    {auto write=c->subtitleAccess->TryWrite();auto style=new AssStyle();style->name="Target";LiveWriteForTest(*c).Styles.push_back(*style);
        LiveWriteForTest(*c).Events.front().Text=R"({\k10}a)";ids.push_back(LiveWriteForTest(*c).Events.front().Id);
        for(int i=1;i<4;++i){auto row=new AssDialogue();row->Style=i%2?"Target":"Default";row->Text=i%2?"字":R"({\k20}b)";ids.push_back(row->Id);LiveWriteForTest(*c).Events.push_back(*row);}
        LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM|AssFile::COMMIT_STYLES);
    }
    auto text=[&](int id){for(auto const& row:LiveRead(*c).Events)if(row.Id==id)return row.Text.get();return std::string();};
    auto prepare=[&](wxDialog* dialog){
        std::vector<FlatChoice*> combos;for(auto child:descendants(dialog))if(auto combo=dynamic_cast<FlatChoice*>(child))combos.push_back(combo);
        ASSERT_EQ(2u,combos.size());combos[0]->SetStringSelection("Default");combos[1]->SetStringSelection("Target");
    };
    auto press=[&](wxDialog* dialog,wxString label){for(auto child:descendants(dialog))if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()==label){b->Press();return;}FAIL()<<"Button missing";};
    {RunModal accept("Kanji timer",[&](wxDialog* dialog){prepare(dialog);press(dialog,"S&tart!");EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());for(int i=0;i<2;++i){press(dialog,"&Link");press(dialog,"&Accept Line");}EXPECT_EQ("字",text(ids[1]));press(dialog,"&Close");});ShowKanjiTimerDialog(c.get());}
    EXPECT_EQ(R"({\k10}字)",text(ids[1]));EXPECT_EQ(R"({\k20}字)",text(ids[3]));EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->subsController->Undo();EXPECT_EQ("字",text(ids[1]));EXPECT_EQ("字",text(ids[3]));c->subsController->Redo();EXPECT_EQ(R"({\k20}字)",text(ids[3]));c->subsController->Undo();
    {RunModal cancel("Kanji timer",[&](wxDialog* dialog){prepare(dialog);press(dialog,"S&tart!");press(dialog,"&Link");press(dialog,"&Accept Line");wxCloseEvent e(wxEVT_CLOSE_WINDOW);dialog->GetEventHandler()->ProcessEvent(e);});ShowKanjiTimerDialog(c.get());}
    EXPECT_EQ("字",text(ids[1]));EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    {RunModal stale("Kanji timer",[&](wxDialog* dialog){prepare(dialog);press(dialog,"S&tart!");for(int i=0;i<2;++i){press(dialog,"&Link");press(dialog,"&Accept Line");}
        {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.back().Text="external";LiveWriteForTest(*c).Commit("external",AssFile::COMMIT_DIAG_TEXT);}
        AcceptMessages messages;press(dialog,"&Close");EXPECT_TRUE(dialog->IsShown());});ShowKanjiTimerDialog(c.get());}
    EXPECT_EQ("字",text(ids[1]));EXPECT_EQ("external",text(ids[3]));EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, video_properties_prompt_is_detached_and_preserves_resample_undo) {
    {std::ofstream video("properties.y4m",std::ios::binary);video<<"YUV4MPEG2 W32 H24 F25:1 Ip A1:1 C420jpeg\nFRAME\n";std::string pixels(32*24*3/2,'\x80');video.write(pixels.data(),pixels.size());}
    OPT_SET("Video/No YCbCr Matrix in Script")->SetInt(0);OPT_SET("Video/PlayRes Mismatch")->SetInt(0);OPT_SET("Video/No LayoutRes in Script")->SetInt(0);OPT_SET("Video/LayoutRes Mismatch")->SetInt(0);
    AcceptMessages messages;c->project->LoadVideo("properties.y4m");ASSERT_NE(nullptr,c->project->VideoProvider());FrameData();
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).SetScriptInfo("PlayResX","80");LiveWriteForTest(*c).SetScriptInfo("PlayResY","60");LiveWriteForTest(*c).Events.front().Text=R"({\pos(20,20)\bord4}中 文)";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_SCRIPTINFO|AssFile::COMMIT_DIAG_TEXT);}
    auto id=LiveRead(*c).Events.front().Id;auto pointer=&LiveRead(*c).Events.front();auto text=LiveRead(*c).Events.front().Text.get();auto size=LiveRead(*c).Styles.front().fontsize;
    OPT_SET("Video/PlayRes Mismatch")->SetInt(1);
    {RunModal choose("Resolution mismatch",[&](wxDialog* dialog){EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
        for(auto child:descendants(dialog))if(auto list=dynamic_cast<FlatList*>(child))list->SetSelection(1);button(dialog,wxID_OK);
    });UpdateVideoProperties(c.get(),c->project->VideoProvider());}
    EXPECT_EQ(32,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));EXPECT_NE(text,LiveRead(*c).Events.front().Text.get());EXPECT_DOUBLE_EQ(std::round(size*0.4),LiveRead(*c).Styles.front().fontsize);EXPECT_EQ(id,LiveRead(*c).Events.front().Id);EXPECT_EQ(pointer,&LiveRead(*c).Events.front());
    auto changed=LiveRead(*c).Events.front().Text.get();c->subsController->Undo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());c->subsController->Undo();EXPECT_EQ(text,LiveRead(*c).Events.front().Text.get());EXPECT_EQ(80,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));c->subsController->Redo();EXPECT_EQ(changed,LiveRead(*c).Events.front().Text.get());
}
TEST_F(visual_edit, video_properties_denies_comparison_without_changing_script_metadata) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).SetScriptInfo("PlayResX","0");LiveWriteForTest(*c).SetScriptInfo("PlayResY","0");LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_SCRIPTINFO);}
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);AcceptMessages messages;UpdateVideoProperties(c.get(),c->project->VideoProvider());
    EXPECT_EQ(0,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));
}

TEST_F(ordinary_edit, dialog_batch_rejects_reordered_missing_or_invalid_rows_before_any_write) {
    {auto write=c->subtitleAccess->TryWrite();for(int i=0;i<2;++i)LiveWriteForTest(*c).Events.push_back(*new AssDialogue());LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    SubtitleDialogEdit edit(c.get());edit.ResetAll();auto updates=edit.Rows();updates.front().Text="first";updates.back().Text="last";
    updates.back().Row=99;EXPECT_THROW(edit.Commit(updates,"bad order",AssFile::COMMIT_DIAG_TEXT),std::invalid_argument);EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    edit.ResetAll();updates=edit.Rows();updates.front().Text="first";updates.back().Text="last";
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.splice(LiveWriteForTest(*c).Events.begin(),LiveWriteForTest(*c).Events,LiveWriteForTest(*c).iterator_to(LiveWriteForTest(*c).Events.back()));}
    EXPECT_FALSE(edit.Commit(updates,"reordered",AssFile::COMMIT_DIAG_TEXT));for(auto const& row:LiveRead(*c).Events)EXPECT_EQ("",row.Text.get());
    edit.ResetAll();updates=edit.Rows();updates.front().Text="first";
    {auto write=c->subtitleAccess->TryWrite();delete &LiveRead(*c).Events.back();}
    EXPECT_FALSE(edit.Commit(updates,"removed",AssFile::COMMIT_DIAG_TEXT));EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(ordinary_edit, translation_dialog_repeated_preview_does_not_reinterpret_inserted_tags_as_blocks) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text=R"({\i1}source)";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    DialogTranslation dialog(c.get());SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;
    ASSERT_NE(nullptr,target);target->SetTextRaw(R"({\b1}译文)");dialog.Commit(false);EXPECT_EQ(R"({\i1}{\b1}译文)",LiveRead(*c).Events.front().Text.get());
    target->SetTextRaw("second");dialog.Commit(false);EXPECT_EQ(R"({\i1}second)",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, dialog_noncontiguous_batch_preserves_unselected_fields_and_releases_on_notification_exception) {
    {auto write=c->subtitleAccess->TryWrite();for(int i=0;i<2;++i)LiveWriteForTest(*c).Events.push_back(*new AssDialogue());LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM);}
    SubtitleDialogEdit edit(c.get());auto first=LiveRead(*c).Events.front().Id,last=LiveRead(*c).Events.back().Id;edit.Reset({first,last});auto updates=edit.Rows();updates.front().Text="first";updates.back().Text="last";
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([]{throw std::runtime_error("observer");});
    EXPECT_THROW(edit.Commit(updates,"complete batch",AssFile::COMMIT_DIAG_TEXT),std::runtime_error);
    EXPECT_EQ("first",LiveRead(*c).Events.front().Text.get());EXPECT_EQ("last",LiveRead(*c).Events.back().Text.get());EXPECT_EQ("",std::next(LiveRead(*c).Events.begin())->Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    throwing.Disconnect();c->subsController->Undo();for(auto const& row:LiveRead(*c).Events)EXPECT_EQ("",row.Text.get());
}

TEST_F(ordinary_edit, translation_dialog_persists_only_confirmed_text_and_keeps_metadata) {
    uint32_t extra;
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();row.Text="source";row.Comment=true;row.Layer=3;row.Start=1;row.End=2002;row.Actor="actor";row.Effect="effect";extra=LiveWriteForTest(*c).AddExtradata("unknown","payload");row.ExtradataIds=std::vector<uint32_t>{extra};LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_FULL);}
    auto id=LiveRead(*c).Events.front().Id;
    {DialogTranslation dialog(c.get());SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;ASSERT_NE(nullptr,target);target->SetTextRaw("中 文");dialog.Commit(false);target->SetTextRaw("unconfirmed");}
    auto const& row=LiveRead(*c).Events.front();EXPECT_EQ(id,row.Id);EXPECT_EQ(1,row.Start.GetMilliseconds());EXPECT_EQ(2002,row.End.GetMilliseconds());EXPECT_TRUE(row.Comment);EXPECT_EQ(3,row.Layer);EXPECT_EQ("actor",row.Actor.get());EXPECT_EQ("effect",row.Effect.get());EXPECT_EQ(std::vector<uint32_t>{extra},row.ExtradataIds.get());
    auto save=agi::fs::path((std::filesystem::current_path()/"dialog-result.ass").wstring());{auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile reopened;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&reopened,save,c->project->Timecodes(),"utf-8");EXPECT_EQ("中 文",reopened.Events.front().Text.get());ASSERT_EQ(1u,reopened.Extradata.size());EXPECT_EQ("payload",reopened.Extradata.front().value);
}
namespace {
speech::Word SW(std::string text,int start,int end,double score=0.9,std::string speaker={}){
    return {std::move(text),start,end,score,std::move(speaker)};
}
speech::Segment SS(std::vector<speech::Word> words,std::string speaker={}){
    speech::Segment s;s.start=words.front().start;s.end=words.back().end;s.speaker=std::move(speaker);
    for(auto const& w:words)s.text+=w.text;
    s.words=std::move(words);return s;
}
/// No lead, no stretching: what sentences are made, as heard
speech::Rules PlainRules(){
    speech::Rules r;r.lead_in=r.lead_out=0;r.min_duration=0;r.adjacent_gap=0;return r;
}
std::vector<std::string> Texts(std::vector<speech::Line> const& lines){
    std::vector<std::string> out;for(auto const& l:lines)out.push_back(l.text);return out;
}
}

TEST(speech_lines, whisperx_json_is_read_with_escapes_missing_times_scores_and_speakers) {
    std::istringstream in(R"({"segments":[{"start":1.0,"end":2.5,"text":"こんにちは","speaker":"SPEAKER_00",
        "words":[{"word":"こん","start":1.0,"end":1.4,"score":0.91,"speaker":"SPEAKER_00"},
                 {"word":"にちは","start":1.5,"end":2.5,"score":0.3}, {"word":"2"}]}],
        "word_segments":[],"language":"ja"})");
    auto t=speech::ReadJson(in);
    EXPECT_EQ("ja",t.language);
    ASSERT_EQ(1u,t.segments.size());
    auto const& seg=t.segments[0];
    EXPECT_EQ(1000,seg.start);EXPECT_EQ(2500,seg.end);EXPECT_EQ("SPEAKER_00",seg.speaker);
    ASSERT_EQ(3u,seg.words.size());
    EXPECT_EQ("こん",seg.words[0].text);EXPECT_DOUBLE_EQ(0.91,seg.words[0].score);
    EXPECT_EQ(1500,seg.words[1].start);EXPECT_DOUBLE_EQ(0.3,seg.words[1].score);
    EXPECT_EQ(-1,seg.words[2].start) << "a number the aligner gave no time";
    // faster-whisper's: "probability", integer seconds
    std::istringstream fw(R"({"segments":[{"start":0,"end":1,"text":" Hi","words":[{"word":" Hi","start":0,"end":1,"probability":0.8}]}]})");
    EXPECT_DOUBLE_EQ(0.8,speech::ReadJson(fw).segments[0].words[0].score);
    std::istringstream nothing(R"({"text":"x"})");
    EXPECT_THROW(speech::ReadJson(nothing),agi::InvalidInputException);
    std::istringstream garbage("not json");
    EXPECT_THROW(speech::ReadJson(garbage),agi::InvalidInputException);
}

TEST(speech_lines, elevenlabs_scribe_json_is_read_by_speaker_with_sounds_apart_and_its_language_shortened) {
    std::istringstream in(R"j({"language_code":"jpn","language_probability":0.98,"text":"こんにちは。(笑い)うん。",
        "words":[{"text":"こんにちは。","start":1.0,"end":1.8,"type":"word","speaker_id":"speaker_0","logprob":-0.1},
                 {"text":" ","start":1.8,"end":2.0,"type":"spacing","speaker_id":"speaker_0"},
                 {"text":"(笑い)","start":2.0,"end":2.6,"type":"audio_event","speaker_id":"speaker_0"},
                 {"text":"laughter","start":2.6,"end":2.9,"type":"audio_event","speaker_id":"speaker_0"},
                 {"text":"うん","start":3.0,"end":3.2,"type":"word","speaker_id":"speaker_1"},
                 {"text":"。","start":3.2,"end":3.3,"type":"word","speaker_id":"speaker_1"}]})j");
    auto t=speech::ReadJson(in);
    EXPECT_EQ("ja",t.language) << "ISO 639-3 made the 639-1 the unspaced languages are known by";
    ASSERT_EQ(4u,t.segments.size()) << "a piece a speaker's run, each sound its own";
    EXPECT_EQ(1000,t.segments[0].start);EXPECT_EQ(1800,t.segments[0].end);
    ASSERT_EQ(1u,t.segments[0].words.size()) << "spacing dropped";
    EXPECT_NEAR(0.905,t.segments[0].words[0].score,0.001);
    EXPECT_EQ("speaker_0",t.segments[0].speaker);
    EXPECT_EQ("(笑い)",t.segments[1].words[0].text);
    EXPECT_EQ("(laughter)",t.segments[2].words[0].text) << "a sound named bare is bracketed";
    EXPECT_EQ("speaker_1",t.segments[3].speaker);
    ASSERT_EQ(2u,t.segments[3].words.size());
    EXPECT_EQ(-1,t.segments[3].words[0].score) << "no logprob, no score";

    auto lines=speech::MakeLines(t,PlainRules(),{});
    EXPECT_EQ((std::vector<std::string>{"こんにちは。","(笑い)","(laughter)","うん。"}),Texts(lines));
    ASSERT_EQ(4u,lines.size());
    EXPECT_FALSE(lines[0].note);EXPECT_TRUE(lines[1].note);EXPECT_TRUE(lines[2].note);
    EXPECT_EQ("speaker_1",lines[3].speaker);

    std::istringstream channels(R"({"transcripts":[{"words":[]}]})");
    EXPECT_THROW(speech::ReadJson(channels),agi::InvalidInputException);
}

TEST(speech_lines, a_sound_heard_never_shares_a_line_with_what_is_said) {
    speech::Transcript t;t.language="en";
    t.segments.push_back(SS({SW("Well",0,300),SW("(laughs)",350,700),SW("okay.",750,1000)},"A"));
    EXPECT_EQ((std::vector<std::string>{"Well","(laughs)","okay."}),Texts(speech::MakeLines(t,PlainRules(),{})));
}

TEST(transcribe, the_key_comes_from_the_environment_first_and_the_lists_are_split) {
    EXPECT_EQ("env",transcribe::ApiKey("env","kept"));
    EXPECT_EQ("kept",transcribe::ApiKey(nullptr," kept\n"));
    EXPECT_EQ("kept",transcribe::ApiKey("","kept")) << "set but empty is not set";
    EXPECT_EQ("",transcribe::ApiKey(nullptr,""));
    EXPECT_EQ((std::vector<std::string>{"en","zh","fr"}),transcribe::SplitCodes("en, zh  fr"));
    EXPECT_EQ((std::vector<std::string>{"Tokyo Tower","Madoka"}),transcribe::SplitList(" Tokyo Tower, Madoka,\n")) << "a term keeps its spaces";
    EXPECT_TRUE(transcribe::SplitList("").empty());
}

TEST(transcribe, the_actors_names_are_terms_to_recognise_each_once_after_the_extras_but_not_a_recognisers_speaker_labels) {
    AssFile file;
    for(auto actor:{"Madoka","","Madoka","SPEAKER_00","speaker_1"," Homura "}){auto l=new AssDialogue;l->Actor=actor;file.Events.push_back(*l);}
    EXPECT_EQ((std::vector<std::string>{"Mami","Madoka","Homura"}),transcribe::Keyterms(file,{"Mami","Madoka"}));
    EXPECT_EQ((std::vector<std::string>{"Mami","Madoka"}),transcribe::Keyterms(file,{"Mami"},2)) << "no more than the limit";
}

TEST(transcribe, the_form_sent_to_elevenlabs_has_the_model_the_options_and_each_list_repeated) {
    transcribe::Request r;r.media="ep01.mkv";r.diarize=false;
    transcribe::ElevenLabs el;
    auto fields=transcribe::Form(r,el);
    auto value=[&](std::string const& name){std::vector<std::string> v;for(auto const& f:fields)if(f.first==name)v.push_back(f.second);return v;};
    EXPECT_EQ((std::vector<std::string>{"scribe_v3"}),value("model_id"));
    EXPECT_EQ((std::vector<std::string>{"word"}),value("timestamps_granularity"));
    EXPECT_EQ((std::vector<std::string>{"false"}),value("diarize"));
    EXPECT_TRUE(value("language_code").empty()) << "none given, none sent: it is detected";
    EXPECT_TRUE(value("file").empty()) << "the file is not a text field";
    r.language="ja";r.secondary_languages={"en","zh"};r.keyterms={"Madoka","Homura"};r.diarize=true;el.model="scribe_v2";
    fields=transcribe::Form(r,el);
    EXPECT_EQ((std::vector<std::string>{"scribe_v2"}),value("model_id"));
    EXPECT_EQ((std::vector<std::string>{"true"}),value("diarize"));
    EXPECT_EQ((std::vector<std::string>{"ja"}),value("language_code"));
    EXPECT_EQ((std::vector<std::string>{"en","zh"}),value("secondary_language_codes"));
    EXPECT_EQ((std::vector<std::string>{"Madoka","Homura"}),value("keyterms"));
    r.keyterms.assign(150,"x");
    fields=transcribe::Form(r,el);
    EXPECT_EQ(100u,value("keyterms").size()) << "no more than 100 terms";
}

TEST(transcribe, what_elevenlabs_takes_as_it_is_and_what_its_answers_mean) {
    EXPECT_TRUE(transcribe::Accepted("/v/ep01.mkv"));
    EXPECT_TRUE(transcribe::Accepted("C:\\a\\song.MP3"));
    EXPECT_FALSE(transcribe::Accepted("/v/ep01.mka")) << "written out as WAV first";
    EXPECT_FALSE(transcribe::Accepted("/v/ep01.ts"));
    EXPECT_EQ("",transcribe::Explain(200,"{}"));
    auto refused=transcribe::Explain(401,R"({"detail":{"status":"invalid_api_key","message":"Invalid API key"}})");
    EXPECT_NE(std::string::npos,refused.find("API key")) << refused;
    EXPECT_NE(std::string::npos,refused.find("Invalid API key")) << refused;
    EXPECT_NE(std::string::npos,transcribe::Explain(422,R"({"detail":"keyterms: too many"})").find("keyterms: too many"));
    EXPECT_NE(std::string::npos,transcribe::Explain(500,"<html>Bad Gateway</html>").find("500"));
    EXPECT_EQ(std::string::npos,transcribe::Explain(500,"<html>Bad Gateway</html>").find("<html>")) << "no markup shown";
    EXPECT_EQ("24:03",transcribe::Duration(1443000));
    EXPECT_EQ("1:02:03",transcribe::Duration(3723000));
    EXPECT_EQ("0:05",transcribe::Duration(5400));
}

TEST(transcribe, the_users_command_line_gets_the_paths_put_in_its_quotes_taken_off_and_bracketed_parts_dropped_when_empty) {
    std::string templ="whisperx \"{media}\" --output_dir '{out}' --output_format json [--language {language}] --model large-v3";
    auto with=transcribe::CommandLine(templ,"/videos/ep 01.mkv","/tmp/out dir","ja");
    EXPECT_EQ((std::vector<std::string>{"whisperx","/videos/ep 01.mkv","--output_dir","/tmp/out dir","--output_format","json","--language","ja","--model","large-v3"}),with);
    auto without=transcribe::CommandLine(templ,"/videos/ep 01.mkv","/tmp/out","");
    EXPECT_EQ((std::vector<std::string>{"whisperx","/videos/ep 01.mkv","--output_dir","/tmp/out","--output_format","json","--model","large-v3"}),without);
    EXPECT_EQ((std::vector<std::string>{"a","/videos/ep 01.mkv"}),transcribe::CommandLine("a {media}","/videos/ep 01.mkv","o","")) << "a path with spaces stays one word even unquoted";
    EXPECT_EQ((std::vector<std::string>{"x","",""}),transcribe::CommandLine("x \"\" ''","m","o","")) << "an empty quoted word is a word";
    EXPECT_TRUE(transcribe::CommandLine("   ","m","o","").empty());
}

TEST(transcribe, the_json_the_command_wrote_is_the_one_named_after_the_media_else_the_only_one) {
    agi::fs::path dir=agi::fs::path(std::filesystem::temp_directory_path()/("aegisub-transcribe-test-"+std::to_string(::getpid())));
    std::filesystem::remove_all(std::filesystem::path(dir));std::filesystem::create_directories(std::filesystem::path(dir));
    EXPECT_FALSE(transcribe::OutputJson(dir,"/v/ep01.mkv")) << "nothing yet";
    {std::ofstream(dir/"other.json")<<"{}";}
    auto only=transcribe::OutputJson(dir,"/v/ep01.mkv");
    ASSERT_TRUE(only);EXPECT_EQ("other.json",only->filename().string());
    {std::ofstream(dir/"ep01.json")<<"{}";std::ofstream(dir/"ep01.txt")<<"";}
    auto named=transcribe::OutputJson(dir,"/v/ep01.mkv");
    ASSERT_TRUE(named);EXPECT_EQ("ep01.json",named->filename().string());
    EXPECT_FALSE(transcribe::OutputJson(dir,"/v/ep02.mkv")) << "two, neither its own";
    std::filesystem::remove_all(std::filesystem::path(dir));
}

#ifndef _WIN32
namespace {
struct TranscribeCancelledSink final : agi::ProgressSink {
    void SetIndeterminate() override {}
    void SetTitle(std::string const&) override {}
    void SetMessage(std::string const&) override {}
    void SetProgress(int64_t,int64_t) override {}
    void Log(std::string const&) override {}
    bool IsCancelled() override { return true; }
};
}

TEST(transcribe, a_program_of_the_users_is_run_its_output_and_exit_code_read_and_the_json_it_wrote_found) {
    agi::fs::path dir=agi::fs::path(std::filesystem::temp_directory_path()/("aegisub-transcribe-run-"+std::to_string(::getpid())));
    std::filesystem::remove_all(std::filesystem::path(dir));std::filesystem::create_directories(std::filesystem::path(dir));
    {
        std::vector<std::string> argv{"sh","-c","printf '{\"words\":[]}' > \"$0/ep01.json\"; echo started; echo 'done 日本語'; exit 3",dir.string()};
        transcribe::Program program(argv,dir);
        DialogProgress progress(nullptr,"Transcribing","Running sh...");
        EXPECT_NO_THROW(progress.Run([&](agi::ProgressSink *ps){ program.Wait(ps); }));
        EXPECT_EQ(3,program.ExitCode());
        EXPECT_NE(std::string::npos,program.Output().find("done 日本語")) << program.Output();
        auto json=transcribe::OutputJson(dir,"/v/ep01.mkv");
        ASSERT_TRUE(json);
        auto t=speech::ReadJson(*agi::io::Open(*json));
        EXPECT_TRUE(t.segments.empty()) << "ElevenLabs' shape, read as it is";
    }
    EXPECT_THROW(transcribe::Program({dir.string()+"/no-such-program"},dir),agi::EnvironmentError);
    {
        // Cancelled: it is killed and the wait ends in a cancel
        transcribe::Program slow({"sh","-c","sleep 30"},dir);
        TranscribeCancelledSink cancelled;
        EXPECT_THROW(slow.Wait(&cancelled),agi::UserCancelException);
    }
    for(int i=0;i<50;++i){wxYield();wxMilliSleep(5);}
    std::filesystem::remove_all(std::filesystem::path(dir));
}
#endif

TEST(speech_lines, a_line_a_sentence_joined_across_pieces_and_ended_by_a_full_stop_or_another_speaker) {
    speech::Transcript t;t.language="ja";
    // Heard in two pieces, no sentence end between: one line
    t.segments.push_back(SS({SW("今日は",0,800)},"A"));
    t.segments.push_back(SS({SW("いい",900,1200),SW("天気ですね。",1200,2000),SW("本当に",2100,2500),SW("ね。",2500,2800)},"A"));
    // Another speaker, close after
    t.segments.push_back(SS({SW("うん。",2900,3200)},"B"));
    // A long pause in a piece ends the sentence
    t.segments.push_back(SS({SW("じゃあ",5000,5400),SW("行こう",6500,7000)},"A"));
    auto lines=speech::MakeLines(t,PlainRules(),{});
    EXPECT_EQ((std::vector<std::string>{"今日はいい天気ですね。","本当にね。","うん。","じゃあ","行こう"}),Texts(lines));
    EXPECT_EQ("B",lines[2].speaker);
    EXPECT_EQ(0,lines[0].start);EXPECT_EQ(2000,lines[0].end);
    for(auto const& l:lines)EXPECT_EQ(0,l.group);
}

TEST(speech_lines, without_punctuation_sentences_end_at_the_audio_pauses_though_the_characters_times_are_off) {
    // As WhisperX aligns Japanese: a character at a time, no gaps, and here
    // all of it squeezed into the first 2.6 s of the 6 s it is said in
    speech::Transcript t;t.language="ja";
    std::vector<speech::Word> chars;
    std::string const text="私はクロですありがとうございました見てくださいね";
    for(size_t i=0,k=0;i<text.size();i+=3,++k)chars.push_back(SW(text.substr(i,3),int(k)*100,int(k+1)*100));
    t.segments.push_back(SS(chars));
    std::vector<std::pair<int,int>> const quiet{{1500,1900},{4000,4400},{6000,7000}};
    auto lines=speech::MakeLines(t,PlainRules(),{},quiet);
    // Not ありがとう|ございました, くだ|さい nor before ね
    EXPECT_EQ((std::vector<std::string>{"私はクロです","ありがとうございました","見てくださいね"}),Texts(lines));
    ASSERT_EQ(3u,lines.size());
    EXPECT_EQ(1500,lines[0].end);
    EXPECT_EQ(1900,lines[1].start);EXPECT_EQ(4000,lines[1].end);
    EXPECT_EQ(4400,lines[2].start);EXPECT_EQ(6000,lines[2].end);
    // Without the pauses, by the characters' times: one line
    EXPECT_EQ(1u,speech::MakeLines(t,PlainRules(),{}).size());
}

TEST(speech_lines, a_long_sentence_is_split_where_a_clause_ends_and_both_parts_share_a_group) {
    speech::Transcript t;t.language="ja";
    // 10 s: the clause ends after けど、 with a pause; splitting before です would
    // be shorter work for the middle but wrong
    t.segments.push_back(SS({SW("朝から",0,1500),SW("雨が",1500,3000),SW("降っていたけど、",3000,4500),
        SW("午後には",4900,6500),SW("晴れてきて",6500,8200),SW("よかったです。",8200,10000)}));
    auto r=PlainRules();
    auto lines=speech::MakeLines(t,r,{});
    EXPECT_EQ((std::vector<std::string>{"朝から雨が降っていたけど、","午後には晴れてきてよかったです。"}),Texts(lines));
    ASSERT_EQ(2u,lines.size());
    EXPECT_GT(lines[0].group,0);EXPECT_EQ(lines[0].group,lines[1].group);
    EXPECT_EQ(4500,lines[0].end);EXPECT_EQ(4900,lines[1].start);
    // Short enough: left whole
    r.max_duration=12000;
    EXPECT_EQ(1u,speech::MakeLines(t,r,{}).size());
}

TEST(speech_lines, unsure_words_notes_and_the_times_around_lines_and_cuts) {
    speech::Transcript t;t.language="ja";
    t.segments.push_back(SS({SW("マミ",0,300,0.2),SW("さん",300,500,0.3),SW("は",500,600),SW("ソウルジェム",600,1400,0.1),SW("を",1400,1500),SW("持ってる。",1500,2000)}));
    t.segments.push_back(SS({SW("（笑い）",5000,5600)}));
    auto plain=PlainRules();plain.unsure_below=0.5;
    auto lines=speech::MakeLines(t,plain,{});
    ASSERT_EQ(2u,lines.size());
    EXPECT_EQ((std::vector<std::string>{"マミさん","ソウルジェム"}),lines[0].unsure) << "a run of unsure words is one";
    EXPECT_FALSE(lines[0].note);EXPECT_TRUE(lines[1].note);

    // Timed: lead in and out, stretched to a second, near lines meeting, at
    // a cut between them when there is one, and a cut just inside an end
    // taking it
    speech::Transcript u;u.language="ja";
    u.segments.push_back(SS({SW("はい。",1000,1300)}));
    u.segments.push_back(SS({SW("そうです。",2500,4000)}));
    u.segments.push_back(SS({SW("では。",4300,6000)}));
    speech::Rules r;r.lead_in=100;r.lead_out=200;r.min_duration=1000;r.adjacent_gap=300;
    lines=speech::MakeLines(u,r,{});
    ASSERT_EQ(3u,lines.size());
    EXPECT_EQ(900,lines[0].start);EXPECT_EQ(1900,lines[0].end) << "a second long";
    EXPECT_EQ(2400,lines[1].start);
    EXPECT_EQ(lines[2].start,lines[1].end) << "less than 300 ms apart: they meet";
    lines=speech::MakeLines(u,r,{4150});
    EXPECT_EQ(4150,lines[1].end);EXPECT_EQ(4150,lines[2].start) << "at the cut between them";
    // Said on 50 ms past a cut, 250 ms with the lead-out: the cut takes the end
    lines=speech::MakeLines(u,r,{5950});
    EXPECT_EQ(5950,lines[2].end);
}

TEST(speech_lines, a_line_across_a_cut_is_split_at_a_pause_there_else_kept) {
    speech::Transcript t;t.language="ja";
    t.segments.push_back(SS({SW("それで",0,1000),SW("どうしたの？",1000,2000)}));
    t.segments.push_back(SS({SW("それで",3000,4000),SW("次は",4400,5400),SW("何を",5400,6000)}));
    auto lines=speech::MakeLines(t,PlainRules(),{1000,4200});
    EXPECT_EQ((std::vector<std::string>{"それでどうしたの？","それで","次は何を"}),Texts(lines))
        << "no pause at the first cut: kept whole; a pause at the second: split";
    EXPECT_GT(lines[1].group,0);EXPECT_EQ(lines[1].group,lines[2].group);
}

TEST_F(ordinary_edit, speech_recognition_imported_a_sentence_a_line_in_one_step) {
    speech::Transcript t;t.language="ja";
    t.segments.push_back(SS({SW("こんにちは。",1000,2000,0.9,"SPEAKER_00")},"SPEAKER_00"));
    t.segments.push_back(SS({SW("マミ",3000,3500,0.2,"SPEAKER_01"),SW("です。",3500,4500,0.9,"SPEAKER_01")},"SPEAKER_01"));
    auto description=c->subsController->GetUndoDescription();
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Tool/Speech Import/Bilingual")->SetBool(false); };
    OPT_SET("Tool/Speech Import/Bilingual")->SetBool(true);
    size_t styles=LiveRead(*c).Styles.size();
    EXPECT_EQ(2u,speech::Import(c.get(),t));
    std::vector<std::tuple<std::string,std::string,std::string,std::string>> lines;
    for(auto const& row:LiveRead(*c).Events)lines.emplace_back(row.Style.get(),row.Actor.get(),row.Effect.get(),row.Text.get());
    EXPECT_EQ((std::vector<std::tuple<std::string,std::string,std::string,std::string>>{
        {"JP","SPEAKER_00","","こんにちは。"},{"CN","SPEAKER_00","",""},
        {"JP","SPEAKER_01","?マミ","マミです。"},{"CN","SPEAKER_01","",""}}),lines) << "the new file's empty line made way";
    auto const& first=LiveRead(*c).Events.front();
    EXPECT_EQ(900,int(first.Start));EXPECT_EQ(2350,int(first.End)) << "the timing post-processor's lead";
    EXPECT_EQ(styles+2,LiveRead(*c).Styles.size());
    c->subsController->Undo();
    EXPECT_EQ(1u,LiveRead(*c).Events.size());
    EXPECT_EQ(styles,LiveRead(*c).Styles.size()) << "one step, styles and all";
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    // Something written: kept, the lines put in among it by time
    OPT_SET("Tool/Speech Import/Bilingual")->SetBool(false);
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();row.Start=2500;row.End=2800;row.Text="written";LiveWriteForTest(*c).Commit("text",AssFile::COMMIT_DIAG_FULL);}
    EXPECT_EQ(2u,speech::Import(c.get(),t));
    std::vector<std::string> texts;for(auto const& row:LiveRead(*c).Events)texts.push_back(row.Text.get());
    EXPECT_EQ((std::vector<std::string>{"こんにちは。","written","マミです。"}),texts);
}

TEST(bilingual_marks, read_and_written_in_the_effect_field_beside_what_else_it_holds) {
    auto marks=bilingual::ParseMarks("#12 ?マミ ?ソウルジェム");
    EXPECT_EQ(12,marks.group);
    EXPECT_EQ((std::vector<std::string>{"マミ","ソウルジェム"}),marks.unsure);
    EXPECT_EQ("",marks.rest);
    marks=bilingual::ParseMarks("note #3 ?a b");
    EXPECT_EQ(3,marks.group);EXPECT_EQ("note b",marks.rest);
    // A renderer's effect is never read as marks
    EXPECT_EQ(0,bilingual::ParseMarks("Scroll up;10;20;#5").group);
    EXPECT_EQ("Banner;20",bilingual::ClearMarks("Banner;20"));
    // No commas in the field, and no spaces in a word
    bilingual::Marks written;written.group=7;written.unsure={"a, b"};written.rest="note";
    EXPECT_EQ("note #7 ?a\xEF\xBC\x8C" "b",bilingual::FormatMarks(written));
    EXPECT_EQ("note",bilingual::ClearMarks(bilingual::FormatMarks(written)));
    EXPECT_EQ("karaoke",bilingual::ClearMarks("karaoke"));
    EXPECT_EQ("a  b",bilingual::ClearMarks("a  b")) << "untouched when it holds no marks";
}

TEST_F(ordinary_edit, make_bilingual_pairs_each_line_with_an_empty_translation_line_in_one_step) {
    {auto write=c->subtitleAccess->TryWrite();auto& f=LiveWriteForTest(*c);
     auto& first=f.Events.front();first.Start=1000;first.End=2000;first.Text="こんにちは";first.Effect="#1";first.Actor="マミ";
     auto second=new AssDialogue;second->Start=3000;second->End=4000;second->Text="さようなら";f.Events.push_back(*second);
     f.Commit("lines",AssFile::COMMIT_DIAG_ADDREM);}
    std::set<int> ids;for(auto const& row:LiveRead(*c).Events)ids.insert(row.Id);
    c->selectionController->SetSelectionAndActiveIDs(ids,LiveRead(*c).Events.front().Id);
    auto description=c->subsController->GetUndoDescription();
    size_t styles=LiveRead(*c).Styles.size();
    cmd::call("autotiming/bilingual",c.get());
    std::vector<std::tuple<std::string,int,int,std::string,std::string,std::string>> lines;
    for(auto const& row:LiveRead(*c).Events)lines.emplace_back(row.Style.get(),row.Start,row.End,row.Text.get(),row.Actor.get(),row.Effect.get());
    EXPECT_EQ((std::vector<std::tuple<std::string,int,int,std::string,std::string,std::string>>{
        {"JP",1000,2000,"こんにちは","マミ","#1"},{"CN",1000,2000,"","マミ",""},
        {"JP",3000,4000,"さようなら","",""},{"CN",3000,4000,"","",""}}),lines) << "marks stay on the original";
    EXPECT_NE(nullptr,LiveRead(*c).GetStyle("JP"));EXPECT_NE(nullptr,LiveRead(*c).GetStyle("CN"));
    EXPECT_EQ(styles+2,LiveRead(*c).Styles.size());
    // Done again, nothing is to do: no step
    auto made=c->subsController->GetUndoDescription();
    cmd::call("autotiming/bilingual",c.get());
    EXPECT_EQ(4u,LiveRead(*c).Events.size());
    // One step back: the lines and the styles both
    c->subsController->Undo();
    EXPECT_EQ(2u,LiveRead(*c).Events.size());
    EXPECT_EQ(styles,LiveRead(*c).Styles.size());
    EXPECT_EQ("Default",LiveRead(*c).Events.front().Style.get());
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    c->subsController->Redo();
    EXPECT_EQ(made,c->subsController->GetUndoDescription());

    // The translation assistant writes in the translation line, keeps the
    // original, and goes from original to original
    c->selectionController->SetSelectionAndActiveIDs({LiveRead(*c).Events.front().Id},LiveRead(*c).Events.front().Id);
    auto row=[&](size_t i){return std::next(LiveRead(*c).Events.begin(),i);};
    {DialogTranslation dialog(c.get());SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;ASSERT_NE(nullptr,target);
     target->SetTextRaw("你好");dialog.Commit(true);
     EXPECT_EQ("こんにちは",row(0)->Text.get());EXPECT_EQ("你好",row(1)->Text.get());
     EXPECT_EQ(row(2)->Id,c->selectionController->ReadActiveLine()->Id) << "the translation line passed over";
     target->SetTextRaw("再见");dialog.Commit(false);
     EXPECT_EQ("さようなら",row(2)->Text.get());EXPECT_EQ("再见",row(3)->Text.get());}
    // Back on a translated line, its translation is there to go on from
    c->selectionController->SetSelectionAndActiveIDs({row(0)->Id},row(0)->Id);
    {DialogTranslation dialog(c.get());SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;ASSERT_NE(nullptr,target);
     EXPECT_EQ("你好",std::string(target->GetTextRaw().data()));}
}

TEST_F(ordinary_edit, clear_auto_timing_marks_leaves_the_rest_of_the_effect_field) {
    {auto write=c->subtitleAccess->TryWrite();auto& f=LiveWriteForTest(*c);
     f.Events.front().Effect="#3 ?マミ";
     for(auto effect:{"Banner;20","karaoke","note ?x"}){auto line=new AssDialogue;line->Effect=effect;f.Events.push_back(*line);}
     f.Commit("lines",AssFile::COMMIT_DIAG_ADDREM);}
    auto description=c->subsController->GetUndoDescription();
    cmd::call("autotiming/clear_marks",c.get());
    std::vector<std::string> effects;for(auto const& row:LiveRead(*c).Events)effects.push_back(row.Effect.get());
    EXPECT_EQ((std::vector<std::string>{"","Banner;20","karaoke","note"}),effects);
    auto cleared=c->subsController->GetUndoDescription();
    EXPECT_NE(description,cleared);
    cmd::call("autotiming/clear_marks",c.get());
    EXPECT_EQ(cleared,c->subsController->GetUndoDescription()) << "nothing left to clear: no step";
    c->subsController->Undo();
    EXPECT_EQ("#3 ?マミ",LiveRead(*c).Events.front().Effect.get());
}

TEST_F(ordinary_edit, dialog_dependency_changes_and_explicit_hide_release_pending_work) {
    SubtitleDialogEdit edit(c.get());edit.ResetActive();
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Start=1;}
    EXPECT_FALSE(edit.Begin());edit.ResetActive();
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Styles.front().fontsize+=1;}
    EXPECT_FALSE(edit.Begin());
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="source";LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_TEXT);}
    DialogTranslation dialog(c.get());dialog.Show();SubsTextEditCtrl* target=nullptr;for(auto child:descendants(&dialog))if(auto text=dynamic_cast<SubsTextEditCtrl*>(child))target=text;
    ASSERT_NE(nullptr,target);target->SetTextRaw("draft");EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());dialog.Hide();EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(ordinary_edit, video_layout_writeback_rejects_bad_structure_and_keeps_live_nodes_and_other_state) {
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Properties.video_zoom=2;LiveWriteForTest(*c).AddExtradata("unknown","keep");}
    SubtitleDialogEdit edit(c.get());edit.ResetAll();AssFile calculated(c->subtitleAccess->Read());calculated.Events.front().Id=LiveRead(*c).Events.front().Id;
    calculated.Events.front().Text="layout";calculated.Styles.front().name="renamed";
    EXPECT_THROW(edit.CommitLayout(calculated,"invalid layout",AssFile::COMMIT_DIAG_FULL),std::invalid_argument);EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    calculated.Styles.front().name="Default";calculated.Styles.front().fontsize=24;calculated.Styles.front().UpdateData();calculated.SetScriptInfo("PlayResX","123");
    edit.ResetAll();auto row=&LiveRead(*c).Events.front();auto style=&LiveRead(*c).Styles.front();
    ASSERT_TRUE(edit.CommitLayout(calculated,"layout",AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_STYLES|AssFile::COMMIT_SCRIPTINFO));
    EXPECT_EQ(row,&LiveRead(*c).Events.front());EXPECT_EQ(style,&LiveRead(*c).Styles.front());EXPECT_EQ(row,c->selectionController->ReadActiveLine());EXPECT_EQ("layout",row->Text.get());EXPECT_DOUBLE_EQ(24,style->fontsize);EXPECT_DOUBLE_EQ(2,LiveRead(*c).Properties.video_zoom);EXPECT_EQ("keep",LiveRead(*c).Extradata.front().value);
}
TEST_F(ordinary_edit, video_layout_access_requires_a_current_interaction) {
    auto id=LiveRead(*c).Events.front().Id;AssFile calculated(c->subtitleAccess->Read());calculated.Events.front().Id=id;calculated.SetScriptInfo("Title","blocked");
    auto interaction=c->subtitleAccess->TryBeginInteraction({id});ASSERT_TRUE(interaction);interaction->End();
    EXPECT_THROW(interaction->UpdateLayout(calculated),std::logic_error);EXPECT_NE("blocked",LiveRead(*c).GetScriptInfo("Title"));
}

}

TEST_F(ordinary_edit, controlled_tag_commands_preserve_multiline_data_and_single_undo) {
    cmd::PasteSubtitleLines(c.get(),"one\ntwo");
    std::vector<int> ids;for(auto row:c->selectionController->ReadSelectedLines())ids.push_back(row->Id);
    ASSERT_EQ(2u,ids.size());
    cmd::call("edit/style/bold",c.get());
    for(auto const& row:LiveRead(*c).Events)if(std::find(ids.begin(),ids.end(),row.Id)!=ids.end())EXPECT_NE(std::string::npos,row.Text.get().find("\\b1"));
    c->subsController->Undo();
    for(auto const& row:LiveRead(*c).Events)EXPECT_EQ(std::string::npos,row.Text.get().find("\\b1"));
    c->subsController->Redo();
    for(auto const& row:LiveRead(*c).Events)if(std::find(ids.begin(),ids.end(),row.Id)!=ids.end())EXPECT_NE(std::string::npos,row.Text.get().find("\\b1"));
}
TEST_F(ordinary_edit, controlled_adjoin_and_replace_all_use_production_entries_and_preserve_ids) {
    cmd::PasteSubtitleLines(c.get(),"Dialogue: 0,0:00:00.000,0:00:01.000,Default,A,0,0,0,,one\nDialogue: 0,0:00:02.000,0:00:03.000,Default,B,0,0,0,,one");
    auto selected=c->selectionController->ReadSortedSelection();ASSERT_EQ(2u,selected.size());
    auto second=selected[1]->Id;
    cmd::call("time/continuous/start",c.get());
    EXPECT_EQ(1000,LiveRead(*c).Events.front().End.GetMilliseconds());
    for(auto const& row:LiveRead(*c).Events)if(row.Id==second)EXPECT_EQ(1000,row.Start.GetMilliseconds());
    c->subsController->Undo();
    for(auto const& row:LiveRead(*c).Events)if(row.Id==second)EXPECT_EQ(2000,row.Start.GetMilliseconds());
    SearchReplaceSettings settings{};settings.find="one";settings.replace_with="translated";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::SELECTED;
    c->search->Configure(settings);AcceptMessages messages;EXPECT_TRUE(c->search->ReplaceAll());
    for(auto row:c->selectionController->ReadSelectedLines())EXPECT_EQ("translated",row->Text.get());
    c->subsController->Undo();for(auto row:c->selectionController->ReadSelectedLines())EXPECT_EQ("one",row->Text.get());
}

TEST_F(ordinary_edit, controlled_duplicate_move_join_and_split_preserve_live_identity_and_history) {
    cmd::PasteSubtitleLines(c.get(),"one\ntwo");
    auto selected=c->selectionController->ReadSortedSelection();ASSERT_EQ(2u,selected.size());
    auto original=selected.front();auto id=original->Id;
    cmd::call("edit/line/duplicate",c.get());EXPECT_EQ(5u,LiveRead(*c).Events.size());
    bool preserved=false;for(auto const& row:LiveRead(*c).Events)if(row.Id==id)preserved=&row==original;EXPECT_TRUE(preserved);
    cmd::call("grid/move/up",c.get());EXPECT_EQ(2u,c->selectionController->ReadSelectedSet().size());
    cmd::call("edit/line/join/concatenate",c.get());EXPECT_EQ(4u,LiveRead(*c).Events.size());
    c->subsController->Undo();EXPECT_EQ(5u,LiveRead(*c).Events.size());
    c->subsController->Redo();EXPECT_EQ(4u,LiveRead(*c).Events.size());
    c->textSelectionController->SetSelection(1,1);
    cmd::call("edit/line/split/preserve",c.get());EXPECT_EQ(5u,LiveRead(*c).Events.size());
    c->subsController->Undo();EXPECT_EQ(4u,LiveRead(*c).Events.size());
}

TEST_F(ordinary_edit, controlled_real_style_rename_updates_references_and_repeated_apply_keeps_node_identity) {
    class ConfirmRename : public wxModalDialogHook {
    public: ConfirmRename(){Register();}
        int Enter(wxDialog* dialog) override {return dynamic_cast<wxMessageDialog*>(dialog)?wxID_YES:wxID_NONE;}
    } confirm;
    Input("{\\rDefault}text");
    auto live=LiveRead(*c).GetStyle("Default");wxArrayString fonts;fonts.Add("Arial");
    DialogStyleEditor dialog(window,live,c.get(),nullptr,"",fonts);
    wxTextCtrl* name=nullptr;
    for(auto child:descendants(&dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child);text && text->GetValue()=="Default"){name=text;break;}
    ASSERT_NE(nullptr,name);name->ChangeValue("Renamed");button(&dialog,wxID_APPLY);
    EXPECT_EQ(live,LiveRead(*c).GetStyle("Renamed"));EXPECT_EQ("Renamed",LiveRead(*c).Events.front().Style.get());EXPECT_EQ("{\\rRenamed}text",LiveRead(*c).Events.front().Text.get());
    button(&dialog,wxID_APPLY);EXPECT_EQ(live,LiveRead(*c).GetStyle("Renamed"));
    c->subsController->Undo();c->subsController->Undo();EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Default"));EXPECT_EQ("{\\rDefault}text",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("{\\rRenamed}text",LiveRead(*c).Events.front().Text.get());
}

TEST_F(ordinary_edit, shift_times_is_drawn_switches_and_a_history_list) {
    OPT_SET("Tool/Shift Times/ByTime")->SetBool(false);OPT_SET("Tool/Shift Times/Direction")->SetBool(true);
    OPT_SET("Tool/Shift Times/Type")->SetInt(1);OPT_SET("Tool/Shift Times/Affect")->SetInt(1);
    // The history starts empty, whatever an earlier run left
    std::filesystem::remove(std::filesystem::path(config::path->Decode("?user/shift_history.json").wstring()));
    c->selectionController->SetSelectedIDs({});
    auto open=[&]{
        ShowShiftTimesDialog(c.get());
        for(auto child:window->GetChildren())if(auto d=dynamic_cast<wxDialog*>(child);d && d->GetTitle()=="Shift Times" && d->IsShown())return d;
        return static_cast<wxDialog*>(nullptr);
    };
    auto shift=open(); ASSERT_NE(nullptr,shift);
    std::vector<FlatSwitch*> switches; FlatList *history=nullptr; TimeEdit *time=nullptr;
    for(auto child:descendants(shift)){
        if(auto s=dynamic_cast<FlatSwitch*>(child))switches.push_back(s);
        if(auto l=dynamic_cast<FlatList*>(child))history=l;
        if(auto t=dynamic_cast<TimeEdit*>(child))time=t;
    }
    ASSERT_EQ(4u,switches.size()); ASSERT_NE(nullptr,history); ASSERT_NE(nullptr,time);
    auto by=switches[0],direction=switches[1],affect=switches[2],fields=switches[3];
    // No timecodes: frames can't be chosen; no selection: only all rows
    EXPECT_FALSE(by->IsItemEnabled(1)); EXPECT_EQ(0,by->GetSelection());
    by->Choose(1); EXPECT_EQ(0,by->GetSelection());
    EXPECT_FALSE(affect->IsItemEnabled(1)); EXPECT_FALSE(affect->IsItemEnabled(2)); EXPECT_EQ(0,affect->GetSelection());
    EXPECT_EQ(1,direction->GetSelection()); EXPECT_EQ(1,fields->GetSelection()) << "the last choices";
    // Alt with an option's letter chooses it
    {wxKeyEvent alt(wxEVT_CHAR_HOOK); alt.m_keyCode='W'; alt.m_altDown=true; alt.SetEventObject(shift); shift->GetEventHandler()->ProcessEvent(alt);}
    EXPECT_EQ(0,direction->GetSelection());
    fields->Choose(0);
    time->SetTime(1500); button(shift,wxID_OK);
    EXPECT_EQ(1500,LiveRead(*c).Events.front().Start.GetMilliseconds()); EXPECT_EQ(6500,LiveRead(*c).Events.front().End.GetMilliseconds());

    // The shift is in the history; choosing it loads it again
    shift=open(); ASSERT_NE(nullptr,shift);
    for(auto child:descendants(shift)){
        if(auto l=dynamic_cast<FlatList*>(child))history=l;
        if(auto t=dynamic_cast<TimeEdit*>(child))time=t;
        if(auto s=dynamic_cast<FlatSwitch*>(child);s && s->GetCount()==2 && s->GetLabel(0)=="For&ward")direction=s;
    }
    ASSERT_EQ(1u,history->GetCount());
    EXPECT_TRUE(history->GetString(0).Contains("0:00:01.50")); EXPECT_TRUE(history->GetString(0).Contains("forward"));
    time->SetTime(0); direction->SetSelection(1);
    history->Click(0);
    EXPECT_EQ(1500,time->GetTime()); EXPECT_EQ(0,direction->GetSelection());
    shift->Close();
}
TEST_F(ordinary_edit, search_and_replace_is_drawn_fields_checks_and_switches) {
    AcceptMessages messages;
    cmd::PasteSubtitleLines(c.get(),"Dialogue: 0,0:00:01.00,0:00:02.00,Default,A,0,0,0,,alpha beta");
    OPT_SET("Tool/Search Replace/Field")->SetInt(0);OPT_SET("Tool/Search Replace/Affect")->SetInt(0);
    OPT_SET("Tool/Search Replace/Match Case")->SetBool(false);OPT_SET("Tool/Search Replace/RegExp")->SetBool(false);
    OPT_SET("Tool/Search Replace/Skip Comments")->SetBool(false);OPT_SET("Tool/Search Replace/Skip Tags")->SetBool(false);
    ShowSearchReplaceDialog(c.get(),true);
    wxDialog *d=nullptr;
    for(auto child:window->GetChildren())if(auto x=dynamic_cast<wxDialog*>(child);x && x->GetTitle()==_("Replace") && x->IsShown())d=x;
    ASSERT_NE(nullptr,d);
    std::vector<wxTextCtrl*> texts; std::vector<FlatSwitch*> switches; std::vector<FlatCheck*> checks; FlatButton *replace_all=nullptr;
    for(auto child:descendants(d)){
        if(auto t=dynamic_cast<wxTextCtrl*>(child))texts.push_back(t);
        if(auto s=dynamic_cast<FlatSwitch*>(child))switches.push_back(s);
        if(auto k=dynamic_cast<FlatCheck*>(child))checks.push_back(k);
        if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()==_("Replace &all"))replace_all=b;
    }
    ASSERT_EQ(2u,texts.size()); ASSERT_EQ(2u,switches.size()); ASSERT_EQ(4u,checks.size()); ASSERT_NE(nullptr,replace_all);
    EXPECT_EQ(0,switches[0]->GetSelection()); EXPECT_EQ(0,switches[1]->GetSelection());
    // Alt with a check box's letter ticks it
    {wxKeyEvent alt(wxEVT_CHAR_HOOK); alt.m_keyCode='M'; alt.m_altDown=true; alt.SetEventObject(d); d->GetEventHandler()->ProcessEvent(alt);}
    EXPECT_TRUE(checks[0]->GetValue());
    texts[0]->SetValue("beta"); texts[1]->SetValue("gamma");
    replace_all->Press();
    bool replaced=false; for(auto const& line:LiveRead(*c).Events)if(line.Text.get()=="alpha gamma")replaced=true;
    EXPECT_TRUE(replaced);
    EXPECT_TRUE(OPT_GET("Tool/Search Replace/Match Case")->GetBool()) << "the choices are kept";
    // Escape presses Close
    {wxKeyEvent esc(wxEVT_CHAR_HOOK); esc.m_keyCode=WXK_ESCAPE; esc.SetEventObject(d); d->GetEventHandler()->ProcessEvent(esc);}
    EXPECT_FALSE(d->IsShown());
}
TEST_F(ordinary_edit, select_lines_is_drawn_switches_and_checks) {
    AcceptMessages messages;
    cmd::PasteSubtitleLines(c.get(),"Dialogue: 0,0:00:01.00,0:00:02.00,Default,A,0,0,0,,one\nDialogue: 0,0:00:02.00,0:00:03.00,Default,B,0,0,0,,two");
    OPT_SET("Tool/Select Lines/Field")->SetInt(0);OPT_SET("Tool/Select Lines/Action")->SetInt(0);
    OPT_SET("Tool/Select Lines/Mode")->SetInt(1);OPT_SET("Tool/Select Lines/Condition")->SetInt(0);
    OPT_SET("Tool/Select Lines/Match/Case")->SetBool(false);OPT_SET("Tool/Select Lines/Match/Dialogue")->SetBool(true);
    OPT_SET("Tool/Select Lines/Match/Comment")->SetBool(false);OPT_SET("Tool/Select Lines/Text")->SetString("");
    ShowSelectLinesDialog(c.get());
    wxDialog *d=nullptr;
    for(auto child:window->GetChildren())if(auto x=dynamic_cast<wxDialog*>(child);x && x->GetTitle()==_("Select") && x->IsShown())d=x;
    ASSERT_NE(nullptr,d);
    std::vector<wxTextCtrl*> texts; std::vector<FlatSwitch*> switches; std::vector<FlatCheck*> checks;
    for(auto child:descendants(d)){
        if(auto t=dynamic_cast<wxTextCtrl*>(child))texts.push_back(t);
        if(auto s=dynamic_cast<FlatSwitch*>(child))switches.push_back(s);
        if(auto k=dynamic_cast<FlatCheck*>(child))checks.push_back(k);
    }
    ASSERT_EQ(1u,texts.size()); ASSERT_EQ(4u,switches.size()); ASSERT_EQ(3u,checks.size());
    auto condition=switches[0],mode=switches[1],field=switches[2],action=switches[3];
    auto dialogue=checks[1],comments=checks[2];
    EXPECT_EQ(0,condition->GetSelection()); EXPECT_EQ(1,mode->GetSelection()); EXPECT_EQ(0,field->GetSelection()); EXPECT_EQ(0,action->GetSelection());
    EXPECT_EQ(3u,mode->GetCount()); EXPECT_EQ(4u,action->GetCount());
    // One kind of line stays ticked
    dialogue->Toggle(); EXPECT_FALSE(dialogue->GetValue()); EXPECT_TRUE(comments->GetValue());
    comments->Toggle(); EXPECT_TRUE(dialogue->GetValue()); EXPECT_FALSE(comments->GetValue());
    // Alt with an option's letter chooses it
    {wxKeyEvent alt(wxEVT_CHAR_HOOK); alt.m_keyCode='D'; alt.m_altDown=true; alt.SetEventObject(d); d->GetEventHandler()->ProcessEvent(alt);}
    EXPECT_EQ(1,condition->GetSelection());
    condition->Choose(0);
    texts[0]->SetValue("two");
    button(d,wxID_OK);
    auto selected=c->selectionController->ReadSortedSelection();
    ASSERT_EQ(1u,selected.size());
    EXPECT_EQ("two",selected.front()->Text.get());
    EXPECT_FALSE(d->IsShown());
    wxTheApp->ProcessIdle(); // the choices are kept as it goes
    EXPECT_EQ("two",OPT_GET("Tool/Select Lines/Text")->GetString());
}
TEST_F(ordinary_edit, paste_over_ticks_fields_in_the_columns_of_a_line) {
    OPT_SET("Tool/Paste Lines Over/Fields")->SetListBool(std::vector<bool>(11,true));
    bool checked=false;
    {
        RunModal hook(_("Select Fields to Paste Over"),[&](wxDialog *dialog){
            std::vector<FlatCheck*> fields; FlatButton *times=nullptr;
            for(auto child:descendants(dialog)){
                if(auto k=dynamic_cast<FlatCheck*>(child))fields.push_back(k);
                if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()==_("&Times"))times=b;
            }
            ASSERT_EQ(11u,fields.size()); ASSERT_NE(nullptr,times);
            for(auto field:fields)EXPECT_TRUE(field->GetValue());
            times->Press();
            for(size_t i=0;i<fields.size();++i)EXPECT_EQ(i==2 || i==3,fields[i]->GetValue()) << i;
            fields[10]->Toggle();
            checked=true;
            button(dialog,wxID_OK);
        });
        EXPECT_TRUE(ShowPasteOverDialog(window));
    }
    EXPECT_TRUE(checked);
    std::vector<bool> expected(11,false); expected[2]=expected[3]=expected[10]=true;
    EXPECT_EQ(expected,OPT_GET("Tool/Paste Lines Over/Fields")->GetListBool());
}
TEST_F(ordinary_edit, jump_to_names_its_fields_and_jumps) {
    c->project->LoadVideo("?dummy:25:250:640:480:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    bool ran=false;
    {
        RunModal hook(_("Jump to"),[&](wxDialog *dialog){
            std::vector<FlatFrame*> frames; wxTextCtrl *frame=nullptr; TimeEdit *time=nullptr;
            for(auto child:descendants(dialog)){
                if(auto f=dynamic_cast<FlatFrame*>(child))frames.push_back(f);
                if(auto t=dynamic_cast<TimeEdit*>(child))time=t;
                else if(auto t=dynamic_cast<wxTextCtrl*>(child))frame=t;
            }
            ASSERT_EQ(2u,frames.size()); ASSERT_NE(nullptr,frame); ASSERT_NE(nullptr,time);
            frame->SetValue("100");
            EXPECT_EQ(4000,time->GetTime());
            ran=true;
            button(dialog,wxID_OK);
        });
        ShowJumpToDialog(c.get());
    }
    EXPECT_TRUE(ran);
    EXPECT_EQ(100,c->videoController->GetFrameN());
}
TEST_F(ordinary_edit, translation_assistant_is_drawn_with_titles_and_foot_buttons) {
    cmd::PasteSubtitleLines(c.get(),"Dialogue: 0,0:00:01.00,0:00:02.00,Default,A,0,0,0,,hello");
    bool ran=false;
    {
        RunModal hook(_("Translation Assistant"),[&](wxDialog *dialog){
            FlatCheck *preview=nullptr; std::vector<FlatButton*> buttons;
            for(auto child:descendants(dialog)){
                if(auto k=dynamic_cast<FlatCheck*>(child))preview=k;
                if(auto b=dynamic_cast<FlatButton*>(child))buttons.push_back(b);
            }
            ASSERT_NE(nullptr,preview); EXPECT_TRUE(preview->GetValue());
            ASSERT_EQ(4u,buttons.size());
            for(auto b:buttons){
                if(b->GetLabel()==_("Play &Audio"))EXPECT_FALSE(b->IsEnabled()) << "no audio";
                if(b->GetLabel()==_("Play &Video"))EXPECT_FALSE(b->IsEnabled()) << "no video";
            }
            // Escape presses Cancel
            wxKeyEvent esc(wxEVT_CHAR_HOOK); esc.m_keyCode=WXK_ESCAPE; esc.SetEventObject(dialog); dialog->GetEventHandler()->ProcessEvent(esc);
            EXPECT_FALSE(dialog->IsShown());
            ran=true;
        });
        cmd::call("tool/translation_assistant",c.get());
    }
    EXPECT_TRUE(ran);
}
TEST_F(ordinary_edit, controlled_shift_and_timing_processor_use_real_dialog_confirm_events) {
    OPT_SET("Tool/Shift Times/ByTime")->SetBool(true);OPT_SET("Tool/Shift Times/Direction")->SetBool(false);
    OPT_SET("Tool/Shift Times/Type")->SetInt(0);OPT_SET("Tool/Shift Times/Affect")->SetInt(0);
    ShowShiftTimesDialog(c.get());
    wxDialog* shift=nullptr;
    for(auto child:window->GetChildren())if(auto dialog=dynamic_cast<wxDialog*>(child);dialog && dialog->GetTitle()=="Shift Times")shift=dialog;
    ASSERT_NE(nullptr,shift);
    TimeEdit* value=nullptr;for(auto child:descendants(shift))if(auto time=dynamic_cast<TimeEdit*>(child))value=time;
    ASSERT_NE(nullptr,value);value->SetTime(1000);button(shift,wxID_OK);
    EXPECT_EQ(1000,LiveRead(*c).Events.front().Start.GetMilliseconds());
    OPT_SET("Tool/Timing Post Processor/Lead/IN")->SetInt(100);
    OPT_SET("Tool/Timing Post Processor/Enable/Lead/IN")->SetBool(true);
    OPT_SET("Tool/Timing Post Processor/Enable/Lead/OUT")->SetBool(false);
    OPT_SET("Tool/Timing Post Processor/Enable/Adjacent")->SetBool(false);
    OPT_SET("Tool/Timing Post Processor/Enable/Keyframe")->SetBool(false);
    {
        RunModal hook("Timing Post-Processor",[&](wxDialog* dialog){
            for(auto child:descendants(dialog))if(auto list=dynamic_cast<FlatList*>(child))for(unsigned i=0;i<list->GetCount();++i)list->Check(i,true);
            button(dialog,wxID_OK);
        });
        ShowTimingProcessorDialog(c.get());EXPECT_EQ(900,LiveRead(*c).Events.front().Start.GetMilliseconds());
    }
    c->subsController->Undo();EXPECT_EQ(1000,LiveRead(*c).Events.front().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(0,LiveRead(*c).Events.front().Start.GetMilliseconds());
}
/// The text fields of @p dialog named @p label in their frames, in order
std::vector<wxTextCtrl*> fields_named(wxWindow *dialog, wxString const& label) {
    std::vector<wxTextCtrl*> found;
    for(auto child:descendants(dialog))if(auto frame=dynamic_cast<FlatFrame*>(child);frame && frame->GetLabel()==label)
        for(auto inner:frame->GetChildren())if(auto text=dynamic_cast<wxTextCtrl*>(inner))found.push_back(text);
    return found;
}
FlatButton *button_labelled(wxWindow *dialog, wxString const& label) {
    for(auto child:descendants(dialog))if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()==label)return b;
    return nullptr;
}
TEST_F(ordinary_edit, timing_processor_is_drawn_ticks_and_millisecond_fields) {
    OPT_SET("Tool/Timing Post Processor/Lead/IN")->SetInt(100);
    OPT_SET("Tool/Timing Post Processor/Enable/Lead/IN")->SetBool(true);
    OPT_SET("Tool/Timing Post Processor/Enable/Lead/OUT")->SetBool(false);
    OPT_SET("Tool/Timing Post Processor/Enable/Adjacent")->SetBool(false);
    OPT_SET("Tool/Timing Post Processor/Enable/Keyframe")->SetBool(false);
    bool ran=false;
    {
        RunModal hook("Timing Post-Processor",[&](wxDialog* dialog){
            FlatList *styles=nullptr; FlatCheck *lead_out=nullptr,*keys=nullptr;
            for(auto child:descendants(dialog)){
                if(auto l=dynamic_cast<FlatList*>(child))styles=l;
                if(auto k=dynamic_cast<FlatCheck*>(child);k && k->GetLabel()==_("Add lead &out:").BeforeLast(':'))lead_out=k;
                if(auto k=dynamic_cast<FlatCheck*>(child);k && k->GetLabel()==_("E&nable") )keys=k;
            }
            ASSERT_NE(nullptr,styles); ASSERT_NE(nullptr,lead_out); ASSERT_NE(nullptr,keys);
            for(unsigned i=0;i<styles->GetCount();++i)EXPECT_TRUE(styles->IsChecked(i)) << "every style to begin with";
            EXPECT_FALSE(keys->IsEnabled()) << "no keyframes";
            auto detect=button_labelled(dialog,_("&Detect Scene Changes")); ASSERT_NE(nullptr,detect);
            EXPECT_FALSE(detect->IsEnabled()) << "no video to find scene changes in";
            auto leads=fields_named(dialog,_("Lead")); ASSERT_EQ(2u,leads.size());
            EXPECT_EQ("100",leads[0]->GetValue()); EXPECT_FALSE(leads[1]->IsEnabled());
            lead_out->Toggle(); EXPECT_TRUE(leads[1]->IsEnabled()) << "a field is used while its box is ticked";
            // Nothing to process, nothing to accept
            auto ok=button_labelled(dialog,flat::StockLabel(wxID_OK)); ASSERT_NE(nullptr,ok);
            button_labelled(dialog,_("&None"))->Press(); EXPECT_FALSE(ok->IsEnabled());
            button_labelled(dialog,_("&All"))->Press(); EXPECT_TRUE(ok->IsEnabled());
            leads[0]->SetValue("200");
            ran=true;
            button(dialog,wxID_OK);
        });
        ShowTimingProcessorDialog(c.get());
    }
    EXPECT_TRUE(ran);
    EXPECT_EQ(200,OPT_GET("Tool/Timing Post Processor/Lead/IN")->GetInt());
    EXPECT_TRUE(OPT_GET("Tool/Timing Post Processor/Enable/Lead/OUT")->GetBool());
}
TEST_F(ordinary_edit, resample_is_drawn_number_fields_a_switch_and_even_margins) {
    int width,height;LiveRead(*c).GetResolution(width,height);
    bool ran=false;
    RunModal hook("Resample Resolution",[&](wxDialog* dialog){
        FlatSwitch *ar=nullptr; for(auto child:descendants(dialog))if(auto s=dynamic_cast<FlatSwitch*>(child))ar=s;
        ASSERT_NE(nullptr,ar); EXPECT_EQ(4u,ar->GetCount());
        EXPECT_FALSE(ar->IsEnabled()) << "the same aspect ratio";
        auto widths=fields_named(dialog,wxGETTEXT_IN_CONTEXT("resolution","Width")); ASSERT_EQ(2u,widths.size());
        EXPECT_EQ(wxString::Format("%d",width),widths[0]->GetValue());
        widths[1]->SetValue(wxString::Format("%d",width*2));
        EXPECT_TRUE(ar->IsEnabled());
        auto left=fields_named(dialog,_("Left")),right=fields_named(dialog,_("Right"));
        ASSERT_EQ(1u,left.size()); ASSERT_EQ(1u,right.size());
        EXPECT_FALSE(left[0]->IsEnabled());
        ar->Choose(3);
        EXPECT_TRUE(left[0]->IsEnabled()); EXPECT_FALSE(right[0]->IsEnabled()) << "symmetrical";
        left[0]->SetValue("12"); EXPECT_EQ("12",right[0]->GetValue());
        ran=true;
        button(dialog,wxID_CANCEL);
    });
    cmd::call("tool/resampleres",c.get());
    EXPECT_TRUE(ran);
    int after_width,after_height;LiveRead(*c).GetResolution(after_width,after_height);
    EXPECT_EQ(width,after_width) << "cancelled"; EXPECT_EQ(height,after_height);
}
TEST_F(ordinary_edit, spell_checker_is_drawn_fields_a_list_and_buttons) {
    AcceptMessages messages;
    cmd::PasteSubtitleLines(c.get(),"Dialogue: 0,0:00:01.00,0:00:02.00,Default,A,0,0,0,,Helo");
    try{ShowSpellcheckerDialog(c.get());}catch(...){}
    wxDialog *d=nullptr;
    for(auto child:window->GetChildren())if(auto x=dynamic_cast<wxDialog*>(child);x && x->GetTitle()==_("Spell Checker") && x->IsShown())d=x;
    if(!d)GTEST_SKIP() << "no spelling dictionary";
    auto orig=fields_named(d,_("Misspelled word:").BeforeLast(':')),replace=fields_named(d,_("Replace with:").BeforeLast(':'));
    ASSERT_EQ(1u,orig.size()); ASSERT_EQ(1u,replace.size());
    EXPECT_FALSE(orig[0]->GetValue().empty());
    FlatList *suggestions=nullptr; for(auto child:descendants(d))if(auto l=dynamic_cast<FlatList*>(child))suggestions=l;
    ASSERT_NE(nullptr,suggestions);
    if(suggestions->GetCount()>1){suggestions->Click(1);EXPECT_EQ(suggestions->GetString(1),replace[0]->GetValue()) << "a click takes a suggestion";}
    EXPECT_NE(nullptr,button_labelled(d,_("&Replace")));
    {wxKeyEvent esc(wxEVT_CHAR_HOOK); esc.m_keyCode=WXK_ESCAPE; esc.SetEventObject(d); d->GetEventHandler()->ProcessEvent(esc);}
    EXPECT_FALSE(d->IsShown());
}
TEST_F(ordinary_edit, fonts_collector_chooses_an_action_from_a_list) {
    OPT_SET("Tool/Fonts Collector/Action")->SetInt(0);
    ShowFontsCollectorDialog(c.get());
    wxDialog *d=nullptr;
    for(auto child:window->GetChildren())if(auto x=dynamic_cast<wxDialog*>(child);x && x->GetTitle()==_("Fonts Collector") && x->IsShown())d=x;
    ASSERT_NE(nullptr,d);
    FlatChoice *action=nullptr; for(auto child:descendants(d))if(auto ch=dynamic_cast<FlatChoice*>(child))action=ch;
    ASSERT_NE(nullptr,action);
    for(auto const& item:action->GetItems())EXPECT_NE(_("Copy fonts to subtitle file's folder"),item) << "the subtitles have no folder";
    auto dest=fields_named(d,_("Destination")); ASSERT_EQ(1u,dest.size());
    EXPECT_FALSE(dest[0]->IsEnabled());
    action->Choose(1);
    EXPECT_EQ(1,OPT_GET("Tool/Fonts Collector/Action")->GetInt()); EXPECT_TRUE(dest[0]->IsEnabled());
    EXPECT_NE(nullptr,button_labelled(d,_("&Start!")));
    d->Close();
    EXPECT_FALSE(d->IsShown());
}
TEST(flat_text, labels_lose_their_colon_however_translated) {
    EXPECT_EQ(wxString("Input framerate"),flat::WithoutColon("Input framerate: "));
    EXPECT_EQ(wxString(L"\u8f38\u5165\u5f71\u683c\u7387"),flat::WithoutColon(L"\u8f38\u5165\u5f71\u683c\u7387\uff1a")) << "full-width colon";
    EXPECT_EQ(wxString("Invoer-beeldsnelheid"),flat::WithoutColon("Invoer-beeldsnelheid")) << "no colon at all";
    EXPECT_EQ(wxString("&Constant"),flat::WithoutColon("&Constant: "));
    EXPECT_EQ(wxString("One two. Three"),flat::OneLine("One two.\n\nThree\r\n"));
}
TEST_F(ordinary_edit, scroll_effect_form_sets_the_effect_field_in_one_step) {
    int w,h;LiveRead(*c).GetResolution(w,h);
    auto effect=[&]{return LiveRead(*c).Events.front().Effect.get();};
    auto kind_switch=[](wxDialog* d){FlatSwitch* found=nullptr;for(auto child:descendants(d))if(auto s=dynamic_cast<FlatSwitch*>(child);s && s->GetLabel(0)==wxGETTEXT_IN_CONTEXT("scroll effect","&None"))found=s;return found;};
    bool ran=false;
    {
        RunModal hook("Scroll Effect",[&](wxDialog* d){
            auto kind=kind_switch(d);ASSERT_NE(nullptr,kind);EXPECT_EQ(0,kind->GetSelection()) << "none yet";
            kind->Choose(2);
            auto delay=fields_named(d,_("Delay"));ASSERT_EQ(1u,delay.size());delay[0]->SetValue("40");
            ran=true;button(d,wxID_OK);
        });
        box->EditScrollEffect();
    }
    EXPECT_TRUE(ran);
    ScrollEffect expected;expected.y1=h/2;expected.y2=h;expected.delay=40;
    EXPECT_EQ(FormatScrollEffect(expected),effect());
    EXPECT_EQ(_("effect change"),c->subsController->GetUndoDescription());
    c->subsController->Undo();EXPECT_EQ("",effect()) << "one step";
    // Cancelled, nothing changes
    {RunModal hook("Scroll Effect",[&](wxDialog* d){kind_switch(d)->Choose(1);button(d,wxID_CANCEL);});box->EditScrollEffect();}
    EXPECT_EQ("",effect());
    // Accepted as it was, the field is left as written; None clears it
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Effect="Banner;20";LiveWriteForTest(*c).Commit("banner",AssFile::COMMIT_DIAG_META);}
    box->SetFocus();wxTheApp->ProcessPendingEvents();
    {RunModal hook("Scroll Effect",[&](wxDialog* d){EXPECT_EQ(1,kind_switch(d)->GetSelection());button(d,wxID_OK);});box->EditScrollEffect();}
    EXPECT_EQ("Banner;20",effect());EXPECT_EQ("banner",c->subsController->GetUndoDescription());
    {RunModal hook("Scroll Effect",[&](wxDialog* d){kind_switch(d)->Choose(0);button(d,wxID_OK);});box->EditScrollEffect();}
    EXPECT_EQ("",effect());
    // Anything else stays unless an effect is chosen
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Effect="template line";LiveWriteForTest(*c).Commit("template",AssFile::COMMIT_DIAG_META);}
    {RunModal hook("Scroll Effect",[&](wxDialog* d){EXPECT_EQ(0,kind_switch(d)->GetSelection());button(d,wxID_OK);});box->EditScrollEffect();}
    EXPECT_EQ("template line",effect());
}
TEST_F(ordinary_edit, export_lists_filters_with_their_descriptions) {
    // Each filter registered here, if it is not already, is gone again after
    std::vector<AssExportFilter*> registered;
    auto need=[&](wxString const& name,auto make){if(!AssExportFilterChain::GetFilter(from_wx(name))){AssExportFilterChain::Register(make());registered.push_back(AssExportFilterChain::GetFilter(from_wx(name)));}};
    need(_("Fix Styles"),[]{return std::make_unique<AssFixStylesFilter>();});
    need(_("Transform Framerate"),[]{return std::make_unique<AssTransformFramerateFilter>();});
    BOOST_SCOPE_EXIT_ALL(&){for(auto filter:registered)delete filter;};
    // None ticked to begin with, whatever was remembered
    c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::ExportFilters,"");
    bool ran=false;
    RunModal hook("Export",[&](wxDialog* dialog){
        FlatList *filters=nullptr; for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child))filters=l;
        ASSERT_NE(nullptr,filters); ASSERT_LE(2u,filters->GetCount());
        for(unsigned i=0;i<filters->GetCount();++i)EXPECT_FALSE(filters->GetRow(i).detail.empty()) << "described under its name";
        int framerate=-1; for(unsigned i=0;i<filters->GetCount();++i)if(filters->GetString(i)==_("Transform Framerate"))framerate=i;
        ASSERT_LE(0,framerate);
        FlatSwitch *output=nullptr; for(auto child:descendants(dialog))if(auto s=dynamic_cast<FlatSwitch*>(child))output=s;
        ASSERT_NE(nullptr,output); EXPECT_FALSE(output->GetParent()->IsShown()) << "settings shown while ticked";
        ASSERT_FALSE(filters->IsChecked(framerate));
        filters->Click(framerate,false,false,true);
        EXPECT_TRUE(filters->IsChecked(framerate)); EXPECT_TRUE(output->GetParent()->IsShown());
        // A row's words are whole in the tip, its description cut short as it may be
        {wxMouseEvent move(wxEVT_MOTION); move.SetPosition(wxPoint(filters->FromDIP(40),filters->FromDIP(12))); move.SetEventObject(filters); filters->GetEventHandler()->ProcessEvent(move);}
        EXPECT_TRUE(filters->GetToolTipText().Contains(filters->GetRow(0).detail));
        // Moving a filter takes its tick along
        wxString name=filters->GetString(0); bool ticked=filters->IsChecked(0);
        filters->SetSelection(0); button_labelled(dialog,_("Move &Down"))->Press();
        EXPECT_EQ(name,filters->GetString(1)); EXPECT_EQ(ticked,filters->IsChecked(1)); EXPECT_EQ(1,filters->GetSelection());
        ran=true;
        button(dialog,wxID_CANCEL);
    });
    ShowExportDialog(c.get());
    EXPECT_TRUE(ran);
}
TEST_F(ordinary_edit, controlled_resample_uses_real_settings_confirmation_and_one_history_step) {
    Input("{\\pos(10,20)}sample");
    int width,height;LiveRead(*c).GetResolution(width,height);auto fontsize=LiveRead(*c).GetStyle("Default")->fontsize;
    RunModal hook("Resample Resolution",[&](wxDialog* dialog){
        // Width and height of the source, then of the destination
        std::vector<wxTextCtrl*> sizes;
        for(auto child:descendants(dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child);text && dynamic_cast<FlatFrame*>(text->GetParent()))sizes.push_back(text);
        ASSERT_LE(4u,sizes.size());
        for(int i:{2,3}){long v=0;ASSERT_TRUE(sizes[i]->GetValue().ToLong(&v));sizes[i]->SetValue(wxString::Format("%ld",v*2));}
        button(dialog,wxID_OK);
    });
    cmd::call("tool/resampleres",c.get());
    EXPECT_EQ(width*2,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));EXPECT_EQ(height*2,LiveRead(*c).GetScriptInfoAsInt("PlayResY"));
    EXPECT_DOUBLE_EQ(fontsize*2,LiveRead(*c).GetStyle("Default")->fontsize);
    c->subsController->Undo();int restored_width,restored_height;LiveRead(*c).GetResolution(restored_width,restored_height);
    EXPECT_EQ(width,restored_width);EXPECT_EQ(height,restored_height);EXPECT_DOUBLE_EQ(fontsize,LiveRead(*c).GetStyle("Default")->fontsize);
    EXPECT_EQ("{\\pos(10,20)}sample",LiveRead(*c).Events.front().Text.get());
}

TEST_F(ordinary_edit, selection_notifications_refresh_real_editor_without_model_authority) {
    int first=c->selectionController->ActiveID(), second=0;
    {
        auto write=c->subtitleAccess->TryWrite();
        auto row=new AssDialogue;row->Text="second selection";second=row->Id;
        LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("selection fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    auto undo=c->subsController->GetUndoDescription();auto revision=c->subtitleEditGate.Revision();
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    std::vector<char> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&]{order.push_back('S');});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&] {
        order.push_back('A');EXPECT_FALSE(c->subtitleAccess->TryWrite());
        c->subsController->Undo(); // History is also read-only during delivery.
    });
    c->selectionController->SetSelectionAndActiveIDs({second},second,false);
    EXPECT_EQ("second selection",std::string(editor->GetTextRaw().data()));
    c->selectionController->SetSelectionAndActiveIDs({second},second,false);
    EXPECT_EQ((std::vector<char>{'S','A','S'}),order);
    EXPECT_EQ(second,c->selectionController->ActiveID());
    EXPECT_EQ(undo,c->subsController->GetUndoDescription());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
    c->selectionController->SetActiveID(first);
    EXPECT_EQ("",std::string(editor->GetTextRaw().data()));
    EXPECT_EQ((std::set<int>{second}),c->selectionController->SelectedIDs());
}

namespace {
class selection_ui : public ordinary_edit {
protected:
    BaseGrid* grid=nullptr;
    int height=0;
    std::vector<int> ids;
    void SetUp() override {
        ordinary_edit::SetUp();
        grid=new BaseGrid(window,c.get());c->subsGrid=grid;
        grid->SetSize(900,300);window->SetSize(1000,600);window->Show();
        {
            auto write=c->subtitleAccess->TryWrite();
            auto& first=LiveWriteForTest(*c).Events.front();first.Text="first";first.Start=1000;
            ids.push_back(first.Id);
            for(int i=1;i<4;++i) {
                auto row=new AssDialogue;row->Text=std::to_string(i);row->Start=(i+1)*1000;
                LiveWriteForTest(*c).Events.push_back(*row);ids.push_back(row->Id);
            }
            LiveWriteForTest(*c).Commit("selection UI fixture",AssFile::COMMIT_DIAG_ADDREM|AssFile::COMMIT_DIAG_FULL);
        }
        c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0],false);
        height=grid->LineHeight();
    }
    void TearDown() override {
        if(grid->HasCapture())grid->ReleaseMouse();
        c->subsGrid=nullptr;ordinary_edit::TearDown();
    }
    void Mouse(int row,bool ctrl=false,bool shift=false,bool alt=false) {
        wxMouseEvent down(wxEVT_LEFT_DOWN);down.SetPosition({300,(row+1)*height+2});
        down.SetControlDown(ctrl);down.SetShiftDown(shift);down.SetAltDown(alt);
        down.SetEventObject(grid);grid->GetEventHandler()->ProcessEvent(down);
        wxMouseEvent up(wxEVT_LEFT_UP);up.SetPosition(down.GetPosition());up.SetEventObject(grid);
        grid->GetEventHandler()->ProcessEvent(up);
    }
    void Key(int key,bool shift=false,bool alt=false) {
        wxKeyEvent event(wxEVT_KEY_DOWN);event.m_keyCode=key;event.SetShiftDown(shift);event.SetAltDown(alt);
        event.SetEventObject(grid);grid->GetEventHandler()->ProcessEvent(event);
    }
};
TEST_F(selection_ui, mouse_keyboard_and_select_all_are_pure_ui_during_comparison) {
    auto before=c->subtitleEditGate.Revision();auto undo=c->subsController->GetUndoDescription();
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    Mouse(1);EXPECT_EQ(ids[1],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[1]}),c->selectionController->SelectedIDs());
    EXPECT_EQ("1",std::string(editor->GetTextRaw().data()));
    Mouse(3,true);EXPECT_EQ((std::set<int>{ids[1],ids[3]}),c->selectionController->SelectedIDs());
    Key(WXK_UP,false,true);EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[1],ids[3]}),c->selectionController->SelectedIDs());
    Key(WXK_UP);EXPECT_EQ((std::set<int>{ids[1]}),c->selectionController->SelectedIDs());
    cmd::call("subtitle/select/all",c.get());EXPECT_EQ(4u,c->selectionController->SelectedIDs().size());
    EXPECT_EQ(before,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
}
TEST_F(selection_ui, bound_row_modifiers_keep_single_member_and_repeat_notifications) {
    {
        auto write=c->subtitleAccess->TryWrite();
        auto group=LiveWriteForTest(*c).AddExtradata("aegisub.enhanced.subtitle-binding","0:test");
        LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{group};
        std::next(LiveWriteForTest(*c).Events.begin())->ExtradataIds=std::vector<uint32_t>{group};
        LiveWriteForTest(*c).Commit("binding fixture",AssFile::COMMIT_EXTRADATA);
    }
    auto revision=c->subtitleEditGate.Revision();auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    Mouse(0);EXPECT_EQ((std::set<int>{ids[0],ids[1]}),c->selectionController->SelectedIDs());
    Mouse(1,true);EXPECT_EQ((std::set<int>{ids[1]}),c->selectionController->SelectedIDs());
    Mouse(0,false,false,true);EXPECT_EQ((std::set<int>{ids[0]}),c->selectionController->SelectedIDs());
    int notices=0;agi::signal::Connection selected=c->selectionController->AddSelectionListener([&]{++notices;});
    Mouse(0);Mouse(0);EXPECT_EQ(2,notices);
    EXPECT_EQ((std::set<int>{ids[0],ids[1]}),c->selectionController->SelectedIDs());
    EXPECT_EQ(revision,c->subtitleEditGate.Revision());
}
TEST_F(selection_ui, range_anchor_survives_sort_by_identity_and_missing_anchor_is_safe) {
    Mouse(1);Key(WXK_DOWN,true);
    EXPECT_EQ((std::set<int>{ids[1],ids[2]}),c->selectionController->SelectedIDs());
    {
        auto write=c->subtitleAccess->TryWrite();
        SubtitleRowListEdit staged(c.get());
        staged.file.Events.reverse();staged.Commit("reverse fixture",AssFile::COMMIT_ORDER);
    }
    // Current row 2 moved to position 1; anchor row 1 moved to position 2.
    Key(WXK_UP,true);
    EXPECT_EQ(ids[3],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[3],ids[2],ids[1]}),c->selectionController->SelectedIDs());
    {
        auto write=c->subtitleAccess->TryWrite();SubtitleRowListEdit staged(c.get());
        for(auto it=staged.file.Events.begin();it!=staged.file.Events.end();++it)if(it->Id==ids[1]) {
            staged.file.Events.erase_and_dispose(it,[](auto row){delete row;});break;
        }
        staged.Commit("delete anchor fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    Key(WXK_DOWN,true);
    EXPECT_EQ((std::set<int>{ids[3],ids[2]}),c->selectionController->SelectedIDs());
}
TEST_F(selection_ui, undo_redo_rebind_editor_grid_and_bound_selection_with_same_ids) {
    c->selectionController->SetSelectionAndActiveIDs({ids[1],ids[2]},ids[2],false);
    c->textSelectionController->SetSelection(0,1);
    Input("both edited");
    auto selected=c->selectionController->SelectedIDs();
    std::vector<char> order;
    agi::signal::Connection selection=c->selectionController->AddSelectionListener([&]{order.push_back('S');});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&]{order.push_back('A');});
    c->subsController->Undo();
    EXPECT_EQ((std::vector<char>{'S','A'}),order);EXPECT_EQ(selected,c->selectionController->SelectedIDs());
    EXPECT_EQ(ids[2],c->selectionController->ActiveID());EXPECT_EQ("2",std::string(editor->GetTextRaw().data()));
    order.clear();c->subsController->Redo();
    EXPECT_EQ((std::vector<char>{'S','A'}),order);EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ("both edited",std::string(editor->GetTextRaw().data()));
    EXPECT_EQ(selected,c->selectionController->SelectedIDs());
    Key(WXK_UP);EXPECT_EQ(ids[1],c->selectionController->ActiveID());
}
TEST_F(selection_ui, selection_listener_cannot_undo_or_commit_with_a_preexisting_scope) {
    Input("history entry");auto before=c->subsController->GetUndoDescription();
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto edit=c->subtitleAccess->Edit(scope->Lease());
    auto revision=c->subtitleEditGate.Revision();int calls=0;
    agi::signal::Connection observer=c->selectionController->AddSelectionListener([&]{
        ++calls;c->subsController->Undo();c->subsController->Redo();
        EXPECT_FALSE(c->subtitleAccess->TryWrite());
        EXPECT_THROW(edit.Commit("reentry",AssFile::COMMIT_DIAG_META),std::logic_error);
    });
    c->selectionController->SetSelectedIDs({ids[0]});
    EXPECT_EQ(1,calls);EXPECT_EQ(before,c->subsController->GetUndoDescription());
    EXPECT_EQ("history entry",LiveRead(*c).Events.front().Text.get());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
}
TEST_F(selection_ui, find_navigation_uses_ids_without_a_write_lease) {
    SearchReplaceSettings settings{};settings.find="2";settings.field=SearchReplaceSettings::Field::TEXT;
    settings.limit_to=SearchReplaceSettings::Limit::ALL;c->search->Configure(settings);
    auto before=c->subtitleEditGate.Revision();auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    EXPECT_TRUE(c->search->FindNext());EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[2]}),c->selectionController->SelectedIDs());
    EXPECT_EQ(before,c->subtitleEditGate.Revision());
}
}

TEST_F(ordinary_edit, save_cleans_only_independent_copy_and_keeps_ass_format_precision) {
    {
        auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();
        row.Text=" 中 文 {\\i1} ";row.Comment=true;row.Start=1001;row.End=2345;
        auto binding=LiveWriteForTest(*c).AddExtradata("aegisub.enhanced.subtitle-binding","0:save-test");
        LiveWriteForTest(*c).AddExtradata("orphan","keep in memory");
        row.ExtradataIds=std::vector<uint32_t>{binding,999999};
        LiveWriteForTest(*c).Properties.video_zoom=1.25;LiveWriteForTest(*c).Properties.automation_settings["test"]="saved";
        LiveWriteForTest(*c).Commit("save fixture",AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_EXTRADATA);
    }
    auto id=LiveRead(*c).Events.front().Id;auto refs=LiveRead(*c).Events.front().ExtradataIds.get();
    auto revision=c->subtitleEditGate.Revision();auto undo=c->subsController->GetUndoDescription();
    auto path=agi::fs::path((std::filesystem::current_path()/"snapshot-save.ass").wstring());
    std::filesystem::remove(std::filesystem::path(path.wstring())); // left by an earlier run
    {
        // C1-C1c-12: saving during a comparison is refused before any output.
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        EXPECT_THROW(c->subsController->Save(path,"utf-8"),SubtitleProjectBusy);
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path.wstring())));
    }
    c->subsController->Save(path,"utf-8");
    // Saving works on a snapshot: no row revision, history or live extradata change.
    EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
    EXPECT_EQ(refs,LiveRead(*c).Events.front().ExtradataIds.get());EXPECT_EQ(2u,LiveRead(*c).Extradata.size());
    EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    AssFile reopened;SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    std::ifstream saved(path.string(),std::ios::binary);
    std::string output((std::istreambuf_iterator<char>(saved)),{});
    EXPECT_NE(std::string::npos,output.find(" 中 文 {\\i1} "));
    // The existing ASS reader trims line-end space; ASS time serialization rounds to centiseconds.
    EXPECT_EQ(" 中 文 {\\i1}",reopened.Events.front().Text.get());EXPECT_TRUE(reopened.Events.front().Comment);
    EXPECT_EQ(1000,reopened.Events.front().Start.GetMilliseconds());EXPECT_EQ(2350,reopened.Events.front().End.GetMilliseconds());
    EXPECT_EQ(1001,LiveRead(*c).Events.front().Start.GetMilliseconds());EXPECT_EQ(2345,LiveRead(*c).Events.front().End.GetMilliseconds());
    ASSERT_EQ(1u,reopened.Extradata.size());EXPECT_EQ("aegisub.enhanced.subtitle-binding",reopened.Extradata.front().key);
    EXPECT_EQ(1u,reopened.Events.front().ExtradataIds.get().size());EXPECT_NE(nullptr,reopened.GetStyle("Default"));
    EXPECT_DOUBLE_EQ(1.25,reopened.Properties.video_zoom);
}
TEST_F(ordinary_edit, failed_save_leaves_filename_paths_dirty_state_model_and_history_intact) {
    Input("unsaved");auto old=c->subsController->Filename();auto revision=c->subtitleEditGate.Revision();
    auto undo=c->subsController->GetUndoDescription();auto script=c->path->Decode("?script");
    auto path=agi::fs::path((std::filesystem::current_path()/"missing-parent"/"failed.ass").wstring());
    EXPECT_ANY_THROW(c->subsController->Save(path,"utf-8"));
    EXPECT_EQ(old,c->subsController->Filename());EXPECT_EQ(script,c->path->Decode("?script"));EXPECT_TRUE(c->subsController->IsModified());
    EXPECT_EQ("unsaved",LiveRead(*c).Events.front().Text.get());EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
}

TEST_F(ordinary_edit, autosave_owns_complete_input_and_queued_completion_survives_controller_destruction) {
    auto directory=std::filesystem::current_path()/"autosave-snapshot";
    std::filesystem::remove_all(directory); // left by an earlier run
    auto old_path=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString(directory.string());
    Input("automatic snapshot 中 文");
    {
        auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Properties.video_zoom=1.75;
        LiveWriteForTest(*c).Properties.export_encoding="utf-8";
        auto id=LiveWriteForTest(*c).AddExtradata("aegisub.enhanced.subtitle-binding","0:auto");
        LiveWriteForTest(*c).Events.front().ExtradataIds=std::vector<uint32_t>{id};LiveWriteForTest(*c).AddExtradata("orphan","not saved");
        LiveWriteForTest(*c).Commit("auto fixture",AssFile::COMMIT_EXTRADATA);
    }
    c->subsController->RequestAutoSave();Input("later edit");
    // The production destructor drains its serial worker and invalidates queued UI results.
    c->subsController.reset();wxTheApp->ProcessPendingEvents();
    std::vector<std::filesystem::path> outputs;
    for(auto const& item:std::filesystem::directory_iterator(directory))outputs.push_back(item.path());
    ASSERT_EQ(1u,outputs.size());AssFile reopened;agi::fs::path path(outputs.front().wstring());
    SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("automatic snapshot 中 文",reopened.Events.front().Text.get());EXPECT_EQ("later edit",LiveRead(*c).Events.front().Text.get());
    EXPECT_DOUBLE_EQ(1.75,reopened.Properties.video_zoom);EXPECT_EQ("utf-8",reopened.Properties.export_encoding);
    EXPECT_EQ(1u,reopened.Extradata.size());EXPECT_EQ(2u,LiveRead(*c).Extradata.size());
    OPT_SET("Path/Auto/Save")->SetString(old_path);
}

TEST_F(ordinary_edit, metadata_only_save_close_reopen_has_independent_dirty_state_and_compatible_ass_fields) {
    using Field=SubtitleProjectAccess::MetadataField;
    auto path=agi::fs::path((std::filesystem::current_path()/"metadata-only.ass").wstring());
    c->subsController->Save(path,"utf-8");ASSERT_FALSE(c->subsController->IsModified());
    auto revision=c->subtitleEditGate.Revision();auto undo=c->subsController->GetUndoDescription();
    auto ui=c->subtitleAccess->UIState();ui.video_zoom=1.5;c->subtitleAccess->UpdateUIState(ui);
    EXPECT_FALSE(c->subsController->IsModified());
    c->subtitleAccess->SetMetadata(Field::ExportEncoding,"utf-8");c->subtitleAccess->SetAutomationSetting("test","some settings");
    EXPECT_TRUE(c->subsController->IsModified());EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(undo,c->subsController->GetUndoDescription());
    auto bad=agi::fs::path((std::filesystem::current_path()/"no-parent-metadata"/"failed.ass").wstring());
    EXPECT_ANY_THROW(c->subsController->Save(bad,"utf-8"));EXPECT_TRUE(c->subtitleAccess->MetadataDirty());
    c->subsController->Save(path,"utf-8");EXPECT_FALSE(c->subsController->IsModified());
    c->subsController->Close();c->subsController->Load(path,"utf-8");
    auto id=LiveRead(*c).Events.front().Id;c->selectionController->SetSelectionAndActiveIDs({id},id);
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_EQ("utf-8",LiveRead(*c).Properties.export_encoding);
    EXPECT_EQ("some settings",c->subtitleAccess->AutomationSetting("test"));EXPECT_DOUBLE_EQ(1.5,LiveRead(*c).Properties.video_zoom);
}
TEST_F(ordinary_edit, metadata_only_change_triggers_autosave_after_a_clean_manual_save) {
    auto root=std::filesystem::current_path()/"metadata-autosave";
    std::filesystem::remove_all(root); // left by an earlier run
    auto old=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString(root.string());
    c->subsController->Save(agi::fs::path((std::filesystem::current_path()/"clean-metadata.ass").wstring()),"utf-8");
    auto revision=c->subtitleEditGate.Revision();
    c->subtitleAccess->SetAutomationSetting("test","metadata only");c->subsController->RequestAutoSave();
    EXPECT_EQ(revision,c->subtitleEditGate.Revision());c->subsController.reset();wxTheApp->ProcessPendingEvents();
    std::vector<std::filesystem::path> files;for(auto const& item:std::filesystem::directory_iterator(root))files.push_back(item.path());
    ASSERT_EQ(1u,files.size());AssFile reopened;agi::fs::path path(files.front().wstring());
    SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("metadata only",reopened.Properties.automation_settings.at("test"));
    OPT_SET("Path/Auto/Save")->SetString(old);
}

template<class T> concept PublicHistoryRestore = requires { typename T::HistoryRestore; };
static_assert(!PublicHistoryRestore<SubsController>);
TEST_F(ordinary_edit, ordinary_scope_cannot_register_a_live_history_writer_or_change_undo_granularity) {
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);
    EXPECT_THROW(LiveWriteForTest(*c).AddUndoManager([](AssFileCommit const&){}),std::logic_error);
    scope.reset();Input("first history edit");Input("same undo group");
    auto description=c->subsController->GetUndoDescription();
    // Input emits STARTACTION, so each call retains its own existing Undo group.
    c->subsController->Undo();EXPECT_EQ("first history edit",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo();EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("first history edit",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("same undo group",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
}

TEST_F(ordinary_edit, font_collection_uses_snapshot_and_can_be_destroyed_with_worker_pending) {
    auto action=OPT_GET("Tool/Fonts Collector/Action")->GetInt();OPT_SET("Tool/Fonts Collector/Action")->SetInt(0);
    Input("font snapshot");auto revision=c->subtitleEditGate.Revision();
    ShowFontsCollectorDialog(c.get());wxDialog* dialog=nullptr;FlatButton* start=nullptr;
    for(auto child:descendants(window))if(auto candidate=dynamic_cast<wxDialog*>(child);candidate && candidate->GetTitle()=="Fonts Collector")dialog=candidate;
    ASSERT_NE(nullptr,dialog);
    for(auto child:descendants(dialog))if(auto candidate=dynamic_cast<FlatButton*>(child);candidate && candidate->GetLabel()=="&Start!")start=candidate;
    ASSERT_NE(nullptr,start);
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    button(start,start->GetId());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
    comparison.reset();Input("edited while worker owns snapshot");
    button(dialog,wxID_CANCEL); // unregisters; parent destruction drains the private worker queue
    EXPECT_EQ("edited while worker owns snapshot",LiveRead(*c).Events.front().Text.get());
    OPT_SET("Tool/Fonts Collector/Action")->SetInt(action);
}

namespace {
class model_installation : public ordinary_edit {
protected:
    using State=SubtitleModelInstallation::State;
    using Ticket=SubtitleModelRebindTicket;
    agi::fs::path Candidate(std::string const& name,bool bad_extra=false) {
        AssFile source;source.LoadDefault();source.Events.front().Text="new 模型";source.Events.front().Comment=true;
        source.Properties.export_encoding="utf-8";source.Properties.video_zoom=1.5;
        auto id=source.AddExtradata("aegisub.enhanced.subtitle-binding","0:installed");
        source.Events.front().ExtradataIds=std::vector<uint32_t>{bad_extra?999999:id};
        auto path=agi::fs::path((std::filesystem::current_path()/(name+".ass")).wstring());
        SubtitleFormat::GetWriter(path)->WriteFile(&source,path,0,"utf-8");return path;
    }
    std::vector<std::string> Contents() {
        auto const& file=c->subtitleAccess->Read();std::vector<std::string> values;
        for(auto const& info:file.Info)values.push_back(std::string(info.Key())+"="+std::string(info.Value()));
        for(auto const& style:file.Styles)values.push_back(style.GetEntryData());
        for(auto const& row:file.Events)values.push_back(std::to_string(row.Id)+":"+std::to_string(row.Row)+":"+
            std::to_string(row.Start.GetMilliseconds())+":"+std::to_string(row.End.GetMilliseconds())+":"+row.GetEntryData());
        for(auto const& extra:file.Extradata)values.push_back(std::to_string(extra.id)+":"+extra.key+":"+extra.value);
        for(auto const& attachment:file.Attachments)values.push_back(attachment.GetEntryData());
        values.push_back(std::to_string(file.next_extradata_id));return values;
    }
    struct Envelope {
        std::vector<std::string> content;ProjectProperties properties;
        agi::SubtitleRevision revision;uint64_t metadata;
        std::set<int> selected;int active;
        wxString undo,redo;agi::fs::path filename,script;
        bool modified,metadata_dirty,undo_empty,redo_empty;
    };
    Envelope Capture() {
        return {Contents(),LiveRead(*c).Properties,c->subtitleEditGate.Revision(),c->subtitleAccess->MetadataRevision(),
            c->selectionController->SelectedIDs(),c->selectionController->ActiveID(),c->subsController->GetUndoDescription(),
            c->subsController->GetRedoDescription(),c->subsController->Filename(),c->path->Decode("?script"),
            c->subsController->IsModified(),c->subtitleAccess->MetadataDirty(),c->subsController->IsUndoStackEmpty(),c->subsController->IsRedoStackEmpty()};
    }
    void Unchanged(Envelope const& before) {
        EXPECT_EQ(before.content,Contents());EXPECT_EQ(static_cast<ProjectMetadata const&>(before.properties),static_cast<ProjectMetadata const&>(LiveRead(*c).Properties));
        auto const& ui=LiveRead(*c).Properties;EXPECT_EQ(before.properties.active_row,ui.active_row);EXPECT_EQ(before.properties.scroll_position,ui.scroll_position);
        EXPECT_EQ(before.properties.video_position,ui.video_position);EXPECT_EQ(before.properties.video_zoom,ui.video_zoom);
        EXPECT_EQ(before.properties.ar_mode,ui.ar_mode);EXPECT_EQ(before.properties.ar_value,ui.ar_value);
        EXPECT_EQ(before.revision,c->subtitleEditGate.Revision());EXPECT_EQ(before.metadata,c->subtitleAccess->MetadataRevision());
        EXPECT_EQ(before.selected,c->selectionController->SelectedIDs());EXPECT_EQ(before.active,c->selectionController->ActiveID());
        EXPECT_EQ(before.undo,c->subsController->GetUndoDescription());EXPECT_EQ(before.redo,c->subsController->GetRedoDescription());
        EXPECT_EQ(before.filename,c->subsController->Filename());EXPECT_EQ(before.script,c->path->Decode("?script"));
        EXPECT_EQ(before.modified,c->subsController->IsModified());EXPECT_EQ(before.metadata_dirty,c->subtitleAccess->MetadataDirty());
        EXPECT_EQ(before.undo_empty,c->subsController->IsUndoStackEmpty());EXPECT_EQ(before.redo_empty,c->subsController->IsRedoStackEmpty());
    }
};
TEST_F(model_installation, production_load_denied_by_comparison_preserves_complete_host_state) {
    Input("one");Input("two");c->subsController->Undo();
    c->subtitleAccess->SetAutomationSetting("keep","metadata");
    auto path=Candidate("denied-load");auto before=Capture();auto old=c->selectionController->ReadActiveLine();int notices=0;
    agi::signal::Connection listener=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++notices;if(t.Current())t.Acknowledge(t.Revision());});
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);Unchanged(before);
    EXPECT_EQ(old,c->selectionController->ReadActiveLine());EXPECT_EQ(0,notices);EXPECT_EQ("one",std::string(editor->GetTextRaw().data()));
    comparison.reset();c->subsController->Redo();EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
}
TEST_F(model_installation, production_observer_failure_is_recovery_blocked_and_retries_explicitly) {
    Input("retained old row");auto old=c->selectionController->ReadActiveLine();auto path=Candidate("observer-failure");
    bool fail=true;std::vector<int> order;
    agi::signal::Connection reader=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){order.push_back(1);EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());t.Acknowledge(t.Revision());});
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){order.push_back(2);if(fail)throw std::runtime_error("observer failure");old=nullptr;t.Acknowledge(t.Revision());});
    agi::signal::Connection normal=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){order.push_back(3);t.Acknowledge(t.Revision());});
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::runtime_error);
    EXPECT_EQ((std::vector<int>{3,2,1}),order);EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    ASSERT_NE(nullptr,old);EXPECT_EQ("retained old row",old->Text.get());EXPECT_EQ(1u,c->subsController->PendingInstallationObservers().size());
    EXPECT_FALSE(c->subtitleAccess->TryWrite());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());EXPECT_FALSE(c->subsController->CanSave());
    EXPECT_THROW(c->subsController->Save(path,"utf-8"),std::logic_error);
    EXPECT_THROW(c->subsController->RequestAutoSave(),std::logic_error);
    EXPECT_THROW(c->subsController->Close(),std::logic_error);
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);
    EXPECT_FALSE(c->subsController->RetryModelInstallation());EXPECT_EQ("retained old row",old->Text.get());
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());EXPECT_EQ(nullptr,old);
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    EXPECT_TRUE(c->subsController->PendingInstallationObservers().empty());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    reader.Disconnect();throwing.Disconnect();normal.Disconnect();
    Input("new edit");c->subsController->Undo();EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("new edit",LiveRead(*c).Events.front().Text.get());
}
TEST_F(model_installation, successful_install_invalidates_old_document_and_saves_reopened_data) {
    Input("old");auto old=c->subtitleEditGate.Capture();auto path=Candidate("successful-install");
    c->subsController->Load(path,"utf-8");auto next=c->subtitleEditGate.Revision();
    EXPECT_EQ(old.Revision().document,next.document);EXPECT_EQ(old.Revision().document_epoch+1,next.document_epoch);
    EXPECT_EQ(old.Revision().generation+1,next.generation);EXPECT_FALSE(old.Current());
    EXPECT_THROW(c->subtitleAccess->Read(old),std::logic_error);
    EXPECT_THROW(c->selectionController->ReadActiveLine(old),std::logic_error);
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
    EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());EXPECT_EQ("new 模型",std::string(editor->GetTextRaw().data()));
    c->subsController->Save(path,"utf-8");AssFile reopened;SubtitleFormat::GetReader(path,"utf-8")->ReadFile(&reopened,path,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("new 模型",reopened.Events.front().Text.get());EXPECT_TRUE(reopened.Events.front().Comment);
    EXPECT_EQ("aegisub.enhanced.subtitle-binding",reopened.Extradata.front().key);EXPECT_NE(nullptr,reopened.GetStyle("Default"));
}
TEST_F(model_installation, active_writer_and_prepare_failure_or_cancel_preserve_original_state) {
    Input("protected");auto path=Candidate("prepare-failure");
    {
        auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto before=Capture();
        EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);Unchanged(before);
    }
    for(bool cancel:{false,true}) {
        auto before=Capture();
        agi::signal::Connection observer=LiveRead(*c).AddInstallationObserver("prepare-stop",[&]{
            EXPECT_FALSE(c->subtitleAccess->TryWrite());
            EXPECT_THROW(c->subtitleAccess->SetAutomationSetting("keep","blocked"),std::logic_error);
            auto ui=c->subtitleAccess->UIState();ui.video_zoom=9;
            EXPECT_THROW(c->subtitleAccess->UpdateUIState(ui),std::logic_error);
            if(cancel)c->subsController->CancelModelInstallation();else throw std::runtime_error("prepare failure");
        },[](int,AssDialogue const*,Ticket t){t.Acknowledge(t.Revision());});
        EXPECT_ANY_THROW(c->subsController->Load(path,"utf-8"));Unchanged(before);
    }
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(model_installation, invalid_candidate_and_missing_input_do_not_publish_or_change_revisions) {
    Input("untouched");auto before=Capture();auto bad=Candidate("invalid-extra",true);
    EXPECT_THROW(c->subsController->Load(bad,"utf-8"),std::invalid_argument);Unchanged(before);
    auto missing=agi::fs::path((std::filesystem::current_path()/"missing-install-input.ass").wstring());
    EXPECT_ANY_THROW(c->subsController->Load(missing,"utf-8"));Unchanged(before);
}
TEST_F(model_installation, observer_reentry_and_real_host_close_event_cannot_destroy_publication_stack) {
    auto path=Candidate("reentrant-load");bool closed=false,visited=false;
    window->Bind(wxEVT_CLOSE_WINDOW,[&](wxCloseEvent& event){if(c->subsController->DeferHostClose(event)){closed=true;return;}event.Skip();});
    agi::signal::Connection observer=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){
        visited=true;EXPECT_FALSE(window->Close());EXPECT_TRUE(closed);
        EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);
        EXPECT_THROW(c->subsController->RetryModelInstallation(),std::logic_error);
        EXPECT_THROW(c->selectionController->SetActiveID(0),std::logic_error);
        EXPECT_FALSE(c->subtitleAccess->TryWrite());t.Acknowledge(t.Revision());
    });
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(visited);EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    wxCloseEvent normal(wxEVT_CLOSE_WINDOW);EXPECT_FALSE(c->subsController->DeferHostClose(normal));
}
TEST_F(model_installation, observer_self_destruction_withdraws_only_after_reference_release) {
    auto path=Candidate("destroyed-observer");bool destroyed=false;
    struct Borrower {
        std::shared_ptr<int> lifetime=std::make_shared<int>(0); // Declared first, released after every borrowed field.
        AssDialogue const* row;bool& destroyed;agi::signal::Connection connection;
        Borrower(AssDialogue const* row,bool& destroyed):row(row),destroyed(destroyed){}
        ~Borrower(){row=nullptr;destroyed=true;}
    };
    auto borrower=std::make_unique<Borrower>(c->selectionController->ReadActiveLine(),destroyed);
    borrower->connection=LiveRead(*c).AddInstallationObserver("owned borrower",[]{},[&](int,AssDialogue const*,Ticket){borrower.reset();},std::weak_ptr<void const>(borrower->lifetime));
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(destroyed);EXPECT_FALSE(borrower);EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
}
TEST_F(model_installation, background_installation_is_rejected_before_any_host_access) {
    auto path=Candidate("background-install");auto before=Capture();
    std::thread worker([&]{EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);});worker.join();Unchanged(before);
}
TEST_F(model_installation, missing_ack_and_disconnection_cannot_release_an_owners_old_reference) {
    Input("borrowed until owner releases");auto old=c->selectionController->ReadActiveLine();auto path=Candidate("missing-ack");
    auto lifetime=std::make_shared<int>(0);int called=0;
    agi::signal::Connection observer=LiveRead(*c).AddInstallationObserver("no acknowledgement",[]{},
        [&](int,AssDialogue const*,Ticket){++called;},std::weak_ptr<void const>(lifetime));
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::runtime_error);
    EXPECT_EQ(1,called);EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    observer.Disconnect();EXPECT_FALSE(c->subsController->RetryModelInstallation());
    EXPECT_EQ("borrowed until owner releases",old->Text.get());EXPECT_EQ(1,called);
    old=nullptr;lifetime.reset();EXPECT_TRUE(c->subsController->RetryModelInstallation());
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
}
TEST_F(model_installation, owner_destroyed_during_prepare_releases_its_old_reference_before_withdrawal) {
    auto path=Candidate("prepare-destruction");bool destroyed=false,called=false;
    struct Borrower {
        std::shared_ptr<int> lifetime=std::make_shared<int>(0);
        AssDialogue const* row;bool& destroyed;agi::signal::Connection connection;
        Borrower(AssDialogue const* row,bool& destroyed):row(row),destroyed(destroyed){}
        ~Borrower(){row=nullptr;destroyed=true;}
    };
    auto borrower=std::make_unique<Borrower>(c->selectionController->ReadActiveLine(),destroyed);
    borrower->connection=LiveRead(*c).AddInstallationObserver("prepare destroys owner",[&]{borrower.reset();},
        [&](int,AssDialogue const*,Ticket){called=true;},std::weak_ptr<void const>(borrower->lifetime));
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(destroyed);EXPECT_FALSE(called);
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
}
TEST_F(model_installation, repeated_load_keeps_selection_before_active_and_rejects_old_task_writeback) {
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto task=c->subtitleAccess->Edit(scope->Lease());scope.reset();
    auto path=Candidate("repeat-install");std::vector<int> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);if(t.Current())t.Acknowledge(t.Revision());});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);if(t.Current())t.Acknowledge(t.Revision());});
    auto revision=c->subtitleEditGate.Revision();
    for(int i=1;i<=2;++i) {
        order.clear();c->subsController->Load(path,"utf-8");
        EXPECT_EQ((std::vector<int>{1,2}),order);EXPECT_EQ(revision.document_epoch+i,c->subtitleEditGate.Revision().document_epoch);
        EXPECT_THROW(task.SetDialogueField(LiveRead(*c).Events.front().Id,&AssDialogueBase::Text,std::string("old task")),std::logic_error);
        EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    }
    order.clear();auto ids=c->selectionController->SelectedIDs();c->selectionController->SetSelectedIDs(ids);
    c->selectionController->SetSelectedIDs(ids);EXPECT_EQ((std::vector<int>{1,1}),order);
}
TEST_F(model_installation, real_grid_audio_and_long_lived_dialogs_rebind_and_edit_the_installed_document) {
    Input("old dialog input");auto path=Candidate("ui-observers");
    c->subsGrid=new BaseGrid(window,c.get());c->audioBox=new AudioBox(window,c.get());
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    {
        DialogTranslation translation(c.get());
        c->subsController->Load(path,"utf-8");
        EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
        EXPECT_TRUE(c->subsController->PendingInstallationObservers().empty());
        EXPECT_EQ("new 模型",std::string(editor->GetTextRaw().data()));
        EXPECT_NE(nullptr,c->audioController->GetTimingController());
        Input("edited new UI model");c->subsController->Undo();EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
        c->subsController->Redo();EXPECT_EQ("edited new UI model",LiveRead(*c).Events.front().Text.get());
    }
    c->project->CloseAudio();c->subsGrid=nullptr;c->audioBox=nullptr;
}

// C1-C1c-8: Load/Close lifecycle business migration.
class RecordMessages : public wxModalDialogHook {
public:
    std::vector<wxString> messages;
    RecordMessages() { Register(); }
    int Enter(wxDialog* dialog) override {
        if(auto message=dynamic_cast<wxMessageDialog*>(dialog)){messages.push_back(message->GetMessage());return wxID_OK;}
        return wxID_NONE;
    }
    void Exit(wxDialog*) override {}
};
class document_lifecycle : public model_installation {
protected:
    std::vector<agi::fs::path> Recent() { auto list=config::mru->Get("Subtitle");return list?*list:std::vector<agi::fs::path>{}; }
    static void Ack(Ticket& t) { if(t.Current())t.Acknowledge(t.Revision()); }
    agi::fs::path Local(std::string const& name) { return agi::fs::path((std::filesystem::current_path()/name).wstring()); }
};
TEST_F(document_lifecycle, production_close_installs_blank_document_with_new_identity_and_clean_history) {
    auto path=Candidate("close-source");c->subsController->Load(path,"utf-8");
    Input("edited before close");c->subtitleAccess->SetAutomationSetting("keep","metadata");
    ASSERT_TRUE(c->subsController->IsModified());
    auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto task=c->subtitleAccess->Edit(scope->Lease());scope.reset();
    auto old=c->subtitleEditGate.Capture();auto recent=Recent();
    int opened=0;agi::fs::path opened_path="unset";std::vector<int> order;
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const& p){++opened;opened_path=p;});
    agi::signal::Connection commit=LiveRead(*c).AddCommitListener([&](int type,AssDialogue const*,Ticket t){order.push_back(type==AssFile::COMMIT_NEW?0:-1);Ack(t);});
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);Ack(t);});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);Ack(t);});
    c->subsController->Close();
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty());
    EXPECT_EQ((std::vector<int>{0,1,2}),order); // one COMMIT_NEW, then Selection before Active, each exactly once
    auto next=c->subtitleEditGate.Revision();
    EXPECT_EQ(old.Revision().document,next.document);EXPECT_EQ(old.Revision().document_epoch+1,next.document_epoch);
    EXPECT_EQ(old.Revision().generation+1,next.generation);EXPECT_FALSE(old.Current());
    EXPECT_THROW(c->subtitleAccess->Read(old),std::logic_error);
    EXPECT_THROW(task.SetDialogueField(LiveRead(*c).Events.front().Id,&AssDialogueBase::Text,std::string("old task")),std::logic_error);
    ASSERT_EQ(1u,LiveRead(*c).Events.size());EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_NE(nullptr,LiveRead(*c).GetStyle("Default"));
    EXPECT_TRUE(LiveRead(*c).Extradata.empty());EXPECT_EQ("",std::string(editor->GetTextRaw().data()));
    AssFile blank;blank.LoadDefault(true,OPT_GET("Subtitle Format/ASS/Default Style Catalog")->GetString());
    EXPECT_EQ(static_cast<ProjectMetadata const&>(blank.Properties),static_cast<ProjectMetadata const&>(LiveRead(*c).Properties));
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
    EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_FALSE(c->subtitleAccess->MetadataDirty());
    EXPECT_TRUE(c->subsController->IsUndoStackEmpty());EXPECT_TRUE(c->subsController->IsRedoStackEmpty());
    EXPECT_NE(path,c->subsController->Filename());EXPECT_NE(path.parent_path(),c->path->Decode("?script"));
    EXPECT_EQ(1,opened);EXPECT_TRUE(opened_path.empty());EXPECT_EQ(recent,Recent()); // closing never adds an empty MRU entry
    Input("after close");EXPECT_TRUE(c->subsController->IsModified());
    c->subsController->Undo();EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    c->subsController->Redo();EXPECT_EQ("after close",LiveRead(*c).Events.front().Text.get());
}
TEST_F(document_lifecycle, production_close_denied_by_comparison_or_writer_preserves_complete_host_state) {
    auto path=Candidate("close-denied");c->subsController->Load(path,"utf-8");
    Input("one");Input("two");c->subsController->Undo();c->subtitleAccess->SetAutomationSetting("keep","metadata");
    int opened=0,notices=0;auto old=c->selectionController->ReadActiveLine();
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    agi::signal::Connection commit=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++notices;Ack(t);});
    auto before=Capture();
    {
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        EXPECT_THROW(c->subsController->Close(),std::logic_error);Unchanged(before);
    }
    {
        auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);auto held=Capture(); // acquiring a writer advances generation
        EXPECT_THROW(c->subsController->Close(),std::logic_error);Unchanged(held);
    }
    EXPECT_EQ(old,c->selectionController->ReadActiveLine());EXPECT_EQ(0,opened);EXPECT_EQ(0,notices);
    EXPECT_EQ(State::Aborting,c->subsController->InstallationStatus());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->subsController->Redo();EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());EXPECT_EQ(path,c->subsController->Filename());
}
TEST_F(document_lifecycle, close_recovery_blocked_runs_follow_up_work_exactly_once_after_explicit_retry) {
    auto path=Candidate("close-recovery");c->subsController->Load(path,"utf-8");Input("kept by retired model");
    auto old=c->selectionController->ReadActiveLine();auto recent=Recent();bool fail=true;int opened=0;
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");old=nullptr;Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());EXPECT_EQ(0,opened);
    ASSERT_NE(nullptr,old);EXPECT_EQ("kept by retired model",old->Text.get());
    EXPECT_FALSE(c->subtitleAccess->TryWrite());EXPECT_FALSE(c->subsController->CanSave());
    EXPECT_THROW(c->subsController->Close(),std::logic_error);
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::logic_error);
    EXPECT_FALSE(c->subsController->RetryModelInstallation());EXPECT_EQ(0,opened);
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());EXPECT_EQ(nullptr,old);
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_EQ(1,opened);
    EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty());EXPECT_EQ(recent,Recent());
    EXPECT_FALSE(c->subsController->IsModified());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
    EXPECT_THROW(c->subsController->RetryModelInstallation(),std::logic_error);EXPECT_EQ(1,opened); // plan consumed once
    c->subsController->Load(path,"utf-8");EXPECT_EQ(2,opened);ASSERT_FALSE(Recent().empty());EXPECT_EQ(path,Recent().front());
}
TEST_F(document_lifecycle, load_follow_up_failures_are_reported_after_commit_and_do_not_suppress_later_steps) {
    auto backup=OPT_GET("App/Auto/Backup")->GetBool();auto backup_path=OPT_GET("Path/Auto/Backup")->GetString();
    BOOST_SCOPE_EXIT_ALL(&) { OPT_SET("App/Auto/Backup")->SetBool(backup);OPT_SET("Path/Auto/Backup")->SetString(backup_path); };
    auto blocker=std::filesystem::current_path()/"backup-blocker";std::ofstream(blocker)<<"file, not a directory";
    OPT_SET("App/Auto/Backup")->SetBool(true);OPT_SET("Path/Auto/Backup")->SetString((blocker/"nested").string());
    int opened=0;agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    auto path=Candidate("follow-up");auto epoch=c->subtitleEditGate.Revision().document_epoch;
    EXPECT_NO_THROW(c->subsController->Load(path,"utf-8"));
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_EQ(epoch+1,c->subtitleEditGate.Revision().document_epoch);
    ASSERT_EQ(1u,c->subsController->LastPeripheralErrors().size());
    EXPECT_NE(std::string::npos,c->subsController->LastPeripheralErrors().front().find("Backup"));
    EXPECT_EQ(1,opened);EXPECT_EQ(path,c->subsController->Filename());ASSERT_FALSE(Recent().empty());EXPECT_EQ(path,Recent().front());
    EXPECT_EQ(path.parent_path().string(),OPT_GET("Path/Last/Subtitles")->GetString());
    EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->CanSave());
    agi::signal::Connection failing=c->subsController->AddFileOpenListener([&](agi::fs::path const&){throw std::runtime_error("listener failure");});
    EXPECT_NO_THROW(c->subsController->Load(path,"utf-8"));EXPECT_EQ(epoch+2,c->subtitleEditGate.Revision().document_epoch);
    EXPECT_EQ(2u,c->subsController->LastPeripheralErrors().size());EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());EXPECT_TRUE(c->subtitleAccess->TryWrite());
    failing.Disconnect();OPT_SET("App/Auto/Backup")->SetBool(false);
    c->subsController->Load(path,"utf-8");EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty()); // cleared per installation

    // A real MRU persistence failure must not suppress backup or FileOpen, or
    // turn an already committed installation into a failed load.
    agi::MRUManager blocked_mru(Local("backup-blocker/mru.json"), "{}", config::opt);
    auto original_mru=config::mru;config::mru=&blocked_mru;
    BOOST_SCOPE_EXIT_ALL(&) { config::mru=original_mru; };
    auto good_backup=Local("mru-failure-backup");
    OPT_SET("App/Auto/Backup")->SetBool(true);
    OPT_SET("Path/Auto/Backup")->SetString(good_backup.string());
    auto previous_opened=opened;
    EXPECT_NO_THROW(c->subsController->Load(path,"utf-8"));
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    ASSERT_EQ(1u,c->subsController->LastPeripheralErrors().size());
    EXPECT_NE(std::string::npos,c->subsController->LastPeripheralErrors().front().find("Recent files"));
    EXPECT_EQ(previous_opened+1,opened);
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path((good_backup/(path.stem().string()+".ORIGINAL"+path.extension().string())).wstring())));
    EXPECT_EQ(path,c->subsController->Filename());EXPECT_TRUE(c->subsController->CanSave());
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
}
TEST_F(document_lifecycle, project_entries_distinguish_parse_failure_recovery_blocked_and_follow_up_warnings) {
    RecordMessages messages;Input("kept");auto before=Capture();
    auto unsupported=Local("unsupported.qqq");std::ofstream(std::filesystem::path(unsupported.wstring()))<<"not subtitles";
    c->project->LoadSubtitles(unsupported,"utf-8");ASSERT_EQ(1u,messages.messages.size());Unchanged(before);
    c->project->LoadSubtitles(Local("missing-project-input.ass"),"utf-8");ASSERT_EQ(2u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().Contains("not found"));Unchanged(before);
    auto path=Candidate("project-recovery");bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    c->project->LoadSubtitles(path,"utf-8");ASSERT_EQ(3u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().Contains("Close Aegisub to recover"));EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    EXPECT_NO_THROW(c->project->CloseSubtitles());ASSERT_EQ(4u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().Contains("Close Aegisub to recover"));EXPECT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());throwing.Disconnect();
    agi::signal::Connection failing=c->subsController->AddFileOpenListener([&](agi::fs::path const&){throw std::runtime_error("listener failure");});
    auto second=Candidate("project-warning");c->project->LoadSubtitles(second,"utf-8");ASSERT_EQ(5u,messages.messages.size());
    EXPECT_TRUE(messages.messages.back().StartsWith("The subtitles were opened"));EXPECT_TRUE(messages.messages.back().Contains("listener failure"));
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_EQ(second,c->subsController->Filename());
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
}
TEST_F(document_lifecycle, project_close_reports_rejection_without_throwing_then_closes_once) {
    RecordMessages messages;auto path=Candidate("project-close");c->subsController->Load(path,"utf-8");Input("kept");
    int opened=0;agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
    auto before=Capture();
    {
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        EXPECT_NO_THROW(c->project->CloseSubtitles());ASSERT_EQ(1u,messages.messages.size());Unchanged(before);EXPECT_EQ(0,opened);
    }
    std::vector<int> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);Ack(t);});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);Ack(t);});
    c->project->CloseSubtitles();EXPECT_EQ(1u,messages.messages.size());EXPECT_EQ(1,opened);
    EXPECT_EQ((std::vector<int>{1,2}),order); // no second selection announcement after the installation
    EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_FALSE(c->subsController->IsModified());
    EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());
}
TEST_F(document_lifecycle, project_load_announces_installed_selection_once) {
    RecordMessages messages;auto path=Candidate("project-selection");std::vector<int> order;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){order.push_back(1);Ack(t);});
    agi::signal::Connection active=c->selectionController->AddActiveLineListener([&](Ticket t){order.push_back(2);Ack(t);});
    c->project->LoadSubtitles(path,"utf-8");EXPECT_TRUE(messages.messages.empty());
    EXPECT_EQ((std::vector<int>{1,2}),order);EXPECT_EQ(path,c->subsController->Filename());
    EXPECT_EQ((std::set<int>{LiveRead(*c).Events.front().Id}),c->selectionController->SelectedIDs());
}
TEST_F(document_lifecycle, open_close_reload_save_and_reopen_keep_path_mru_and_metadata_consistent) {
    auto a=Candidate("cycle-a");c->subsController->Load(a,"utf-8");
    EXPECT_EQ(a,Recent().front());EXPECT_EQ(a.parent_path(),c->path->Decode("?script"));
    EXPECT_EQ(a.parent_path().string(),OPT_GET("Path/Last/Subtitles")->GetString());
    Input("edited a");c->subtitleAccess->SetAutomationSetting("cycle","kept");c->subsController->Save(a,"utf-8");
    EXPECT_FALSE(c->subsController->IsModified());
    c->subsController->Close();EXPECT_NE(a,c->subsController->Filename());EXPECT_FALSE(c->subsController->IsModified());
    EXPECT_EQ(a,Recent().front());
    c->subsController->Load(a,"utf-8");EXPECT_EQ("edited a",LiveRead(*c).Events.front().Text.get());
    EXPECT_TRUE(c->subsController->IsUndoStackEmpty());EXPECT_FALSE(c->subsController->IsModified());EXPECT_FALSE(c->subtitleAccess->MetadataDirty());
    Input("second edit");auto b=Local("cycle-b.ass");c->subsController->Save(b,"utf-8");
    EXPECT_EQ(b,c->subsController->Filename());EXPECT_EQ(b,Recent().front());EXPECT_EQ(b.parent_path(),c->path->Decode("?script"));
    AssFile reopened;SubtitleFormat::GetReader(b,"utf-8")->ReadFile(&reopened,b,c->project->Timecodes(),"utf-8");
    EXPECT_EQ("second edit",reopened.Events.front().Text.get());EXPECT_TRUE(reopened.Events.front().Comment);
    ASSERT_EQ(1u,reopened.Extradata.size());EXPECT_EQ("aegisub.enhanced.subtitle-binding",reopened.Extradata.front().key);
    c->subsController->Close();c->subsController->Load(b,"utf-8");EXPECT_EQ("second edit",LiveRead(*c).Events.front().Text.get());
}
TEST_F(document_lifecycle, explicit_retry_with_real_grid_and_dialogs_repeats_rebinding_without_duplicate_side_effects) {
    Input("old dialog input");auto path=Candidate("retry-ui");c->subsGrid=new BaseGrid(window,c.get());
    {
        DialogTranslation translation(c.get());
        bool fail=true;int opened=0;auto recent=Recent();
        agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){++opened;});
        agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
        auto metadata=c->subtitleAccess->MetadataRevision();
        EXPECT_THROW(c->subsController->Load(path,"utf-8"),SubtitleModelRecoveryBlocked);
        auto published=c->subtitleAccess->MetadataRevision();auto revision=c->subtitleEditGate.Revision();
        EXPECT_EQ(metadata+1,published);EXPECT_EQ(0,opened);EXPECT_EQ(recent,Recent());
        EXPECT_FALSE(c->subsController->RetryModelInstallation()); // every fixed observer dispatched again
        EXPECT_EQ(published,c->subtitleAccess->MetadataRevision());EXPECT_EQ(revision,c->subtitleEditGate.Revision());EXPECT_EQ(0,opened);
        fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
        EXPECT_EQ(published,c->subtitleAccess->MetadataRevision());EXPECT_EQ(revision,c->subtitleEditGate.Revision());
        EXPECT_EQ(1,opened);EXPECT_EQ(path,Recent().front());EXPECT_TRUE(c->subsController->IsUndoStackEmpty());
        EXPECT_FALSE(c->subsController->IsModified());EXPECT_FALSE(c->subtitleAccess->MetadataDirty());
        EXPECT_EQ(LiveRead(*c).Events.front().Id,c->selectionController->ActiveID());EXPECT_EQ("new 模型",std::string(editor->GetTextRaw().data()));
        Input("edited after retry");c->subsController->Undo();EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
        c->subsController->Redo();EXPECT_EQ("edited after retry",LiveRead(*c).Events.front().Text.get());
    }
    c->subsGrid=nullptr;
}
TEST_F(document_lifecycle, autosave_result_completing_after_close_cannot_touch_the_new_document) {
    auto save=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString((std::filesystem::current_path()/"autosave").string());
    BOOST_SCOPE_EXIT_ALL(&) { OPT_SET("Path/Auto/Save")->SetString(save); };
    auto path=Candidate("autosave-close");c->subsController->Load(path,"utf-8");Input("pending autosave");
    c->subsController->RequestAutoSave();c->subsController->Close();Input("new document edit");
    EXPECT_TRUE(c->subsController->IsModified());
    for(int i=0;i<200 && wxTheApp->HasPendingEvents();++i)wxTheApp->ProcessPendingEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));wxTheApp->ProcessPendingEvents();
    EXPECT_EQ("new document edit",LiveRead(*c).Events.front().Text.get());EXPECT_TRUE(c->subsController->IsModified());
    c->subsController->Undo();EXPECT_EQ("",LiveRead(*c).Events.front().Text.get());EXPECT_FALSE(c->subsController->IsModified());
}
TEST_F(document_lifecycle, lua_local_scripts_reload_is_post_commit_follow_up_not_isolated_work) {
    // Boundary record: automation still reads the live document from its FileOpen listener.
    // It runs only after the installation is Committed and is not migrated by C1-C1c-8.
    auto path=Candidate("lua-boundary");int opened=0;
    agi::signal::Connection open=c->subsController->AddFileOpenListener([&](agi::fs::path const&){
        ++opened;EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    });
    c->subsController->Load(path,"utf-8");EXPECT_EQ(1,opened);EXPECT_TRUE(c->subsController->LastPeripheralErrors().empty());
}
}
TEST_F(visual_edit, production_load_rebinds_real_visual_cache_and_renderer_before_next_gesture) {
    Install<VisualToolDrag>();auto old_id=LiveRead(*c).Events.front().Id;
    AssFile source;source.LoadDefault();source.Events.front().Start=0;source.Events.front().End=5000;
    source.SetScriptInfo("PlayResX","640");source.SetScriptInfo("PlayResY","480");
    source.Events.front().Text="{\\pos(100,100)\\bord7}new visual model";
    auto path=agi::fs::path((std::filesystem::current_path()/"visual-install.ass").wstring());
    SubtitleFormat::GetWriter(path)->WriteFile(&source,path,0,"utf-8");
    c->subsController->Load(path,"utf-8");EXPECT_EQ(SubtitleModelInstallation::State::Committed,c->subsController->InstallationStatus());
    EXPECT_NE(old_id,LiveRead(*c).Events.front().Id);EXPECT_EQ(source.Events.front().Text.get(),Text());EXPECT_FALSE(FrameData().empty());
    auto before=Text();Mouse(wxEVT_LEFT_DOWN,100,100,true);ASSERT_TRUE(display->HasCapture());
    Mouse(wxEVT_MOTION,150,160,true);Mouse(wxEVT_LEFT_UP,150,160);EXPECT_NE(before,Text());
    c->subsController->Undo();EXPECT_EQ(before,Text());c->subsController->Redo();EXPECT_NE(before,Text());
}
static_assert(!std::is_constructible_v<SubtitleModelInstallation,SubsController&>);

// C1-C1c-10: the privatized owner still holds exactly one live document; the
// production read path is that document, copies are detached, and runtime
// authority (revision tokens, history restore, document replacement) is unchanged.
TEST_F(ordinary_edit, owner_privatization_keeps_one_live_document_read_only_path_and_detached_copies) {
    AssFile const* live=&LiveRead(*c);
    EXPECT_EQ(live,&LiveSubtitleModelTestAccess::Model(*c));
    Input("owner edit");
    ASSERT_EQ("owner edit",LiveRead(*c).Events.front().Text.get());
    auto id=LiveRead(*c).Events.front().Id;
    auto undo=c->subsController->GetUndoDescription();
    auto revision=c->subtitleEditGate.Capture();

    auto snapshot=c->subtitleAccess->Snapshot();
    snapshot->Events.front().Text="detached";snapshot->AddExtradata("k","v");snapshot->Events.clear_and_dispose([](AssDialogue* row){delete row;});
    AssFile copy(LiveRead(*c));copy.Events.front().Text="copy";copy.Commit("copy",AssFile::COMMIT_DIAG_TEXT);
    EXPECT_EQ("owner edit",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(id,LiveRead(*c).Events.front().Id);
    EXPECT_EQ(undo,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.Authorizes(revision));

    std::thread worker([&]{ EXPECT_THROW((void)LiveRead(*c),std::logic_error); });
    worker.join();

    c->subsController->Undo();EXPECT_NE("owner edit",LiveRead(*c).Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ("owner edit",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(live,&LiveRead(*c));
    EXPECT_FALSE(c->subtitleEditGate.Authorizes(revision));

    auto before_close=c->subtitleEditGate.Capture();
    c->subsController->Close();
    EXPECT_EQ(live,&LiveRead(*c));
    EXPECT_FALSE(c->subtitleEditGate.Authorizes(before_close));
    EXPECT_NE("owner edit",LiveRead(*c).Events.front().Text.get());
}

// C1-C1c-11 A1 (audit probe): a throwing newer commit observer used to stop
// delivery, leaving older observers (BaseGrid/SubsEditBox) on freed rows.
TEST_F(ordinary_edit, throwing_newer_commit_observer_still_reaches_older_observers_and_keeps_deleted_rows_alive) {
    cmd::call("edit/line/duplicate",c.get());
    ASSERT_EQ(2u,LiveRead(*c).Events.size());
    auto doomed=&LiveRead(*c).Events.back();auto doomed_id=doomed->Id;
    c->selectionController->SetSelectionAndActiveIDs({doomed_id},doomed_id);
    int older_calls=0;
    agi::signal::Connection older=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*){++older_calls;});
    agi::signal::Connection newer=LiveRead(*c).AddCommitListener([](int,AssDialogue const*){throw std::runtime_error("newer observer");});
    auto before=c->subtitleAccess->RetainedRetiredNodes();
    EXPECT_THROW(cmd::call("edit/line/delete",c.get()),std::runtime_error);
    EXPECT_EQ(1u,LiveRead(*c).Events.size());
    EXPECT_EQ(1,older_calls);
    EXPECT_EQ(before+1,c->subtitleAccess->RetainedRetiredNodes());
    EXPECT_EQ(doomed_id,doomed->Id) << "retired row must remain valid for observers that may still reference it";
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    newer.Disconnect();
    EXPECT_TRUE(c->subtitleAccess->TryWrite().has_value());
}

// C1-C1c-11 B1/B2 (audit probe): Undo/Redo restoration prepares the target state
// before touching the model or the history stacks; a failed announcement keeps
// the replaced rows alive and the model and history consistent with each other.
TEST_F(ordinary_edit, undo_with_throwing_commit_observer_keeps_model_history_and_replaced_rows_consistent) {
    auto commit=[&](char const* text,char const* message){ auto w=c->subtitleAccess->TryWrite();ASSERT_TRUE(w);
        LiveWriteForTest(*c).Events.front().Text=text;LiveWriteForTest(*c).Commit(message,AssFile::COMMIT_DIAG_TEXT); };
    commit("one","step one"); commit("two","step two");
    auto replaced=&LiveRead(*c).Events.front();auto replaced_id=replaced->Id;
    auto replaced_style=&LiveRead(*c).Styles.front();auto style_name=replaced_style->name;
    int older_calls=0;
    agi::signal::Connection older=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*){++older_calls;});
    agi::signal::Connection thrower=LiveRead(*c).AddCommitListener([](int,AssDialogue const*){throw std::runtime_error("observer");});
    EXPECT_THROW(c->subsController->Undo(),std::runtime_error);
    EXPECT_EQ(1,older_calls);
    // Model and history agree: the undo took effect and can be redone.
    EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("step two",c->subsController->GetRedoDescription());
    // The replaced nodes were parked, not freed.
    EXPECT_EQ(replaced_id,replaced->Id);EXPECT_EQ(style_name,replaced_style->name);
    EXPECT_FALSE(c->subtitleEditGate.NeedsRebinding());
    thrower.Disconnect();
    EXPECT_TRUE(c->subtitleAccess->TryWrite().has_value());
    c->subsController->Redo();
    EXPECT_EQ("two",LiveRead(*c).Events.front().Text.get());
    c->subsController->Undo();c->subsController->Redo();c->subsController->Undo();
    EXPECT_EQ("one",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(1u,LiveRead(*c).Styles.size());
}

// C1-C1c-11 A2 (audit probe): media changes in RecoveryBlocked used to replace
// the provider, then fail in SetMetadata before announcing it, leaving the
// audio controller on a freed provider. They are now refused before any effect.
TEST_F(document_lifecycle, media_changes_are_refused_without_side_effects_while_recovery_is_blocked) {
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    auto first=c->project->AudioProvider();ASSERT_TRUE(first);int announced=0;
    agi::signal::Connection l=c->project->AddAudioProviderListener([&](agi::AudioProvider*){++announced;});
    auto path=Candidate("media-recovery");c->subsController->Load(path,"utf-8");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    RecordMessages messages;
    auto metadata=c->subtitleAccess->MetadataRevision();
    EXPECT_NO_THROW(c->project->LoadAudio("dummy-audio:silence?sr=22050&bd=16&ch=1&ln=220500"));
    EXPECT_EQ(first,c->project->AudioProvider());
    EXPECT_NO_THROW(c->project->CloseAudio());
    EXPECT_EQ(first,c->project->AudioProvider());
    EXPECT_NO_THROW(c->project->CloseVideo());
    EXPECT_NO_THROW(c->project->CloseTimecodes());
    EXPECT_NO_THROW(c->project->CloseKeyframes());
    EXPECT_NO_THROW(c->project->LoadList({}));
    EXPECT_EQ(0,announced);EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
    ASSERT_EQ(6u,messages.messages.size());
    for(auto const& m:messages.messages)EXPECT_TRUE(m.Contains("Close Aegisub to recover"));
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());throwing.Disconnect();
    c->project->CloseAudio();EXPECT_EQ(1,announced);EXPECT_FALSE(c->project->AudioProvider());
}

// C1-C1c-11 A3 (audit finding): ~DialogExport stored the filter selection with
// SetMetadata, which throws while recovery is blocked -> std::terminate. The
// destructor must not throw, whether the metadata is sealed or an observer throws.
TEST_F(document_lifecycle, export_dialog_closes_without_terminating_when_metadata_cannot_be_stored) {
    {
        bool fail=true;
        agi::signal::Connection throwing=c->subtitleAccess->AddMetadataListener([&]{if(fail)throw std::runtime_error("metadata observer");});
        RunModal close("Export",[&](wxDialog*){});
        EXPECT_NO_THROW(ShowExportDialog(c.get()));
        fail=false;
    }
    auto path=Candidate("export-recovery");c->subsController->Load(path,"utf-8");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    auto metadata=c->subtitleAccess->MetadataRevision();
    {RunModal close("Export",[&](wxDialog*){});EXPECT_NO_THROW(ShowExportDialog(c.get()));}
    EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
}

// C1-C1c-11 A4 (audit finding): the crash handler saved with Save(), which
// throws for an installing or RecoveryBlocked model, inside the fatal-exception
// handler. The recovery copy works in every state and never throws.
TEST_F(document_lifecycle, recovery_copy_is_written_in_recovery_blocked_without_touching_document_state) {
    auto path=Candidate("crash-recovery");c->subsController->Load(path,"utf-8");Input("unsaved work");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    auto filename=c->subsController->Filename();auto modified=c->subsController->IsModified();
    auto metadata=c->subtitleAccess->MetadataRevision();
    EXPECT_THROW(c->subsController->Save(Local("recovered-save.ass")),std::logic_error);
    auto target=Local("recovered-copy.ass");
    EXPECT_TRUE(c->subsController->WriteRecoveryCopy(target));
    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path(target.wstring())));
    AssFile reread;SubtitleFormat::GetReader(target,"utf-8")->ReadFile(&reread,target,{},"utf-8");
    EXPECT_EQ(LiveRead(*c).Events.size(),reread.Events.size());
    EXPECT_EQ(filename,c->subsController->Filename());EXPECT_EQ(modified,c->subsController->IsModified());
    EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
    EXPECT_FALSE(c->subsController->WriteRecoveryCopy(Local("missing-dir/sub/recovered.ass")));
    EXPECT_FALSE(c->subsController->WriteRecoveryCopy(Local("recovered.unknown-format")));
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
}

// C1-C1c-11 B5 (audit finding): GetSubtitles read the worker-owned render copy
// and subtitle provider from the UI thread without synchronizing with the
// worker. It now runs on the worker after every queued subtitle update.
TEST_F(visual_edit, subtitle_overlay_is_rendered_on_the_worker_after_queued_updates) {
    auto set=[&](char const* text){auto scope=c->subtitleAccess->TryWrite();ASSERT_TRUE(scope);
        LiveWriteForTest(*c).Events.front().Text=text;LiveWriteForTest(*c).Commit("overlay",AssFile::COMMIT_DIAG_TEXT);};
    auto alpha_at=[&](int x,int y){
        auto frame=c->project->VideoProvider()->GetSubtitles(1.0);
        return frame.data[(y*frame.width+x)*4+3];
    };
    char const* square="{\\an7\\pos(0,0)\\bord0\\shad0\\1c&HFFFFFF&\\p1}m 0 0 l 200 0 200 200 0 200";
    for(int i=0;i<20;++i) {
        set(square);EXPECT_EQ(255,alpha_at(100,100)) << "iteration " << i;
        set("");EXPECT_EQ(0,alpha_at(100,100)) << "iteration " << i;
    }
}

// C1-C1c-12 (audit C1): during a comparison session, Save cannot move the file
// name, ?script, relative paths or the saved baseline; AutoSave is deferred;
// media changes are refused; Export still works. All without side effects.
TEST_F(document_lifecycle, comparison_session_freezes_save_target_metadata_and_media) {
    auto saved=Local("compare-saved.ass");c->subsController->Save(saved,"utf-8");
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    auto provider=c->project->AudioProvider();ASSERT_TRUE(provider);
    Input("compared edit");ASSERT_TRUE(c->subsController->IsModified());
    auto autosave_root=std::filesystem::current_path()/"compare-autosave";
    std::filesystem::remove_all(autosave_root); // left by an earlier run
    auto old_autosave=OPT_GET("Path/Auto/Save")->GetString();OPT_SET("Path/Auto/Save")->SetString(autosave_root.string());
    auto filename=c->subsController->Filename();auto script=c->path->Decode("?script");
    auto properties=LiveRead(*c).Properties;auto metadata=c->subtitleAccess->MetadataRevision();
    auto revision=c->subtitleEditGate.Revision();
    {
        auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
        RecordMessages messages;
        auto elsewhere=Local("compare-elsewhere.ass");
        std::filesystem::remove(std::filesystem::path(elsewhere.wstring())); // left by an earlier run
        EXPECT_THROW(c->subsController->Save(elsewhere,"utf-8"),SubtitleProjectBusy);
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(elsewhere.wstring())));
        // The production command reports the refusal instead of failing silently.
        EXPECT_NO_THROW(cmd::call("subtitle/save",c.get()));
        ASSERT_EQ(1u,messages.messages.size());EXPECT_TRUE(messages.messages.back().Contains("being compared"));
        c->subsController->RequestAutoSave();
        EXPECT_FALSE(std::filesystem::exists(autosave_root));
        EXPECT_NO_THROW(c->project->LoadAudio("dummy-audio:silence?sr=22050&bd=16&ch=1&ln=220500"));
        EXPECT_NO_THROW(c->project->CloseAudio());
        EXPECT_EQ(provider,c->project->AudioProvider());
        ASSERT_EQ(3u,messages.messages.size());EXPECT_TRUE(messages.messages.back().Contains("being compared"));
        // Read-only output stays available.
        auto exported=Local("compare-export.srt");AssExporter exporter(c.get());
        EXPECT_NO_THROW(exporter.Export(exported,"utf-8",nullptr));
        EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(exported.wstring())));
        EXPECT_EQ(filename,c->subsController->Filename());EXPECT_EQ(script,c->path->Decode("?script"));
        EXPECT_TRUE(c->subsController->IsModified());EXPECT_EQ(metadata,c->subtitleAccess->MetadataRevision());
        EXPECT_TRUE(static_cast<ProjectMetadata const&>(properties)==static_cast<ProjectMetadata const&>(LiveRead(*c).Properties));
        EXPECT_EQ(revision,c->subtitleEditGate.Revision());
        EXPECT_TRUE(comparison->Valid());
    }
    // After the comparison everything works again, including the deferred autosave.
    c->subsController->RequestAutoSave();c->subsController.reset();wxTheApp->ProcessPendingEvents();
    EXPECT_TRUE(std::filesystem::exists(autosave_root));
    OPT_SET("Path/Auto/Save")->SetString(old_autosave);
}
TEST_F(document_lifecycle, comparison_cannot_begin_while_a_save_or_media_change_is_in_progress) {
    bool began=true;int saves=0;
    agi::signal::Connection on_save=c->subsController->AddFileSaveListener([&]{++saves;began=c->subtitleEditGate.TryBeginComparison().has_value();});
    c->subsController->Save(Local("in-progress.ass"),"utf-8");
    EXPECT_EQ(1,saves);EXPECT_FALSE(began);
    began=true;
    agi::signal::Connection on_audio=c->project->AddAudioProviderListener([&](agi::AudioProvider*){began=c->subtitleEditGate.TryBeginComparison().has_value();});
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    EXPECT_FALSE(began);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

// C1-C1c-13: modal dialogs without write leases (B6), save failures (C2),
// opening inside notifications (C4) and atomic multi-step writes (C6).
namespace {
class ProbeModal : public wxModalDialogHook {
    std::function<int(wxDialog*)> enter;
public:
    explicit ProbeModal(std::function<int(wxDialog*)> enter) : enter(std::move(enter)) { Register(); }
    int Enter(wxDialog* dialog) override { return enter(dialog); }
    void Exit(wxDialog*) override {}
};
wxTextCtrl* AssColorInput(wxDialog* dialog) {
    for(auto child:descendants(dialog))
        if(auto text=dynamic_cast<wxTextCtrl*>(child);text && text->GetValue().StartsWith("&H"))return text;
    return nullptr;
}
}
TEST_F(ordinary_edit, color_picker_preview_is_an_interaction_and_cancel_undoes_it) {
    Input("sample");
    auto before=c->subsController->GetUndoDescription();
    bool previewed=false,comparison_refused=false;
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            previewed=LiveRead(*c).Events.front().Text.get().find("0000FF")!=std::string::npos;
            comparison_refused=!c->subtitleEditGate.TryBeginComparison().has_value();
        });
        cmd::call("edit/color/primary",c.get());
    }
    EXPECT_TRUE(previewed);EXPECT_TRUE(comparison_refused);
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ(before,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison().has_value());
}
TEST_F(ordinary_edit, color_picker_cancel_never_undoes_a_commit_made_while_it_was_open) {
    Input("sample");
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            // Another handler commits through the shared write scope meanwhile.
            auto write=c->subtitleAccess->TryWrite();ASSERT_TRUE(write);
            AssDialogueBase row=LiveRead(*c).Events.front();row.Actor="other handler";
            auto edit=c->subtitleAccess->Edit(write->Lease());edit.UpdateRows({row});
            edit.Commit("other handler",AssFile::COMMIT_DIAG_META);
            input->SetValue("&H00FF00&"); // the preview session is stale: ignored
        });
        cmd::call("edit/color/primary",c.get());
    }
    EXPECT_EQ("other handler",LiveRead(*c).Events.front().Actor.get());
    EXPECT_EQ(wxString("other handler"),c->subsController->GetUndoDescription());
    EXPECT_EQ(std::string::npos,LiveRead(*c).Events.front().Text.get().find("00FF00"));
    EXPECT_NE(std::string::npos,LiveRead(*c).Events.front().Text.get().find("0000FF"));
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison().has_value());
}
TEST_F(ordinary_edit, style_panel_colour_picker_previews_as_an_interaction) {
    Input("sample");
    auto panel=new StylePanel(window,c.get());
    panel->UpdateNow();
    auto before=c->subsController->GetUndoDescription();
    bool previewed=false,comparison_refused=false;
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            previewed=LiveRead(*c).Events.front().Text.get()=="{\\c&H0000FF&}sample";
            comparison_refused=!c->subtitleEditGate.TryBeginComparison().has_value();
        });
        panel->PickColour("fill");
    }
    EXPECT_TRUE(previewed);EXPECT_TRUE(comparison_refused);
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get()) << "cancel undoes the preview";
    EXPECT_EQ(before,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison().has_value());
    {
        RunModal hook("Select Color",[&](wxDialog* dialog){
            auto input=AssColorInput(dialog);ASSERT_NE(nullptr,input);
            input->SetValue("&H0000FF&");
            auto write=c->subtitleAccess->TryWrite();ASSERT_TRUE(write);
            AssDialogueBase row=LiveRead(*c).Events.front();row.Actor="other handler";
            auto edit=c->subtitleAccess->Edit(write->Lease());edit.UpdateRows({row});
            edit.Commit("other handler",AssFile::COMMIT_DIAG_META);
            input->SetValue("&H00FF00&");
        });
        panel->PickColour("outline");
    }
    EXPECT_EQ(wxString("other handler"),c->subsController->GetUndoDescription()) << "never undone";
    EXPECT_EQ("{\\3c&H0000FF&}sample",LiveRead(*c).Events.front().Text.get()) << "and the stale preview is ignored";
    panel->Destroy();
}
TEST_F(ordinary_edit, font_resample_and_style_manager_questions_hold_no_write_lease) {
    Input("sample");
    std::vector<std::pair<wxString,bool>> seen;
    auto description=c->subsController->GetUndoDescription();auto styles=LiveRead(*c).Styles.size();
    ShowStyleManagerDialog(c.get());
    wxDialog* manager=nullptr;
    for(auto top:wxTopLevelWindows) if(auto d=dynamic_cast<wxDialog*>(top);d && d->GetTitle()=="Styles Manager")manager=d;
    ASSERT_NE(nullptr,manager); manager->Hide();
    auto cleanup=std::unique_ptr<wxDialog,std::function<void(wxDialog*)>>(manager,[](wxDialog* d){d->Close();});
    FlatButton *import=nullptr,*remove=nullptr;StyleList *current=nullptr;
    for(auto child:descendants(manager)) {
        if(auto b=dynamic_cast<FlatButton*>(child); b && b->GetLabel()=="&Import from script...")import=b;
        if(child->GetParent()->GetLabel()=="Current script") {
            if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
            if(auto list=dynamic_cast<StyleList*>(child))current=list;
        }
    }
    ASSERT_NE(nullptr,import);ASSERT_NE(nullptr,remove);ASSERT_NE(nullptr,current);
    ProbeModal probe([&](wxDialog* dialog){
        seen.emplace_back(dialog->GetTitle(),c->subtitleEditGate.TryBeginComparison().has_value());
        return dynamic_cast<wxMessageDialog*>(dialog)?wxID_NO:wxID_CANCEL;
    });
    cmd::call("edit/font",c.get());
    cmd::call("tool/resampleres",c.get());
    current->DeselectAll();current->SetStringSelection("Default");button(remove,remove->GetId());
    button(import,import->GetId());
    ASSERT_EQ(4u,seen.size());
    for(auto const& [title,free]:seen) EXPECT_TRUE(free) << title;
    EXPECT_EQ(description,c->subsController->GetUndoDescription());EXPECT_EQ(styles,LiveRead(*c).Styles.size());
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get());
}
TEST_F(ordinary_edit, style_manager_lists_show_each_style_with_its_use_and_copy_between_cards) {
    Input("sample");
    ShowStyleManagerDialog(c.get());
    wxDialog* manager=nullptr;
    for(auto top:wxTopLevelWindows) if(auto d=dynamic_cast<wxDialog*>(top);d && d->GetTitle()=="Styles Manager")manager=d;
    ASSERT_NE(nullptr,manager); manager->Hide();
    auto cleanup=std::unique_ptr<wxDialog,std::function<void(wxDialog*)>>(manager,[](wxDialog* d){d->Close();});
    StyleList *storage=nullptr,*current=nullptr;StyleCard *current_card=nullptr;
    FlatButton *edit=nullptr;wxWindow *to_storage=nullptr,*to_script=nullptr;
    for(auto child:descendants(manager)) {
        if(auto card=dynamic_cast<StyleCard*>(child);card && card->GetLabel()=="Current script")current_card=card;
        if(auto list=dynamic_cast<StyleList*>(child))(child->GetParent()->GetLabel()=="Storage"?storage:current)=list;
        if(auto b=dynamic_cast<FlatButton*>(child);b && b->GetLabel()=="&Edit" && child->GetParent()->GetLabel()=="Current script")edit=b;
        if(child->GetToolTipText()=="Copy the styles chosen to the storage")to_storage=child;
        if(child->GetToolTipText()=="Copy the styles chosen to the current script")to_script=child;
    }
    ASSERT_NE(nullptr,storage);ASSERT_NE(nullptr,current);ASSERT_NE(nullptr,current_card);
    ASSERT_NE(nullptr,edit);ASSERT_NE(nullptr,to_storage);ASSERT_NE(nullptr,to_script);
    // Each style of the script, with how many lines use it
    ASSERT_EQ(1u,current->GetCount());EXPECT_EQ("Default",current->GetString(0));
    EXPECT_EQ("1 line",current->GetRow(0).note);EXPECT_EQ("1 style",current_card->GetCount());
    {
        auto write=c->subtitleAccess->TryWrite();auto extra=new AssStyle;extra->name="Unused";
        LiveWriteForTest(*c).Styles.push_back(*extra);LiveWriteForTest(*c).Commit("fixture style",AssFile::COMMIT_STYLES);
    }
    ASSERT_EQ(2u,current->GetCount());EXPECT_EQ("unused",current->GetRow(1).note);EXPECT_EQ("2 styles",current_card->GetCount());
    // A click chooses as a list box does, and the buttons follow it
    current->Click(1);EXPECT_TRUE(current->IsSelected(1));EXPECT_FALSE(current->IsSelected(0));EXPECT_TRUE(edit->IsEnabled());
    current->Click(0,true);wxArrayInt chosen;EXPECT_EQ(2,current->GetSelections(chosen));EXPECT_FALSE(edit->IsEnabled());
    current->Click(-1);EXPECT_EQ(0,current->GetSelections(chosen));
    // Copied to the storage, the stored style is marked as in the script
    ProbeModal yes([](wxDialog* dialog){return dynamic_cast<wxMessageDialog*>(dialog)?wxID_YES:wxID_CANCEL;});
    current->SetStringSelection("Unused");button(to_storage,to_storage->GetId());
    int stored=-1;for(unsigned i=0;i<storage->GetCount();++i)if(storage->GetString(i)=="Unused")stored=i;
    ASSERT_NE(-1,stored);EXPECT_EQ("in script",storage->GetRow(stored).note);EXPECT_TRUE(storage->IsSelected(stored));
    {
        auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).GetStyle("Unused")->fontsize=99;
        LiveWriteForTest(*c).Commit("fixture change",AssFile::COMMIT_STYLES);
    }
    EXPECT_EQ("differs from script",storage->GetRow(stored).note);
    // And copied back over the script's, the two are the same again
    storage->SetSelection(stored);button(to_script,to_script->GetId());
    EXPECT_EQ(48.,LiveRead(*c).GetStyle("Unused")->fontsize);EXPECT_EQ("in script",storage->GetRow(stored).note);
}
TEST_F(ordinary_edit, joined_lines_are_committed_before_selection_observers_see_them) {
    int first=LiveRead(*c).Events.front().Id,second=0;
    {
        auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue;row->Text="second";second=row->Id;
        LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("join fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    std::vector<wxString> seen;std::vector<size_t> rows;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&]{
        seen.push_back(c->subsController->GetUndoDescription());rows.push_back(LiveRead(*c).Events.size());});
    cmd::call("edit/line/join/concatenate",c.get());
    ASSERT_FALSE(seen.empty());
    for(size_t i=0;i<seen.size();++i){EXPECT_EQ(wxString("join lines"),seen[i]);EXPECT_EQ(1u,rows[i]);}
    ASSERT_EQ(1u,LiveRead(*c).Events.size());
    c->subsController->Undo();EXPECT_EQ(2u,LiveRead(*c).Events.size());
}
TEST_F(document_lifecycle, failed_save_restores_metadata_through_its_listeners) {
    c->subsController->Save(Local("rollback.ass"),"utf-8");
    auto filename=c->subsController->Filename();
    auto original=LiveRead(*c).Properties.export_encoding;
    std::string seen="unset";int notices=0;
    agi::signal::Connection metadata=c->subtitleAccess->AddMetadataListener([&]{++notices;seen=LiveRead(*c).Properties.export_encoding;});
    agi::signal::Connection on_save=c->subsController->AddFileSaveListener([&]{
        c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::ExportEncoding,"changed during save");
        throw std::runtime_error("save listener");
    });
    auto revision=c->subtitleAccess->MetadataRevision();
    EXPECT_THROW(c->subsController->Save(Local("rollback-2.ass"),"utf-8"),std::runtime_error);
    EXPECT_EQ(original,LiveRead(*c).Properties.export_encoding);
    EXPECT_EQ(original,seen); // the listeners were told about the restored value
    EXPECT_EQ(2,notices);EXPECT_EQ(revision+2,c->subtitleAccess->MetadataRevision());
    EXPECT_EQ(filename,c->subsController->Filename());EXPECT_FALSE(c->subsController->IsModified());
}
TEST_F(document_lifecycle, failed_save_on_every_change_still_announces_the_recorded_commit) {
    c->subsController->Save(Local("every-change.ass"),"utf-8");
    auto old=OPT_GET("App/Auto/Save on Every Change")->GetBool();
    OPT_SET("App/Auto/Save on Every Change")->SetBool(true);
    BOOST_SCOPE_EXIT_ALL(old) { OPT_SET("App/Auto/Save on Every Change")->SetBool(old); };
    bool fail=true;int saves=0,notices=0;
    agi::signal::Connection on_save=c->subsController->AddFileSaveListener([&]{++saves;if(fail)throw std::runtime_error("disk full");});
    agi::signal::Connection commit=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++notices;Ack(t);});
    auto edit_row=[&](std::string const& text,wxString const& message) {
        auto write=c->subtitleAccess->TryWrite();ASSERT_TRUE(write);
        AssDialogueBase row=LiveRead(*c).Events.front();row.Text=text;
        auto edit=c->subtitleAccess->Edit(write->Lease());edit.UpdateRows({row});
        EXPECT_NO_THROW(edit.Commit(message,AssFile::COMMIT_DIAG_TEXT));
    };
    edit_row("changed while the disk is full","disk full edit");
    EXPECT_EQ(1,saves);EXPECT_EQ(1,notices);
    EXPECT_EQ(wxString("disk full edit"),c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subsController->IsModified());
    fail=false;edit_row("saved again","second edit");
    EXPECT_EQ(2,saves);EXPECT_EQ(2,notices);EXPECT_FALSE(c->subsController->IsModified());
    c->subsController->Undo();EXPECT_EQ("changed while the disk is full",LiveRead(*c).Events.front().Text.get());
}
TEST_F(document_lifecycle, opening_or_closing_inside_an_observer_notification_is_refused_without_side_effects) {
    auto path=Candidate("notify-source");
    int first=LiveRead(*c).Events.front().Id,second=0;
    {
        auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue;row->Text="second";second=row->Id;
        LiveWriteForTest(*c).Events.push_back(*row);LiveWriteForTest(*c).Commit("notify fixture",AssFile::COMMIT_DIAG_ADDREM);
    }
    auto before=Contents();auto filename=c->subsController->Filename();
    RecordMessages messages;
    bool ran=false,notifying=false;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&](Ticket t){
        if(!ran) {
            ran=true;notifying=LiveRead(*c).IsNotifying();
            EXPECT_THROW(c->subsController->Load(path,"utf-8"),agi::InvalidInputException);
            EXPECT_THROW(c->subsController->Close(),agi::InvalidInputException);
            c->project->LoadSubtitles(path);
            c->project->LoadList({path});
            c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
        }
        Ack(t);
    });
    c->selectionController->SetSelectionAndActiveIDs({second},second);
    EXPECT_TRUE(ran);EXPECT_TRUE(notifying);
    EXPECT_EQ(before,Contents());EXPECT_EQ(filename,c->subsController->Filename());
    EXPECT_EQ(nullptr,c->project->AudioProvider());
    EXPECT_EQ(3u,messages.messages.size());
    // Outside the notification the same open works.
    c->subsController->Load(path,"utf-8");EXPECT_EQ("new 模型",LiveRead(*c).Events.front().Text.get());
    (void)first;
}
// C1-C1c-14 (audit C5): once the new model is published, tools and dialogs
// opened while recovery is blocked can subscribe; they read the new model and
// are not part of the retried delivery.
TEST_F(document_lifecycle, subscriptions_are_allowed_while_recovery_is_blocked) {
    auto path=Candidate("subscribe-recovery");c->subsController->Load(path,"utf-8");
    bool fail=true;
    agi::signal::Connection throwing=LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){if(fail)throw std::runtime_error("observer failure");Ack(t);});
    EXPECT_THROW(c->subsController->Close(),SubtitleModelRecoveryBlocked);
    ASSERT_EQ(State::RecoveryBlocked,c->subsController->InstallationStatus());
    int late_commits=0,late_selections=0;std::string seen_text="unset";
    std::optional<agi::signal::Connection> commit,selected;
    EXPECT_NO_THROW(commit.emplace(LiveRead(*c).AddCommitListener([&](int,AssDialogue const*,Ticket t){++late_commits;Ack(t);})));
    EXPECT_NO_THROW(selected.emplace(c->selectionController->AddSelectionListener([&](Ticket t){++late_selections;Ack(t);})));
    EXPECT_NO_THROW({ SubtitleDialogEdit edit(c.get()); edit.ResetActive(); seen_text=LiveRead(*c).Events.front().Text.get(); });
    EXPECT_EQ("",seen_text); // the published (blank) model
    fail=false;EXPECT_TRUE(c->subsController->RetryModelInstallation());
    EXPECT_EQ(State::Committed,c->subsController->InstallationStatus());
    EXPECT_EQ(0,late_commits);EXPECT_EQ(0,late_selections); // not part of the retried delivery
    Input("after recovery");EXPECT_EQ(1,late_commits);
}
// C1-C1c-14 (audit C7): commands that edit the active line do nothing when
// there is none, instead of dereferencing a null line.
TEST_F(ordinary_edit, active_line_commands_do_nothing_without_an_active_line) {
    Input("sample");
    c->selectionController->SetSelectionAndActiveIDs({},0);
    ASSERT_EQ(nullptr,c->selectionController->ReadActiveLine());
    auto description=c->subsController->GetUndoDescription();auto revision=c->subtitleEditGate.Revision();
    EXPECT_FALSE(cmd::get("edit/line/reverse_move")->Validate(c.get()));
    for(auto name:{"edit/line/reverse_move","subtitle/bind","subtitle/unbind","edit/revert","edit/clear","edit/clear/text",
                   "edit/insert_original","edit/font","edit/color/primary","edit/style/bold"})
        EXPECT_NO_THROW(cmd::call(name,c.get())) << name;
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    EXPECT_EQ("sample",LiveRead(*c).Events.front().Text.get());
    (void)revision;
}
// C1-C1c-14 (audit C3): pasted lines keep references that resolve in this
// document and drop the rest.
TEST_F(ordinary_edit, pasted_lines_drop_extradata_references_that_do_not_resolve) {
    uint32_t known=0;
    {auto write=c->subtitleAccess->TryWrite();known=LiveWriteForTest(*c).AddExtradata("opaque","here");
     LiveWriteForTest(*c).Commit("extradata fixture",AssFile::COMMIT_EXTRADATA);}
    auto data="Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{=" + std::to_string(known) + "=9999}pasted";
    cmd::PasteSubtitleLines(c.get(),data);
    AssDialogue const* pasted=nullptr;
    for(auto const& row:LiveRead(*c).Events)if(row.Text.get()=="pasted")pasted=&row;
    ASSERT_NE(nullptr,pasted);
    EXPECT_EQ((std::vector<uint32_t>{known}),pasted->ExtradataIds.get());
}
// C1-C1c-14 (audit C8): an autosave that fails after a manual save already
// covered its version must not move the autosave baseline back.
TEST_F(document_lifecycle, failed_autosave_does_not_undo_a_later_manual_save_baseline) {
    c->subsController->Save(Local("autosave-baseline.ass"),"utf-8");
    auto old_dir=OPT_GET("Path/Auto/Save")->GetString();
    BOOST_SCOPE_EXIT_ALL(old_dir) { OPT_SET("Path/Auto/Save")->SetString(old_dir); };
    auto blocker=std::filesystem::current_path()/"autosave-blocker";
    std::ofstream(blocker) << "a file, so the autosave directory cannot be created";
    OPT_SET("Path/Auto/Save")->SetString((blocker/"dir").string());
    Input("autosaved and saved");
    c->subsController->RequestAutoSave();               // fails on the worker
    c->subsController->Save(Local("autosave-baseline.ass"),"utf-8"); // covers the same version
    for(int i=0;i<300;++i){wxTheApp->ProcessPendingEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    auto good=std::filesystem::current_path()/"autosave-good";
    std::filesystem::remove_all(good); // left by an earlier run
    OPT_SET("Path/Auto/Save")->SetString(good.string());
    c->subsController->RequestAutoSave();               // nothing new since the save
    for(int i=0;i<100;++i){wxTheApp->ProcessPendingEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    EXPECT_FALSE(std::filesystem::exists(good));
}

// ---- C2: Apply Engine ------------------------------------------------------
namespace {
std::vector<std::string> LiveTexts(agi::Context& c) {
    std::vector<std::string> out;
    for (auto const& line : LiveRead(c).Events) out.push_back(line.Text.get());
    return out;
}
std::vector<int> LiveIds(agi::Context& c) {
    std::vector<int> out;
    for (auto const& line : LiveRead(c).Events) out.push_back(line.Id);
    return out;
}
std::vector<subtitle_apply::Choice> AllChanges(SubtitleComparison const& comparison) {
    std::vector<subtitle_apply::Choice> out;
    for (size_t i = 0; i < comparison.Items().size(); ++i)
        if (comparison.Items()[i].kind != agi::ass::compare::Kind::Unchanged) out.push_back({i});
    return out;
}
}
TEST_F(ordinary_edit, comparison_freezes_writers_and_apply_is_one_undoable_step) {
    Input("current");
    auto id = LiveRead(*c).Events.front().Id;
    auto baseline = c->subtitleAccess->Snapshot();
    baseline->Events.front().Text = "from baseline";
    auto added = new AssDialogue; added->Text = "added"; added->Start = 9000; added->End = 9500;
    baseline->Events.push_back(*added);
    auto undo_before = c->subsController->GetUndoDescription();

    auto comparison = c->subsController->BeginComparison(std::move(baseline), agi::ass::compare::All);
    ASSERT_TRUE(comparison.Valid());
    EXPECT_FALSE(c->subtitleAccess->TryWrite());                 // ordinary edits are frozen
    EXPECT_FALSE(c->subtitleEditGate.TryBeginProjectWrite());    // so is project state
    EXPECT_THROW(c->subsController->BeginComparison(c->subtitleAccess->Snapshot(), agi::ass::compare::All), SubtitleApplyRejected);
    Input("ignored while comparing");
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));

    int commits = 0, type = -1;
    agi::signal::Connection listener = LiveRead(*c).AddCommitListener([&](int t, AssDialogue const*) { ++commits; type = t; });
    auto choices = AllChanges(comparison);
    ASSERT_EQ(2u, choices.size());
    EXPECT_TRUE(c->subsController->CheckApply(comparison, choices).Ok());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c)); // dry run changes nothing
    auto report = c->subsController->ApplyComparison(comparison, choices);
    ASSERT_TRUE(report.Ok());
    EXPECT_EQ(1u, report.modified); EXPECT_EQ(1u, report.added);
    EXPECT_FALSE(comparison.Valid());
    EXPECT_EQ(1, commits); EXPECT_EQ(AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL, type);
    EXPECT_EQ((std::vector<std::string>{"from baseline", "added"}), LiveTexts(*c));
    auto applied = LiveIds(*c);
    EXPECT_EQ(id, applied[0]);                                   // the modified row keeps its identity
    EXPECT_EQ(std::set<int>(applied.begin(), applied.end()), c->selectionController->SelectedIDs());
    EXPECT_EQ(id, c->selectionController->ActiveID());
    EXPECT_EQ("apply comparison", c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subsController->IsModified());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());                  // the session is released

    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
    EXPECT_EQ((std::vector<int>{id}), LiveIds(*c));
    EXPECT_EQ(undo_before, c->subsController->GetUndoDescription());
    c->subsController->Redo();
    EXPECT_EQ((std::vector<std::string>{"from baseline", "added"}), LiveTexts(*c));
    EXPECT_EQ(applied, LiveIds(*c));
}
TEST_F(ordinary_edit, refused_apply_changes_nothing_and_keeps_the_comparison) {
    auto baseline = c->subtitleAccess->Snapshot();
    for (auto text : {"g1", "g2"}) {
        auto line = new AssDialogue; line->Text = text; line->Start = 5000; line->End = 6000;
        baseline->Events.push_back(*line);
    }
    auto token = baseline->AddExtradata("aegisub.enhanced.subtitle-binding", "group");
    for (auto& line : baseline->Events) if (line.Text.get() != "") line.ExtradataIds = std::vector<uint32_t>{token};
    auto texts = LiveTexts(*c); auto ids = LiveIds(*c);
    auto undo = c->subsController->GetUndoDescription(); auto revision = c->subtitleEditGate.Revision();

    auto comparison = c->subsController->BeginComparison(std::move(baseline), agi::ass::compare::All);
    auto choices = AllChanges(comparison);
    ASSERT_EQ(2u, choices.size());
    auto report = c->subsController->ApplyComparison(comparison, {choices[0]});
    ASSERT_FALSE(report.Ok());
    ASSERT_EQ(1u, report.refusals.size());
    EXPECT_EQ(subtitle_apply::Problem::PartialBinding, report.refusals[0].problem);
    EXPECT_EQ("group", report.refusals[0].detail);
    EXPECT_EQ(texts, LiveTexts(*c)); EXPECT_EQ(ids, LiveIds(*c));
    EXPECT_EQ(undo, c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.IsCurrent(revision));
    EXPECT_TRUE(LiveRead(*c).Extradata.empty());
    EXPECT_TRUE(comparison.Valid());                             // the user can choose again
    EXPECT_FALSE(c->subtitleAccess->TryWrite());

    // Choosing the whole group applies it, with its binding.
    ASSERT_TRUE(c->subsController->ApplyComparison(comparison, choices).Ok());
    ASSERT_EQ(3u, LiveRead(*c).Events.size());
    ASSERT_EQ(1u, LiveRead(*c).Extradata.size());
    EXPECT_EQ("group", LiveRead(*c).Extradata.front().value);
}
TEST_F(ordinary_edit, ending_a_comparison_releases_it_and_stale_or_foreign_use_is_rejected) {
    {
        auto scope = c->subtitleAccess->TryWrite(); ASSERT_TRUE(scope);
        EXPECT_THROW(c->subsController->BeginComparison(c->subtitleAccess->Snapshot(), agi::ass::compare::All), SubtitleApplyRejected);
    }
    auto baseline = c->subtitleAccess->Snapshot(); baseline->Events.front().Text = "b";
    auto comparison = c->subsController->BeginComparison(std::move(baseline), agi::ass::compare::Text | agi::ass::compare::Start | agi::ass::compare::End);
    ASSERT_EQ(1u, AllChanges(comparison).size());
    comparison.End();
    EXPECT_FALSE(comparison.Valid());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    EXPECT_THROW(c->subsController->ApplyComparison(comparison, {{0}}), SubtitleApplyRejected);
    EXPECT_EQ((std::vector<std::string>{""}), LiveTexts(*c));
    // Choosing only Unchanged items ends nothing and changes nothing.
    auto same = c->subsController->BeginComparison(c->subtitleAccess->Snapshot(), agi::ass::compare::All);
    auto report = c->subsController->ApplyComparison(same, {{0}});
    EXPECT_FALSE(report.Ok()); EXPECT_TRUE(same.Valid());
    auto empty = c->subsController->ApplyComparison(same, {});
    EXPECT_TRUE(empty.Ok()); EXPECT_FALSE(same.Valid());         // nothing to apply: ended unchanged
    EXPECT_EQ((std::vector<std::string>{""}), LiveTexts(*c));
}

// ---- C2-B: comparison window ------------------------------------------------
namespace {
// Records message boxes and answers them; Settle() runs @p action on the
// comparison tab once it is open, then ends the comparison if still open.
class CompareHook : public wxModalDialogHook {
    std::function<void(wxWindow*)> action;
public:
    static inline CompareHook* active = nullptr;
    std::vector<wxString> messages;
    int shown = 0;
    int drop_prompts = 0, save_prompts = 0;
    int drop_choice = wxID_CANCEL, save_choice = wxID_CANCEL;
    explicit CompareHook(std::function<void(wxWindow*)> action) : action(std::move(action)) { Register(); active = this; }
    ~CompareHook() { if (active == this) active = nullptr; }
    void Settle(agi::Context& c) {
        auto page = c.workspaceTabs ? c.workspaceTabs->Comparison() : nullptr;
        if (!page) return;
        ++shown;
        if (action) action(page);
        if (c.workspaceTabs->Comparison()) c.workspaceTabs->CloseComparison(false);
    }
    int Enter(wxDialog* dialog) override {
        if (auto message = dynamic_cast<wxMessageDialog*>(dialog)) {
            if (dialog->GetTitle() == "Compare Dropped Subtitles") { ++drop_prompts; return drop_choice; }
            if (dialog->GetTitle() == "Unsaved changes") { ++save_prompts; return save_choice; }
            messages.push_back(message->GetMessage()); return wxID_OK;
        }
        return wxID_NONE;
    }
    void Exit(wxDialog*) override { }
};
template<class T = wxWindow> T* Named(wxWindow* dialog, char const* name) {
    auto window = dynamic_cast<T*>(dialog->FindWindow(name));
    EXPECT_NE(nullptr, window) << name;
    return window;
}
void Press(wxWindow* dialog, char const* name) {
    auto w = Named(dialog, name); ASSERT_NE(nullptr, w);
    ASSERT_TRUE(w->IsEnabled()) << name;
    if (auto flat = dynamic_cast<FlatButton*>(w)) flat->Press();
    else button(w, w->GetId());
}
wxString Label(wxWindow* dialog, char const* name) {
    auto w = Named(dialog, name);
    return w ? w->GetLabel() : wxString();
}
wxString Report(wxWindow* dialog) {
    auto t = Named<wxTextCtrl>(dialog, "compare-report");
    return t ? t->GetValue() : wxString();
}
// Open the comparison tab for @p path and settle it with the active hook.
// True if the comparison ended by writing to the subtitles.
bool RunCompare(agi::Context* c, agi::fs::path const& path) {
    if (!OpenCompareTab(c, path)) return false;
    if (CompareHook::active) CompareHook::active->Settle(*c);
    return !c->workspaceTabs->Comparison() && c->workspaceTabs->LastApplied();
}
agi::fs::path WriteBaseline(agi::Context& c, std::string const& name, std::function<void(AssFile&)> edit) {
    auto file = c.subtitleAccess->Snapshot();
    edit(*file);
    agi::fs::path path((std::filesystem::current_path() / name).wstring());
    SubtitleFormat::GetWriter(path)->WriteFile(file.get(), path, agi::vfr::Framerate(), "UTF-8");
    return path;
}
}
TEST_F(ordinary_edit, compare_window_writes_decisions_as_one_undo_step) {
    Input("current");
    auto id = LiveRead(*c).Events.front().Id;
    auto path = WriteBaseline(*c, "compare-baseline.ass", [](AssFile& f) {
        f.Events.front().Text = "from baseline";
        auto line = new AssDialogue; line->Text = "added"; line->Start = 9000; line->End = 9500; f.Events.push_back(*line);
    });
    bool writer_blocked = false, unchanged_while_deciding = false;
    wxString counter, finish_first, report, stats;
    CompareHook hook([&](wxWindow* dialog) {
        writer_blocked = !c->subtitleAccess->TryWrite();
        counter = Label(dialog, "compare-counter");
        finish_first = dialog->FindWindow(wxID_OK)->GetLabel();
        EXPECT_NE(wxNOT_FOUND, Label(dialog, "compare-right-title").Find("target project"));
        Press(dialog, "compare-reject-current");          // restore the file's text
        Press(dialog, "compare-next");
        EXPECT_EQ("Difference 2 of 2", Label(dialog, "compare-counter"));
        Press(dialog, "compare-reject-current");          // restore the file's line
        unchanged_while_deciding = LiveTexts(*c) == std::vector<std::string>{"current"};
        report = Report(dialog);
        stats = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_EQ(1, hook.shown); EXPECT_TRUE(hook.messages.empty());
    EXPECT_TRUE(writer_blocked); EXPECT_TRUE(unchanged_while_deciding);
    EXPECT_EQ("Difference 1 of 2", counter);
    EXPECT_EQ("Done", wxStripMenuCodes(finish_first));
    EXPECT_NE(wxNOT_FOUND, report.Find("1 lines modified, 1 added, 0 removed")) << report;
    EXPECT_EQ("0 pending, 0 accepted, 2 rejected, 0 partial", stats);
    EXPECT_EQ((std::vector<std::string>{"from baseline", "added"}), LiveTexts(*c));
    EXPECT_EQ(id, LiveRead(*c).Events.front().Id);
    EXPECT_EQ("apply comparison", c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    c->subsController->Undo();
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, compare_window_accepting_keeps_the_project_unchanged) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-fields.ass", [](AssFile& f) {
        f.Events.front().Text = "baseline text"; f.Events.front().Actor = "baseline actor";
    });
    auto texts = LiveTexts(*c); auto undo = c->subsController->GetUndoDescription();
    // Accepting keeps the open subtitles as they are: Done ends the comparison unchanged.
    CompareHook done([&](wxWindow* dialog) {
        Press(dialog, "compare-accept-current");
        EXPECT_EQ("Done", wxStripMenuCodes(dialog->FindWindow(wxID_OK)->GetLabel()));
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_FALSE(RunCompare(c.get(), path));
    EXPECT_EQ(texts, LiveTexts(*c)); EXPECT_EQ(undo, c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
}
TEST_F(ordinary_edit, compare_window_cancel_with_decisions_confirms_and_changes_nothing) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-cancel.ass", [](AssFile& f) { f.Events.front().Text = "baseline text"; });
    auto texts = LiveTexts(*c); auto undo = c->subsController->GetUndoDescription();
    bool closed = false;
    CompareHook hook([&](wxWindow* dialog) {
        Press(dialog, "compare-reject-current");
        button(dialog->FindWindow(wxID_CANCEL), wxID_CANCEL); // confirmation answered OK by the hook
        closed = !tabs->Comparison();
    });
    EXPECT_FALSE(RunCompare(c.get(), path));
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("discard 1 decisions")) << hook.messages[0];
    EXPECT_TRUE(closed);
    EXPECT_EQ(texts, LiveTexts(*c)); EXPECT_EQ(undo, c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
}
TEST_F(ordinary_edit, compare_window_takes_only_the_chosen_fields) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-fields2.ass", [](AssFile& f) {
        f.Events.front().Text = "baseline text"; f.Events.front().Actor = "baseline actor";
    });
    wxString stats, actor_tip;
    int actor_y = -1, text_y = -1;
    CompareHook hook([&](wxWindow* dialog) {
        // The current difference changed two fields: each gets a detail row
        // under it, with its own arrows in the gutter.
        auto gutter = dialog->FindWindow("compare-gutter"); ASSERT_NE(nullptr, gutter);
        auto hint = Named<wxStaticText>(dialog, "compare-hint"); ASSERT_NE(nullptr, hint);
        auto mouse = [&](wxEventType type, int x, int y) {
            wxMouseEvent event(type); event.SetPosition({x, y}); event.SetEventObject(gutter);
            gutter->GetEventHandler()->ProcessEvent(event);
        };
        for (int y = 0; y < gutter->GetClientSize().GetHeight(); y += 2) {
            mouse(wxEVT_MOTION, 4, y);
            auto h = hint->GetLabelText();
            if (actor_y < 0 && h.Contains("Actor: take the left side")) { actor_y = y; actor_tip = h; }
            if (text_y < 0 && h.Contains("Text: take the left side")) text_y = y;
        }
        ASSERT_GE(actor_y, 0); ASSERT_GE(text_y, 0);
        // » on the Actor row: keep the left (file) side of that field only.
        mouse(wxEVT_LEFT_DOWN, 4, actor_y);
        stats = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_LT(text_y, actor_y); // in field order, text first
    EXPECT_EQ("0 pending, 0 accepted, 0 rejected, 1 partial", stats);
    EXPECT_EQ("current", LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("baseline actor", LiveRead(*c).Events.front().Actor.get());
}
TEST_F(ordinary_edit, compare_window_decides_detail_fields_by_keyboard) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-keys.ass", [](AssFile& f) {
        f.Events.front().Text = "baseline text"; f.Events.front().Actor = "baseline actor";
    });
    wxString resting, first, second, clamped, after_left, after_escape, whole, undone;
    bool still_open = false;
    CompareHook hook([&](wxWindow* dialog) {
        auto pane = dialog->FindWindow("compare-right"); ASSERT_NE(nullptr, pane);
        auto hint = Named<wxStaticText>(dialog, "compare-hint"); ASSERT_NE(nullptr, hint);
        auto key = [&](wxWindow* target, wxEventType type, int code, bool ctrl) {
            wxKeyEvent event(type); event.m_keyCode = code; event.SetControlDown(ctrl); event.SetEventObject(target);
            target->GetEventHandler()->ProcessEvent(event);
        };
        resting = hint->GetLabelText();
        key(pane, wxEVT_KEY_DOWN, WXK_DOWN, true); first = hint->GetLabelText();
        key(pane, wxEVT_KEY_DOWN, WXK_DOWN, true); second = hint->GetLabelText();
        key(pane, wxEVT_KEY_DOWN, WXK_DOWN, true); clamped = hint->GetLabelText(); // stays on the last field
        key(pane, wxEVT_KEY_DOWN, WXK_LEFT, true);                                 // Actor: the left side
        after_left = Label(dialog, "compare-stats");
        key(dialog, wxEVT_CHAR_HOOK, WXK_ESCAPE, false);                           // leaves the field only
        after_escape = hint->GetLabelText();
        still_open = tabs->Comparison() != nullptr;
        key(pane, wxEVT_KEY_DOWN, WXK_RIGHT, true);                                // no field: the whole line
        whole = Label(dialog, "compare-stats");
        Press(dialog, "compare-undo");
        undone = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_NE(wxNOT_FOUND, resting.Find("Ctrl+Up/Down")) << resting;
    EXPECT_TRUE(first.StartsWith("Text: Ctrl+Left")) << first;
    EXPECT_TRUE(second.StartsWith("Actor: Ctrl+Left")) << second;
    EXPECT_EQ(second, clamped);
    EXPECT_EQ("0 pending, 0 accepted, 0 rejected, 1 partial", after_left);
    EXPECT_EQ(resting, after_escape);
    EXPECT_TRUE(still_open);
    EXPECT_EQ("0 pending, 1 accepted, 0 rejected, 0 partial", whole);
    EXPECT_EQ(after_left, undone);
    EXPECT_EQ("current", LiveRead(*c).Events.front().Text.get());
    EXPECT_EQ("baseline actor", LiveRead(*c).Events.front().Actor.get());
}
TEST_F(ordinary_edit, compare_window_filters_by_category_and_leaves_ignored_fields_alone) {
    Input("current");
    auto start = LiveRead(*c).Events.front().Start, end = LiveRead(*c).Events.front().End;
    auto path = WriteBaseline(*c, "compare-filter.ass", [](AssFile& f) {
        f.Events.front().End = f.Events.front().End + 500;      // a time-only change
        auto line = new AssDialogue; line->Text = "added"; line->Start = 9000; line->End = 9500; f.Events.push_back(*line);
    });
    wxString all_counter, time_counter, time_stats, ignored_counter, ignored_stats;
    bool time_hidden = false;
    CompareHook hook([&](wxWindow* dialog) {
        auto toggle = [&](char const* name) {
            Press(dialog, name);
            EXPECT_TRUE(Named<FlatButton>(dialog, name)->IsAccent()) << name; // the chosen category
        };
        auto check = [&](char const* name, bool value) {
            auto b = Named<FlatCheck>(dialog, name); ASSERT_NE(nullptr, b);
            if (b->GetValue() != value) b->Toggle();
        };
        all_counter = Label(dialog, "compare-counter");
        EXPECT_EQ("Time only 1", Named<FlatButton>(dialog, "compare-category-Time")->GetLabel());
        EXPECT_FALSE(Named<FlatButton>(dialog, "compare-category-Text")->IsShown()); // nothing of that kind
        // Only the time change: Reject All covers just what is shown.
        toggle("compare-category-Time");
        time_counter = Label(dialog, "compare-counter");
        Press(dialog, "compare-reject-all");
        time_stats = Label(dialog, "compare-stats");
        Press(dialog, "compare-undo");
        // Ignoring times instead: the time change is no longer a difference.
        toggle("compare-category-all");
        check("compare-ignore-Time", true);
        ignored_counter = Label(dialog, "compare-counter");
        time_hidden = !Named<FlatButton>(dialog, "compare-category-Time")->IsShown();
        Press(dialog, "compare-reject-all");
        ignored_stats = Label(dialog, "compare-stats");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    EXPECT_EQ("Difference 1 of 2", all_counter);
    EXPECT_EQ("Difference 1 of 1 (2 in total)", time_counter);
    EXPECT_EQ("1 pending, 0 accepted, 1 rejected, 0 partial", time_stats);
    EXPECT_EQ("Difference 1 of 1 (2 in total)", ignored_counter);
    EXPECT_TRUE(time_hidden);
    EXPECT_EQ("1 pending, 0 accepted, 1 rejected, 0 partial", ignored_stats);
    ASSERT_EQ(2u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("the 1 differences shown")) << hook.messages[0];
    EXPECT_NE(wxNOT_FOUND, hook.messages[1].Find("Ignored fields keep their current choice: Start, End")) << hook.messages[1];
    // Only the added line was written; the ignored time stayed as it was.
    ASSERT_EQ(2u, LiveRead(*c).Events.size());
    EXPECT_EQ(start, LiveRead(*c).Events.front().Start);
    EXPECT_EQ(end, LiveRead(*c).Events.front().End);
    EXPECT_EQ("added", std::next(LiveRead(*c).Events.begin())->Text.get());
}
TEST_F(ordinary_edit, compare_window_shows_refusals_and_batch_actions_confirm_their_scope) {
    auto path = WriteBaseline(*c, "compare-bound.ass", [](AssFile& f) {
        auto token = f.AddExtradata("aegisub.enhanced.subtitle-binding", "group");
        for (auto text : {"g1", "g2"}) {
            auto line = new AssDialogue; line->Text = text; line->Start = 5000; line->End = 6000;
            line->ExtradataIds = std::vector<uint32_t>{token}; f.Events.push_back(*line);
        }
    });
    auto texts = LiveTexts(*c);
    bool disabled = false, enabled_after_all = false, undone = false;
    wxString report;
    CompareHook hook([&](wxWindow* dialog) {
        Press(dialog, "compare-reject-current");          // only one member of the group
        report = Report(dialog);
        disabled = !dialog->FindWindow(wxID_OK)->IsEnabled();
        Press(dialog, "compare-reject-all");              // asks, the hook answers OK
        enabled_after_all = dialog->FindWindow(wxID_OK)->IsEnabled();
        Press(dialog, "compare-undo");
        undone = !dialog->FindWindow(wxID_OK)->IsEnabled();
        Press(dialog, "compare-redo");
    });
    EXPECT_FALSE(RunCompare(c.get(), path));
    EXPECT_TRUE(disabled); EXPECT_TRUE(enabled_after_all); EXPECT_TRUE(undone);
    EXPECT_NE(wxNOT_FOUND, report.Find("Bound lines must be applied together")) << report;
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("all 2 differences")) << hook.messages[0];
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("1 of them will change")) << hook.messages[0];
    EXPECT_EQ(texts, LiveTexts(*c));                       // closed by the hook: nothing written
}
TEST_F(ordinary_edit, compare_window_with_the_project_on_the_left_accepts_the_file) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-swap.ass", [](AssFile& f) {
        f.Events.front().Text = "revised";
        auto line = new AssDialogue; line->Text = "new line"; line->Start = 9000; line->End = 9500; f.Events.push_back(*line);
    });
    CompareHook hook([&](wxWindow* dialog) {
        Press(dialog, "compare-swap");
        EXPECT_NE(wxNOT_FOUND, Label(dialog, "compare-left-title").Find("target project"));
        auto all = Named<FlatCheck>(dialog, "compare-check-all"); ASSERT_NE(nullptr, all);
        all->Toggle();
        EXPECT_EQ("2 selected", Label(dialog, "compare-checked-count"));
        Press(dialog, "compare-accept-checked");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    EXPECT_TRUE(RunCompare(c.get(), path));
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("the 2 selected differences")) << hook.messages[0];
    EXPECT_EQ((std::vector<std::string>{"revised", "new line"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, compare_page_is_drawn_flat_buttons_checks_and_a_view_switch) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-flat.ass", [](AssFile& f) { f.Events.front().Text = "baseline text"; });
    bool system_controls = true, finish_accent = false, sync_off = false, everything = false;
    wxString stats;
    window->Show(); // Alt shortcuts take only what is on screen
    CompareHook hook([&](wxWindow* dialog) {
        system_controls = false;
        for (auto child : descendants(dialog))
            if (dynamic_cast<wxButton*>(child) || dynamic_cast<wxCheckBox*>(child) || dynamic_cast<wxRadioButton*>(child)) system_controls = true;
        auto finish = dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_OK));
        finish_accent = finish && finish->IsAccent();
        auto view = Named<FlatSwitch>(dialog, "compare-view"); ASSERT_NE(nullptr, view);
        EXPECT_EQ(0, view->GetSelection());               // differences only
        view->Choose(1); everything = view->GetSelection() == 1;
        auto sync = Named<FlatCheck>(dialog, "compare-sync"); ASSERT_NE(nullptr, sync);
        sync->Toggle(); sync_off = !sync->GetValue();
        // Alt with a button's underlined letter presses it, as in a dialog.
        wxKeyEvent alt(wxEVT_CHAR_HOOK); alt.m_keyCode = 'A'; alt.SetAltDown(true);
        dialog->GetEventHandler()->ProcessEvent(alt);
        stats = Label(dialog, "compare-stats");
    });
    EXPECT_FALSE(RunCompare(c.get(), path));
    EXPECT_FALSE(system_controls); EXPECT_TRUE(finish_accent);
    EXPECT_TRUE(everything); EXPECT_TRUE(sync_off);
    EXPECT_EQ("0 pending, 1 accepted, 0 rejected, 0 partial", stats);
}
TEST_F(ordinary_edit, compare_command_refuses_while_editing_and_reports_unreadable_files) {
    CompareHook hook(nullptr);
    {
        auto scope = c->subtitleAccess->TryWrite(); ASSERT_TRUE(scope);
        cmd::call("subtitle/compare", c.get());               // refused before any file dialog
    }
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("being edited"));
    agi::fs::path bad((std::filesystem::current_path() / "compare-bad.xyz").wstring());
    { std::ofstream out(bad); out << "not subtitles"; }
    EXPECT_FALSE(RunCompare(c.get(), bad));
    ASSERT_EQ(2u, hook.messages.size());
    EXPECT_EQ(0, hook.shown);
    // Identical file: told there is nothing to apply, no window.
    auto same = WriteBaseline(*c, "compare-same.ass", [](AssFile&) {});
    EXPECT_FALSE(RunCompare(c.get(), same));
    ASSERT_EQ(3u, hook.messages.size());
    EXPECT_EQ(0, hook.shown);
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
}

TEST_F(ordinary_edit, compare_tab_locks_the_editor_until_the_comparison_ends) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-tab.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    CompareHook hook(nullptr);
    int frame_keys = 0;
    window->Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& e) { ++frame_keys; e.Skip(); });
    auto key = [&](wxWindow* target, int code, int mods) {
        wxKeyEvent event(wxEVT_CHAR_HOOK); event.m_keyCode = code;
        event.SetControlDown(mods & wxMOD_CONTROL); event.SetShiftDown(mods & wxMOD_SHIFT);
        event.SetEventObject(target); target->GetEventHandler()->ProcessEvent(event);
    };
    // Keys in the editor reach the main window as before.
    key(tabs->Book()->GetChildren().front(), 'S', wxMOD_CONTROL);
    EXPECT_EQ(1, frame_keys);
    EXPECT_FALSE(Named(tabs, "workspace-strip")->IsShown());

    ASSERT_TRUE(OpenCompareTab(c.get(), path));
    auto page = tabs->Comparison(); ASSERT_NE(nullptr, page);
    EXPECT_TRUE(tabs->ComparisonInFront());
    EXPECT_TRUE(Named(tabs, "workspace-strip")->IsShown());
    EXPECT_FALSE(Named(tabs, "workspace-banner")->IsShown());
    EXPECT_EQ("Compare: compare-tab.ass", Label(tabs, "workspace-tab-compare"));
    EXPECT_FALSE(c->subtitleAccess->TryWrite());
    // In front, the tab keeps menu and hotkey commands off the locked subtitles.
    EXPECT_FALSE(tabs->AllowsCommand("edit/undo"));
    EXPECT_NE("", Label(tabs, "workspace-notice"));
    EXPECT_TRUE(tabs->AllowsCommand("app/exit"));
    // Its own shortcuts work there, and no key goes on to the main window.
    auto pane = page->FindWindow("compare-right"); ASSERT_NE(nullptr, pane);
    Press(page, "compare-reject-current");
    EXPECT_EQ("0 pending, 0 accepted, 1 rejected, 0 partial", Label(page, "compare-stats"));
    key(pane, 'Z', wxMOD_CONTROL);
    EXPECT_EQ("1 pending, 0 accepted, 0 rejected, 0 partial", Label(page, "compare-stats"));
    key(pane, 'Y', wxMOD_CONTROL);
    EXPECT_EQ("0 pending, 0 accepted, 1 rejected, 0 partial", Label(page, "compare-stats"));
    key(pane, 'S', wxMOD_CONTROL);
    EXPECT_EQ(1, frame_keys);

    // The editor tab says it is locked, and leads back.
    auto edit = Named<WorkspaceTab>(tabs, "workspace-tab-edit"); ASSERT_NE(nullptr, edit);
    edit->Click();
    EXPECT_GT(Named(tabs, "workspace-strip")->GetPosition().y, tabs->Book()->GetPosition().y) << "the strip is below the page";
    EXPECT_FALSE(tabs->ComparisonInFront());
    EXPECT_TRUE(Named(tabs, "workspace-banner")->IsShown());
    EXPECT_NE(wxNOT_FOUND, Label(tabs, "workspace-banner-text").Find("compare-tab.ass"));
    EXPECT_TRUE(tabs->AllowsCommand("edit/undo"));     // runs, and finds the subtitles locked
    EXPECT_FALSE(c->subtitleAccess->TryWrite());
    Press(tabs, "workspace-banner-back");
    EXPECT_TRUE(tabs->ComparisonInFront());

    // Ending it from the tab strip confirms the decision it discards.
    Named<WorkspaceTab>(tabs, "workspace-tab-close")->Click();
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("discard 1 decisions")) << hook.messages[0];
    EXPECT_EQ(nullptr, tabs->Comparison());
    EXPECT_FALSE(tabs->LastApplied());
    EXPECT_FALSE(Named(tabs, "workspace-strip")->IsShown());
    EXPECT_FALSE(Named(tabs, "workspace-banner")->IsShown());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, compare_tabs_sit_in_the_status_bar_of_the_main_window) {
    Input("current");
    auto path = WriteBaseline(*c, "status-tabs.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    CompareHook hook(nullptr);
    window->CreateStatusBar(4);
    auto bar = window->GetStatusBar();
    window->SetSize(1200, 800); window->Show();
    tabs->MoveStripTo(bar);
    auto strip = bar->FindWindow("workspace-strip"); ASSERT_NE(nullptr, strip);
    EXPECT_EQ(nullptr, tabs->FindWindow("workspace-strip"));
    EXPECT_FALSE(strip->IsShown());
    ASSERT_TRUE(OpenCompareTab(c.get(), path));
    EXPECT_TRUE(strip->IsShown());
    wxRect help; ASSERT_TRUE(bar->GetFieldRect(0, help));
    EXPECT_EQ(help.x, strip->GetPosition().x) << "at the left of the status bar";
    auto edit = dynamic_cast<WorkspaceTab*>(bar->FindWindow("workspace-tab-edit")); ASSERT_NE(nullptr, edit);
    edit->Click();
    EXPECT_FALSE(tabs->ComparisonInFront());
    dynamic_cast<WorkspaceTab*>(bar->FindWindow("workspace-tab-compare"))->Click();
    EXPECT_TRUE(tabs->ComparisonInFront());
    dynamic_cast<WorkspaceTab*>(bar->FindWindow("workspace-tab-close"))->Click();
    EXPECT_EQ(nullptr, tabs->Comparison());
    EXPECT_FALSE(strip->IsShown());
}
TEST_F(ordinary_edit, style_panel_switches_between_its_style_and_animation_pages) {
    auto panel=new StylePanel(window,c.get());
    panel->UpdateNow();
    EXPECT_EQ(StylePanel::Page::Style, panel->ShownPage());
    EXPECT_EQ("Style", panel->Shown()["page"]);
    EXPECT_TRUE(panel->IsSectionVisible("text"));
    EXPECT_TRUE(panel->IsSectionShown("transform")); // open at the start
    panel->ShowSection("transform", false);
    auto visible=[&] {
        int shown = 0;
        for (auto child : panel->GetChildren()) if (child->IsShown()) ++shown;
        return shown;
    };
    int on_style = visible();
    panel->ShowPage(StylePanel::Page::Animation);
    EXPECT_EQ("Animation", panel->Shown()["page"]);
    // The switch, the fold button and the animation page
    EXPECT_EQ(3, visible());
    EXPECT_TRUE(panel->Animation()->IsShown());
    panel->ShowPage(StylePanel::Page::Style);
    EXPECT_EQ(on_style, visible());
    EXPECT_FALSE(panel->IsSectionShown("transform")) << "sections come back as they were";
    panel->Destroy();
}
TEST(animation_tags, read_and_write_move_fade_transform_and_origin) {
    using namespace animation;
    // \move over the whole line, and over a part of it
    auto a = Read("{\\move(10,20,30,40)}x", 4000);
    ASSERT_TRUE(a.move); EXPECT_EQ((Move{10,20,30,40,0,4000}), *a.move);
    a = Read("{\\move(10,20,30,40,500,2000)}x", 4000);
    EXPECT_EQ((Move{10,20,30,40,500,2000}), *a.move);
    EXPECT_EQ(4000, Read("{\\move(1,2,3,4,0,0)}x", 4000).move->t2) << "0,0 is the whole line";
    EXPECT_EQ("{\\move(10,20,30,40)}x", WriteMove("x", Move{10,20,30,40,0,4000}, 4000));
    EXPECT_EQ("{\\move(10,20,30,40,0,2000)}x", WriteMove("x", Move{10,20,30,40,0,2000}, 4000)) << "half the line";
    EXPECT_EQ("{\\b1\\move(1,2,3,4)}x", WriteMove("{\\pos(5,6)\\b1}x", Move{1,2,3,4,0,4000}, 4000)) << "no \\pos with \\move";
    EXPECT_EQ("{\\pos(1.5,2)}x", WritePosition("{\\move(1,2,3,4)}x", 1.5, 2));
    EXPECT_EQ(std::make_pair(5.0, 6.0), *Read("{\\pos(5,6)}x", 100).position);

    // \fad reads as the four times of a fade; written back as \fad while it says it
    a = Read("{\\fad(200,300)}x", 4000);
    ASSERT_TRUE(a.fade); EXPECT_EQ((Fade{255,0,255,0,200,3700,4000}), *a.fade);
    EXPECT_TRUE(a.fade->Simple(4000));
    EXPECT_EQ("{\\fad(200,300)}x", WriteFade("x", a.fade, 4000));
    // Gone halfway through: only \fade says it
    Fade early{255,0,255,0,200,1700,2000};
    EXPECT_FALSE(early.Simple(4000));
    EXPECT_EQ("{\\fade(255,0,255,0,200,1700,2000)}x", WriteFade("{\\fad(1,1)}x", early, 4000));
    EXPECT_EQ((Fade{0,128,255,10,20,30,40}), *Read("{\\fade(0,128,255,10,20,30,40)}x", 4000).fade);
    EXPECT_EQ("x", WriteFade("{\\fad(1,1)}x", std::nullopt, 4000));

    // \t: times, acceleration, what it changes to
    a = Read("{\\b1\\t(0,2000,2,\\fscx150\\1c&H0000FF&)\\t(\\frz10)}a{\\t(\\fs5)}b", 4000);
    ASSERT_EQ(2u, a.transforms.size()) << "only the first block's";
    EXPECT_EQ(0, a.transforms[0].t1); EXPECT_EQ(2000, a.transforms[0].t2); EXPECT_EQ(2, a.transforms[0].accel);
    ASSERT_EQ(2u, a.transforms[0].targets.size());
    EXPECT_EQ((Target{"\\fscx","150"}), a.transforms[0].targets[0]);
    EXPECT_EQ((Target{"\\1c","&H0000FF&"}), a.transforms[0].targets[1]);
    EXPECT_EQ(4000, a.transforms[1].t2) << "no times: the whole line";
    a.transforms[1].t1 = 2000;
    EXPECT_EQ("{\\b1\\t(0,2000,2,\\fscx150\\1c&H0000FF&)\\t(2000,4000,\\frz10)}a{\\t(\\fs5)}b",
        WriteTransforms("{\\b1\\t(\\frz1)}a{\\t(\\fs5)}b", a.transforms, 4000));
    EXPECT_EQ("x", WriteTransforms("{\\t(\\fs1)}x", {}, 4000)) << "an empty block goes";
    EXPECT_EQ("{\\t(1.5,\\fs80)}x", WriteTransforms("x", {Transform{0,4000,1.5,{{"\\fs","80"}}}}, 4000));
    auto targets = SplitTargets("\\fscx120\\c&HFF&\\clip(0,0,10,10)\\fs+2");
    ASSERT_EQ(4u, targets.size());
    EXPECT_EQ("\\fscx", targets[0].name); EXPECT_EQ("\\c", targets[1].name);
    EXPECT_EQ("\\clip", targets[2].name); EXPECT_EQ("(0,0,10,10)", targets[2].value);
    EXPECT_EQ("\\fscx120\\c&HFF&\\clip(0,0,10,10)\\fs+2", JoinTargets(targets));

    // \org
    EXPECT_EQ((Origin{3,4}), *Read("{\\org(3,4)}x", 1).origin);
    EXPECT_EQ("{\\b1\\org(7,8)}x", WriteOrigin("{\\org(1,2)\\b1}x", Origin{7,8}));
}
TEST(libresrc, dialog_icons_are_the_monochrome_ones_unless_classic_icons_are_chosen) {
    bool was=libresrc_classic();
    BOOST_SCOPE_EXIT_ALL(&){libresrc_set_classic(was);};
    libresrc_set_classic(false);
    auto modern=GETICONS(options_button);
    // Drawn for each size, 20 px among them, which the bitmaps don't have
    EXPECT_TRUE(modern.GetIcon(wxSize(20,20),wxIconBundle::FALLBACK_NONE).IsOk());
    auto drawn=modern.GetIcon(wxSize(32,32),wxIconBundle::FALLBACK_NONE);
    ASSERT_TRUE(drawn.IsOk());
    wxBitmap expected=libresrc_modern_bundle(libresrc_modern_icon("options_button"),32).GetBitmap(wxSize(32,32));
    wxBitmap got; got.CopyFromIcon(drawn);
    auto a=expected.ConvertToImage(), b=got.ConvertToImage();
    ASSERT_EQ(a.GetSize(),b.GetSize());
    EXPECT_EQ(0,memcmp(a.GetData(),b.GetData(),a.GetWidth()*a.GetHeight()*3)) << "the toolbar's icon";
    libresrc_set_classic(true);
    EXPECT_FALSE(GETICONS(options_button).GetIcon(wxSize(20,20),wxIconBundle::FALLBACK_NONE).IsOk()) << "the coloured bitmaps";
}
TEST(gl_text, draws_each_character_of_utf8_once) {
    // "行结束" is three characters of three bytes each: one glyph apiece
    EXPECT_EQ((std::vector<int>{'1','0',' ',0x884C,0x7ED3,0x675F}),OpenGLText::CodePoints("10 \xE8\xA1\x8C\xE7\xBB\x93\xE6\x9D\x9F"));
    EXPECT_EQ((std::vector<int>{0x1F600}),OpenGLText::CodePoints("\xF0\x9F\x98\x80"));
    // Bytes that aren't UTF-8 are drawn as themselves
    EXPECT_EQ((std::vector<int>{0xE9,'x'}),OpenGLText::CodePoints("\xE9x"));
    EXPECT_EQ((std::vector<int>{0xE8,0xA1}),OpenGLText::CodePoints("\xE8\xA1"));
}
TEST_F(ordinary_edit, animation_page_edits_scroll_and_banner_effects) {
    int w,h; LiveRead(*c).GetResolution(w,h);
    int first, second, third;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto &row=file.Events.front();row.Text="Hello";row.Start=0;row.End=4000;row.Effect="";
     auto other=new AssDialogue(row);other->End=2000;file.Events.push_back(*other);
     auto taken=new AssDialogue(row);taken->Effect="template line";file.Events.push_back(*taken);
     file.Commit("fixture",AssFile::COMMIT_NEW);
     auto it=file.Events.begin();first=it->Id;second=(++it)->Id;third=(++it)->Id;}
    auto row=[&](int which)->AssDialogue const&{auto it=LiveRead(*c).Events.begin();std::advance(it,which);return *it;};
    auto effect=[&](int which){return std::string(row(which).Effect.get());};
    auto set_effect=[&](int which,std::string const& e){auto write=c->subtitleAccess->TryWrite();auto it=LiveWriteForTest(*c).Events.begin();std::advance(it,which);it->Effect=e;LiveWriteForTest(*c).Commit("effect",AssFile::COMMIT_DIAG_META);};
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    auto panel=new StylePanel(window,c.get());
    panel->ShowPage(StylePanel::Page::Animation);
    auto page=panel->Animation();
    auto shown=[&]{page->UpdateNow();return page->Shown();};
    shown();

    // Added: up through the lower half by each line's end
    ASSERT_TRUE(page->CanAdd(AnimationPage::Effect::Scroll));
    page->AddEffect(AnimationPage::Effect::Scroll);
    EXPECT_EQ(FormatScrollEffect(DefaultScrollEffect(ScrollEffect::Kind::ScrollUp,4000,w,h)),effect(0));
    EXPECT_EQ(FormatScrollEffect(DefaultScrollEffect(ScrollEffect::Kind::ScrollUp,2000,w,h)),effect(1));
    EXPECT_EQ("template line",effect(2));
    EXPECT_EQ(_("add scroll effect"),c->subsController->GetUndoDescription());
    EXPECT_EQ("scroll",shown()["cards"]);
    EXPECT_EQ(wxGETTEXT_IN_CONTEXT("scroll effect","Up"),shown()["scroll.kind"]);
    EXPECT_EQ(wxString::Format("%d",h/2),shown()["scroll.top"]);
    EXPECT_EQ(wxString::Format("%d",h),shown()["scroll.bottom"]);
    EXPECT_EQ(ScrollSpeedText(*ParseScrollEffect(effect(0)),4000),shown()["scroll.speed"]);
    EXPECT_FALSE(page->CanAdd(AnimationPage::Effect::Scroll));
    c->subsController->Undo();
    EXPECT_EQ("",effect(0)); EXPECT_EQ("",effect(1)) << "one step";
    EXPECT_EQ("",shown()["cards"]);
    page->AddEffect(AnimationPage::Effect::Scroll); shown();

    // A drag is one step, to every selected line
    page->EnterNumber("scroll.top",100,false); page->EnterNumber("scroll.top",120,true);
    EXPECT_EQ(120,ParseScrollEffect(effect(0))->Top()); EXPECT_EQ(120,ParseScrollEffect(effect(1))->Top());
    EXPECT_EQ(_("set scroll effect"),c->subsController->GetUndoDescription());
    c->subsController->Undo();
    EXPECT_EQ(h/2,ParseScrollEffect(effect(0))->Top()) << "the drag undone in one step";
    shown();
    page->EnterNumber("scroll.top",120,true); shown();
    page->EnterNumber("scroll.delay",40,true);
    EXPECT_EQ(40,ParseScrollEffect(effect(0))->delay); EXPECT_EQ(40,ParseScrollEffect(effect(1))->delay);
    shown();
    // Through the area by each line's own end
    page->Press("scroll.fit");
    EXPECT_EQ(DelayForShift(4000,h-120),ParseScrollEffect(effect(0))->delay);
    EXPECT_EQ(DelayForShift(2000,h-120),ParseScrollEffect(effect(1))->delay);
    shown();

    // Down keeps the area and speed; a banner starts anew
    int delay=ParseScrollEffect(effect(0))->delay;
    page->Choose("scroll.kind",2);
    auto down=*ParseScrollEffect(effect(0));
    EXPECT_EQ(ScrollEffect::Kind::ScrollDown,down.kind); EXPECT_EQ(120,down.Top()); EXPECT_EQ(delay,down.delay);
    EXPECT_EQ(wxGETTEXT_IN_CONTEXT("scroll effect","Down"),shown()["scroll.kind"]);
    page->Choose("scroll.kind",0);
    EXPECT_EQ(FormatScrollEffect(DefaultScrollEffect(ScrollEffect::Kind::Banner,4000,w,h)),effect(0));
    auto banner=shown();
    EXPECT_EQ(0u,banner.count("scroll.top")) << "a banner has no area";
    EXPECT_EQ(_("Right to left"),banner["scroll.direction"]);
    page->Choose("scroll.direction",1);
    EXPECT_TRUE(ParseScrollEffect(effect(0))->left_to_right); EXPECT_TRUE(ParseScrollEffect(effect(1))->left_to_right);
    shown();

    // VSFilter's fade: on, set, off
    EXPECT_EQ(0u,shown().count("scroll.fade"));
    page->Press("scroll.fade_on");
    EXPECT_TRUE(ParseScrollEffect(effect(0))->fade.has_value());
    shown(); page->EnterNumber("scroll.fade",30,true);
    EXPECT_EQ(30,*ParseScrollEffect(effect(0))->fade);
    shown(); page->Press("scroll.fade_off");
    EXPECT_FALSE(ParseScrollEffect(effect(0))->fade.has_value());
    EXPECT_EQ("Banner;"+std::to_string(ParseScrollEffect(effect(0))->delay)+";1",effect(0)) << "no fade written";

    // Only the selected lines of the line's kind change; anything else stays
    set_effect(1,"Scroll up;0;100;10");
    c->selectionController->SetSelectionAndActiveIDs({first,second,third},first);
    auto mixed=shown();
    EXPECT_EQ(fmt_plural(1,"Changes the %d selected line with a banner","Changes the %d selected lines with a banner",1),mixed["scroll.lines"]);
    page->EnterNumber("scroll.delay",77,true);
    EXPECT_EQ(77,ParseScrollEffect(effect(0))->delay);
    EXPECT_EQ("Scroll up;0;100;10",effect(1)); EXPECT_EQ("template line",effect(2));

    // Where it stands: libass takes one axis from \pos
    EXPECT_EQ(0u,shown().count("scroll.note"));
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).Events.front().Text="{\\pos(10,20)}Hello";LiveWriteForTest(*c).Commit("pos",AssFile::COMMIT_DIAG_TEXT);}
    EXPECT_FALSE(shown()["scroll.note"].empty());

    // ✕ clears the Effect field of the lines of its kind, not the text
    page->RemoveCard("scroll");
    EXPECT_EQ("",effect(0)); EXPECT_EQ("{\\pos(10,20)}Hello",row(0).Text.get());
    EXPECT_EQ("Scroll up;0;100;10",effect(1)); EXPECT_EQ("template line",effect(2));
    EXPECT_EQ(_("remove effect"),c->subsController->GetUndoDescription());

    // A field holding something else is not offered or changed
    c->selectionController->SetSelectionAndActiveIDs({third},third); shown();
    EXPECT_FALSE(page->CanAdd(AnimationPage::Effect::Scroll));
    page->AddEffect(AnimationPage::Effect::Scroll);
    EXPECT_EQ("template line",effect(2));
    EXPECT_EQ(0u,shown().count("scroll.title"));

    // The Effect field's list shows the card when the panel is out: added
    // where the field is empty, without the form
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    panel->ShowPage(StylePanel::Page::Style);
    panel->Show(); window->Show(); wxTheApp->ProcessPendingEvents();
    ASSERT_TRUE(panel->ShowScrollEffect());
    EXPECT_EQ(StylePanel::Page::Animation,panel->ShownPage());
    EXPECT_EQ(FormatScrollEffect(DefaultScrollEffect(ScrollEffect::Kind::ScrollUp,4000,w,h)),effect(0));
    EXPECT_EQ("scroll",shown()["cards"]);
    // Hidden, it leaves the form to the edit box
    panel->Hide();
    EXPECT_FALSE(panel->ShowScrollEffect());
    panel->Destroy();
}
TEST_F(ordinary_edit, animation_page_edits_the_selected_lines_effects_in_times_of_the_video) {
    int first, second;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto &row=file.Events.front();row.Text="Hello";row.Start=62300;row.End=66300;
     auto other=new AssDialogue(row);other->Start=70000;other->End=72000;other->Text="World";
     file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);
     first=file.Events.front().Id; second=file.Events.back().Id;}
    auto text=[&](int which) { auto it=LiveRead(*c).Events.begin(); if (which) ++it; return std::string(it->Text.get()); };
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    auto panel=new StylePanel(window,c.get());
    panel->ShowPage(StylePanel::Page::Animation);
    auto page=panel->Animation();
    auto shown=[&] { page->UpdateNow(); return page->Shown(); };
    EXPECT_EQ("", shown()["cards"]);
    EXPECT_EQ(wxS("0:01:02.30 → 0:01:06.30"), shown()["line"]);

    // Move: standing where the line is until its end is moved
    page->AddEffect(AnimationPage::Effect::Move);
    ASSERT_EQ(0u, text(0).find("{\\move(")) << text(0);
    EXPECT_EQ("move", shown()["cards"]);
    EXPECT_EQ("0:01:02.30", shown()["move.in"]); EXPECT_EQ("0:01:06.30", shown()["move.out"]);
    EXPECT_EQ("100", shown()["move.out%"]);
    page->EnterNumber("move.x1", 100, true); shown();
    page->EnterNumber("move.y1", 200, true); shown();
    page->EnterNumber("move.x2", 500, false); page->EnterNumber("move.x2", 600, false); page->EnterNumber("move.y2", 300, true);
    EXPECT_EQ("{\\move(100,200,600,300)}Hello", text(0));
    // The movement takes only half the line
    shown(); page->EnterNumber("move.out%", 50, true);
    EXPECT_EQ("{\\move(100,200,600,300,0,2000)}Hello", text(0));
    EXPECT_EQ("0:01:04.30", shown()["move.out"]); EXPECT_EQ("50", shown()["move.out%"]);
    // Typed as a time of the video
    EXPECT_TRUE(page->TypeInto("move.in", "0:01:03.30"));
    EXPECT_EQ("{\\move(100,200,600,300,1000,2000)}Hello", text(0));
    shown(); page->DragPoint("move", 1, 3000, true);
    EXPECT_EQ("{\\move(100,200,600,300,1000,3000)}Hello", text(0));
    EXPECT_EQ("1000,3000/4000", shown()["move.bar"]);
    shown(); page->Press("move.whole");
    EXPECT_EQ("{\\move(100,200,600,300)}Hello", text(0));
    shown(); page->Press("move.second_half");
    EXPECT_EQ("{\\move(100,200,600,300,2000,4000)}Hello", text(0));
    // Reversed: back the way it came, at the same times
    shown(); page->Press("move.reverse");
    EXPECT_EQ("{\\move(600,300,100,200,2000,4000)}Hello", text(0));
    shown(); page->Press("move.reverse");
    EXPECT_EQ("{\\move(100,200,600,300,2000,4000)}Hello", text(0));
    EXPECT_EQ(" disabled", shown()["move.in_now"].Right(9)) << "no video, no time of it";

    // Fade: \fad until it no longer says it
    page->AddEffect(AnimationPage::Effect::Fade);
    EXPECT_EQ("{\\move(100,200,600,300,2000,4000)\\fad(300,300)}Hello", text(0));
    EXPECT_EQ("move fade", shown()["cards"]);
    EXPECT_EQ(wxS("Fade \\fad"), shown()["fade.title"]);
    page->EnterNumber("fade.out_end%", 50, true);
    EXPECT_EQ("{\\move(100,200,600,300,2000,4000)\\fade(255,0,255,0,300,2000,2000)}Hello", text(0)) << "out before the end";
    EXPECT_EQ(wxS("Fade \\fade"), shown()["fade.title"]);
    page->EnterNumber("fade.during", 50, true);
    EXPECT_NE(std::string::npos, text(0).find("\\fade(255,128,255,")) << text(0);

    // Transform: to what the line has, then dragged
    page->AddEffect(AnimationPage::Effect::Transform, "\\fscx");
    EXPECT_EQ("move fade t0", shown()["cards"]);
    EXPECT_NE(std::string::npos, text(0).find("\\t(\\fscx100)")) << text(0);
    page->EnterNumber("t0.target0", 150, true); shown();
    page->EnterNumber("t0.out%", 50, true); shown();
    page->EnterNumber("t0.accel", 2, true);
    EXPECT_NE(std::string::npos, text(0).find("\\t(0,2000,2,\\fscx150)")) << text(0);
    page->AddTarget(0, "\\frz"); shown();
    EXPECT_NE(std::string::npos, text(0).find("\\t(0,2000,2,\\fscx150\\frz0)")) << text(0);
    page->Press("t0.target1.remove");
    EXPECT_NE(std::string::npos, text(0).find("\\t(0,2000,2,\\fscx150)")) << text(0);

    // Origin, then each card taken away
    page->AddEffect(AnimationPage::Effect::Origin);
    EXPECT_EQ("move fade t0 origin", shown()["cards"]);
    page->RemoveCard("move");
    EXPECT_NE(std::string::npos, text(0).find("\\pos(100,200)")) << "it stays where it started: " << text(0);
    EXPECT_EQ(std::string::npos, text(0).find("\\move")) << text(0);
    page->RemoveCard("t0"); page->RemoveCard("fade"); page->RemoveCard("origin");
    EXPECT_EQ("{\\pos(100,200)}Hello", text(0));
    EXPECT_EQ("", shown()["cards"]);
    // One undo step each
    c->subsController->Undo();
    EXPECT_NE(std::string::npos, text(0).find("\\org(")) << text(0);

    // A share of the line is of each line's own length
    c->selectionController->SetSelectionAndActiveIDs({first, second}, first);
    shown(); page->RemoveCard("origin");
    page->AddEffect(AnimationPage::Effect::Fade); shown();
    page->EnterNumber("fade.in_end%", 50, true);
    EXPECT_EQ("{\\pos(100,200)\\fad(2000,300)}Hello", text(0));
    EXPECT_EQ("{\\fad(1000,300)}World", text(1));
    panel->Destroy();
}
TEST(timing_adjust, lead_stops_at_lines_it_did_not_overlap_and_moves_only_its_targets) {
    auto line=[](int start,int end){auto d=std::make_unique<AssDialogue>();d->Start=start;d->End=end;return d;};
    auto a=line(1000,2000), b=line(2100,3000), c=line(2900,4000);
    // Only b: held by a before it, not by c, which it already overlapped
    AddLead({b.get()},{a.get(),b.get(),c.get()},300,300);
    EXPECT_EQ(2000,int(b->Start)); EXPECT_EQ(3300,int(b->End));
    EXPECT_EQ(1000,int(a->Start)); EXPECT_EQ(2000,int(a->End)) << "lines that are not targets stay";
    EXPECT_EQ(2900,int(c->Start)); EXPECT_EQ(4000,int(c->End));
    // All of them, as the timing post-processor has it: lead-ins first
    a=line(1000,2000); b=line(2100,3000); c=line(2900,4000);
    std::vector<AssDialogue*> all{a.get(),b.get(),c.get()};
    AddLead(all,all,300,300);
    EXPECT_EQ(700,int(a->Start)); EXPECT_EQ(2000,int(a->End));
    EXPECT_EQ(2000,int(b->Start)); EXPECT_EQ(3300,int(b->End));
    EXPECT_EQ(2600,int(c->Start)); EXPECT_EQ(4300,int(c->End));
    // Counted as the grid's column does
    EXPECT_EQ(-1,CharactersPerSecond("Hello",100,true,true));
    EXPECT_EQ(5,CharactersPerSecond("Hello, world",2000,true,true));
    EXPECT_EQ(6,CharactersPerSecond("Hello, world",2000,false,false));
    // Joined where close, meeting a share of the way across the gap
    a=line(1000,2000); b=line(2100,3000); c=line(2960,4000);
    std::vector<AssDialogue*> close{a.get(),b.get(),c.get()};
    EXPECT_EQ(2,CountJoins({{1000,2000},{2100,3000},{2960,4000}},100,50));
    EXPECT_EQ(1,CountJoins({{1000,2000},{2100,3000},{2960,4000}},99,50)) << "a gap over the limit";
    EXPECT_EQ(2,JoinAdjacent(close,100,50,0.5));
    EXPECT_EQ(2050,int(a->End)); EXPECT_EQ(2050,int(b->Start));
    EXPECT_EQ(2980,int(b->End)); EXPECT_EQ(2980,int(c->Start));
    // Shifted by time, or by frames as a start or an end
    agi::vfr::Framerate fps(25,1);
    EXPECT_EQ(1500,ShiftTime(1000,500,true,fps,agi::vfr::START));
    EXPECT_EQ(fps.TimeAtFrame(fps.FrameAtTime(1000,agi::vfr::START)+2,agi::vfr::START),ShiftTime(1000,2,false,fps,agi::vfr::START));
    EXPECT_EQ(fps.TimeAtFrame(fps.FrameAtTime(1000,agi::vfr::END)-2,agi::vfr::END),ShiftTime(1000,-2,false,fps,agi::vfr::END));
}

TEST_F(ordinary_edit, timing_page_edits_times_snaps_to_scene_changes_and_adds_lead) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    agi::fs::path kf((std::filesystem::current_path()/"timing-page-keyframes.txt").wstring());
    agi::keyframe::Save(kf,{0,50,150});
    c->project->LoadKeyframes(kf);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseKeyframes(); c->project->CloseVideo(); OPT_SET("App/Style Panel/Page")->SetInt(0); };
    int first, second;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto &row=file.Events.front();row.Text="Hello, world";row.Start=1600;row.End=5600;
     auto other=new AssDialogue(row);other->Start=7000;other->End=8000;other->Text="Next";
     file.Events.push_back(*other);
     file.Commit("fixture",AssFile::COMMIT_NEW);
     first=file.Events.front().Id; second=file.Events.back().Id;}
    auto times=[&](int which){auto it=LiveRead(*c).Events.begin();if(which)++it;return std::pair<int,int>(it->Start,it->End);};
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    auto panel=new StylePanel(window,c.get());
    panel->ShowPage(StylePanel::Page::Timing);
    EXPECT_EQ(2,OPT_GET("App/Style Panel/Page")->GetInt());
    auto page=panel->Timing();
    auto shown=[&]{page->UpdateNow();return page->Shown();};
    auto vc=c->videoController.get();
    auto description=c->subsController->GetUndoDescription();

    EXPECT_EQ("time scene lead join shift check",shown()["cards"]);
    EXPECT_EQ(wxS("0:00:01.60 → 0:00:05.60"),shown()["line"]);
    EXPECT_EQ(fmt_tl("%d characters per second",CharactersPerSecond("Hello, world",4000,true,true)),shown()["summary.cps"]);
    EXPECT_EQ(_("No line before it"),shown()["summary.prev"]);
    EXPECT_EQ(fmt_tl("%d ms before the line after",1400),shown()["summary.next"]);
    // Over the grid's thresholds
    OPT_SET("Subtitle/Character Counter/CPS Warning Threshold")->SetInt(1);
    OPT_SET("Subtitle/Character Counter/CPS Error Threshold")->SetInt(5);
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Subtitle/Character Counter/CPS Warning Threshold")->SetInt(15); OPT_SET("Subtitle/Character Counter/CPS Error Threshold")->SetInt(30); };
    EXPECT_EQ(wxS(" warning"),shown()["summary.cps"].Right(8));
    OPT_SET("Subtitle/Character Counter/CPS Error Threshold")->SetInt(2);
    EXPECT_EQ(wxS(" error"),shown()["summary.cps"].Right(6));

    // Scene changes: frame 50 is 10 frames after the start, frame 150 ends
    // the scene the end is in. Moving the start in that far needs more than
    // the few frames a start may move later by default.
    EXPECT_EQ(fmt_tl("Start: none within %d ms",100),shown()["scene.start"]);
    EXPECT_EQ(wxS(" disabled"),shown()["scene.snap_start"].Right(9));
    BOOST_SCOPE_EXIT_ALL(&){
        for(auto o:{"Start Earlier","End Later"})OPT_SET(std::string("Timing/Scene Snap/")+o)->SetInt(500);
        for(auto o:{"Start Later","End Earlier"})OPT_SET(std::string("Timing/Scene Snap/")+o)->SetInt(100); };
    shown(); page->EnterNumber("scene.in",500,true);
    EXPECT_EQ(500,OPT_GET("Timing/Scene Snap/Start Later")->GetInt());
    EXPECT_EQ(500,OPT_GET("Timing/Scene Snap/End Earlier")->GetInt());
    EXPECT_EQ(500,OPT_GET("Timing/Scene Snap/Start Earlier")->GetInt()) << "out is set apart";
    EXPECT_EQ(fmt_plural(10,"Start %d frame early","Start %d frames early",10),shown()["scene.start"]);
    page->Press("scene.snap_start");
    EXPECT_EQ(c->project->Timecodes().TimeAtFrame(50,agi::vfr::START),times(0).first);
    EXPECT_EQ(5600,times(0).second) << "only the start";
    EXPECT_EQ(_("Start on a scene change"),shown()["scene.start"]);
    EXPECT_EQ(wxS(" disabled"),shown()["scene.snap_start"].Right(9));
    page->Press("scene.snap_end");
    EXPECT_EQ(c->project->Timecodes().TimeAtFrame(149,agi::vfr::END),times(0).second);
    c->subsController->Undo(); c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0));
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    // The end moves out: the out distance is the one that reaches it
    shown(); page->EnterNumber("scene.out",100,true);
    EXPECT_EQ(100,OPT_GET("Timing/Scene Snap/Start Earlier")->GetInt());
    EXPECT_EQ(100,OPT_GET("Timing/Scene Snap/End Later")->GetInt());
    EXPECT_EQ(fmt_tl("End: none within %d ms",100),shown()["scene.end"]);
    EXPECT_EQ(wxS(" disabled"),shown()["scene.snap_end"].Right(9));
    shown(); page->EnterNumber("scene.out",500,true);
    shown(); page->Press("scene.both");
    EXPECT_EQ(std::make_pair(c->project->Timecodes().TimeAtFrame(50,agi::vfr::START),c->project->Timecodes().TimeAtFrame(149,agi::vfr::END)),times(0));
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0));

    // A drag is one step, snapped to the video's frames: at 25 fps a
    // frame's start time is 20 ms before its timestamp
    shown(); page->EnterNumber("time.start",1630,false); page->EnterNumber("time.start",1690,true);
    EXPECT_EQ(1700,times(0).first);
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0));
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    EXPECT_TRUE(page->TypeInto("time.end","0:00:06.00"));
    EXPECT_EQ(5980,times(0).second);
    c->subsController->Undo();
    // Two lines: the same move for each, the same length for each
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    shown(); page->EnterNumber("time.start",2000,true);
    EXPECT_EQ(1980,times(0).first); EXPECT_EQ(7380,times(1).first);
    c->subsController->Undo();
    shown(); page->EnterNumber("time.duration",1000,true);
    EXPECT_EQ(std::make_pair(1600,2580),times(0)); EXPECT_EQ(std::make_pair(7000,7980),times(1));
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0)); EXPECT_EQ(std::make_pair(7000,8000),times(1));

    // A frame on and back; the video's frame
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    shown(); page->Press("time.start_next");
    EXPECT_EQ(1620,times(0).first);
    shown(); page->Press("time.end_prev");
    EXPECT_EQ(vc->TimeAtFrame(vc->FrameAtTime(5600,agi::vfr::END)-1,agi::vfr::END),times(0).second);
    c->subsController->Undo(); c->subsController->Undo();
    vc->JumpToFrame(30);
    shown(); page->Press("time.start_now");
    EXPECT_EQ(vc->TimeAtFrame(30,agi::vfr::START),times(0).first);
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0));

    // Lead: the post-processor's lengths, not over the line after
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto it=file.Events.begin();++it;it->Start=5700;file.Commit("closer",AssFile::COMMIT_DIAG_TIME);}
    auto lead_in=OPT_GET("Tool/Timing Post Processor/Lead/IN")->GetInt(), lead_out=OPT_GET("Tool/Timing Post Processor/Lead/OUT")->GetInt();
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Tool/Timing Post Processor/Lead/IN")->SetInt(lead_in); OPT_SET("Tool/Timing Post Processor/Lead/OUT")->SetInt(lead_out); };
    shown(); page->EnterNumber("lead.in",200,true); page->EnterNumber("lead.out",300,true);
    EXPECT_EQ(200,OPT_GET("Tool/Timing Post Processor/Lead/IN")->GetInt());
    EXPECT_EQ(300,OPT_GET("Tool/Timing Post Processor/Lead/OUT")->GetInt());
    EXPECT_EQ(wxS("200"),shown()["lead.in"]);
    auto before_lead=c->subsController->GetUndoDescription();
    page->Press("lead.add_both");
    EXPECT_EQ(std::make_pair(1400,5700),times(0));
    EXPECT_EQ(std::make_pair(5700,8000),times(1)) << "not selected, not moved";
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0));
    EXPECT_EQ(before_lead,c->subsController->GetUndoDescription());
    shown(); page->Press("lead.add_in");
    EXPECT_EQ(std::make_pair(1400,5600),times(0));
    c->subsController->Undo();

    // Joining: the post-processor's limits, the selected lines only
    auto gap=OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->GetInt();
    auto overlap=OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Overlap")->GetInt();
    auto bias=OPT_GET("Tool/Timing Post Processor/Adjacent Bias")->GetDouble();
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->SetInt(gap); OPT_SET("Tool/Timing Post Processor/Threshold/Adjacent Overlap")->SetInt(overlap); OPT_SET("Tool/Timing Post Processor/Adjacent Bias")->SetDouble(bias); };
    EXPECT_EQ(_("Select two or more lines to join them"),shown()["join.note"]);
    EXPECT_EQ(wxS(" disabled"),shown()["join.apply"].Right(9));
    c->selectionController->SetSelectionAndActiveIDs({first,second},first);
    shown(); page->EnterNumber("join.gap",200,true); page->EnterNumber("join.bias",50,true);
    EXPECT_EQ(200,OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->GetInt());
    EXPECT_DOUBLE_EQ(0.5,OPT_GET("Tool/Timing Post Processor/Adjacent Bias")->GetDouble());
    EXPECT_EQ(fmt_plural(1,"%d place to join among the selected lines","%d places to join among the selected lines",1),shown()["join.note"]);
    auto before_join=c->subsController->GetUndoDescription();
    page->Press("join.apply");
    EXPECT_EQ(std::make_pair(1600,5650),times(0)); EXPECT_EQ(std::make_pair(5650,8000),times(1));
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0)); EXPECT_EQ(std::make_pair(5700,8000),times(1));
    EXPECT_EQ(before_join,c->subsController->GetUndoDescription());
    shown(); page->EnterNumber("join.gap",50,true);
    EXPECT_EQ(_("None of the selected lines is close enough to the next to join"),shown()["join.note"]);
    EXPECT_EQ(wxS(" disabled"),shown()["join.apply"].Right(9));

    // Shifting: by Shift Times' amount, in milliseconds or frames
    auto by_time=OPT_GET("Tool/Shift Times/ByTime")->GetBool(); auto shift_ms=OPT_GET("Tool/Shift Times/Time")->GetInt();
    auto shift_frames=OPT_GET("Tool/Shift Times/Frames")->GetInt(); auto shift_back=OPT_GET("Tool/Shift Times/Direction")->GetBool();
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Tool/Shift Times/ByTime")->SetBool(by_time); OPT_SET("Tool/Shift Times/Time")->SetInt(shift_ms); OPT_SET("Tool/Shift Times/Frames")->SetInt(shift_frames); OPT_SET("Tool/Shift Times/Direction")->SetBool(shift_back); };
    shown(); page->Choose("shift.unit",0); shown(); page->EnterNumber("shift.amount",500,true);
    EXPECT_EQ(500,OPT_GET("Tool/Shift Times/Time")->GetInt());
    EXPECT_EQ(wxS("500"),shown()["shift.amount"]);
    page->Press("shift.later");
    EXPECT_EQ(std::make_pair(2100,6100),times(0)); EXPECT_EQ(std::make_pair(6200,8500),times(1)) << "both selected lines";
    EXPECT_FALSE(OPT_GET("Tool/Shift Times/Direction")->GetBool()) << "Shift Times opens going forward";
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0));
    c->selectionController->SetSelectionAndActiveIDs({first},first);
    shown(); page->Choose("shift.unit",1);
    EXPECT_FALSE(OPT_GET("Tool/Shift Times/ByTime")->GetBool());
    shown(); page->EnterNumber("shift.amount",2,true);
    EXPECT_EQ(2,OPT_GET("Tool/Shift Times/Frames")->GetInt());
    shown(); page->Press("shift.earlier");
    auto const& fps=c->project->Timecodes();
    EXPECT_EQ(std::make_pair(ShiftTime(1600,-2,false,fps,agi::vfr::START),ShiftTime(5600,-2,false,fps,agi::vfr::END)),times(0));
    EXPECT_TRUE(OPT_GET("Tool/Shift Times/Direction")->GetBool());
    c->subsController->Undo();
    EXPECT_EQ(std::make_pair(1600,5600),times(0));

    // Without the detected scene changes, a way to find them
    c->project->CloseKeyframes();
    EXPECT_EQ(1u,shown().count("scene.detect"));

    // The panel opens on the page last shown
    auto again=new StylePanel(window,c.get());
    EXPECT_EQ(StylePanel::Page::Timing,again->ShownPage());
    again->ShowPage(StylePanel::Page::Style);
    EXPECT_EQ(0,OPT_GET("App/Style Panel/Page")->GetInt());
    again->Destroy();
    panel->Destroy();
}

TEST(timing_check, finds_short_fast_overlapping_close_and_nearly_cut_lines) {
    agi::vfr::Framerate fps(25,1);
    std::vector<std::unique_ptr<AssDialogue>> owned;
    auto line=[&](int start,int end,std::string text,std::string style="Default",bool comment=false){
        auto d=std::make_unique<AssDialogue>();d->Start=start;d->End=end;d->Text=text;d->Style=style;d->Comment=comment;
        d->Row=int(owned.size());owned.push_back(std::move(d));return owned.back().get();};
    line(1000,3000,"Hi");                                   // 0 fine
    line(4000,4490,"Hi","S1");                              // 1 too short
    line(5000,5500,"Hi","S2");                              // 2 just long enough
    line(6000,7000,"abcdefghijklmnopqrst","S3");            // 3 20 a second: fast
    line(7000,8000,std::string(40,'a'),"S4");               // 4 40 a second: too fast
    line(8000,9000,"Hi","A");                               // 5
    line(8800,9500,"Hi","A");                               // 6 overlaps 5 by 200
    line(8900,9600,"Hi","B");                               // 7 another style: not
    line(8000,10000,"Hi","A",true);                         // 8 a comment: not
    line(fps.TimeAtFrame(52,agi::vfr::START),fps.TimeAtFrame(52,agi::vfr::START)+1500,"Hi","C"); // 9 starts 2 frames after the cut at 50
    line(fps.TimeAtFrame(50,agi::vfr::START),fps.TimeAtFrame(50,agi::vfr::START)+1500,"Hi","D"); // 10 on it
    line(fps.TimeAtFrame(55,agi::vfr::START),fps.TimeAtFrame(55,agi::vfr::START)+1500,"Hi","E"); // 11 5 frames: too far to be meant
    line(fps.TimeAtFrame(100,agi::vfr::START),fps.TimeAtFrame(148,agi::vfr::END),"Hi","F");      // 12 ends a frame before the cut's own frame before
    line(11000,12000,"Hi","G");                             // 13
    line(12060,13000,"Hi","G");                             // 14 60 ms after 13: too close
    line(13100,14000,"Hi","G");                             // 15 100 ms after: far enough
    line(14000,15000,"Hi","G");                             // 16 right after: joined, not a gap
    std::vector<AssDialogue const*> lines; for(auto const& d:owned) lines.push_back(d.get());
    TimingCheckLimits limits; limits.min_duration=500; limits.min_gap=100; limits.cps_warning=15; limits.cps_error=30; limits.cut_frames=3;
    auto list=[&](std::vector<TimingIssue> const& issues){std::string out;for(auto const& i:issues){
        out+=std::to_string(i.row)+":"+"SFOGN"[int(i.kind)]+(i.end?"e":"")+(i.severe?"!":"")+"="+std::to_string(i.value)+(i.other_row>=0?"/"+std::to_string(i.other_row):"")+" ";}return out;};
    std::vector<int> kf{0,50,150};
    EXPECT_EQ("1:S=490 3:F=20 4:F!=40 6:O=200/5 9:N=-2 12:Ne=1 14:G=60/13 ",list(FindTimingIssues(lines,nullptr,limits,kf,0,fps)));
    // Only the selected: still overlapping the one that is not
    std::set<int> only{owned[6]->Id,owned[0]->Id};
    EXPECT_EQ("6:O=200/5 ",list(FindTimingIssues(lines,&only,limits,kf,0,fps)));
    // No keyframes, no scene changes to miss
    EXPECT_EQ("1:S=490 3:F=20 4:F!=40 6:O=200/5 14:G=60/13 ",list(FindTimingIssues(lines,nullptr,limits,{},0,fps)));
    // Two frames is within one frame's limit no longer
    limits.cut_frames=1;
    EXPECT_EQ("1:S=490 3:F=20 4:F!=40 6:O=200/5 12:Ne=1 14:G=60/13 ",list(FindTimingIssues(lines,nullptr,limits,kf,0,fps)));
    // Gaps not looked for; and one too close for a wider limit
    limits.min_gap=0;
    EXPECT_EQ("1:S=490 3:F=20 4:F!=40 6:O=200/5 12:Ne=1 ",list(FindTimingIssues(lines,nullptr,limits,kf,0,fps)));
    limits.min_gap=101;
    EXPECT_EQ("1:S=490 3:F=20 4:F!=40 6:O=200/5 12:Ne=1 14:G=60/13 15:G=100/14 ",list(FindTimingIssues(lines,nullptr,limits,kf,0,fps)));
}

TEST_F(ordinary_edit, timing_page_lists_timing_problems_and_goes_to_them) {
    window->Show();
    BOOST_SCOPE_EXIT_ALL(&){ window->Hide(); };
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    agi::fs::path kf((std::filesystem::current_path()/"timing-check-keyframes.txt").wstring());
    agi::keyframe::Save(kf,{0,50,150});
    c->project->LoadKeyframes(kf);
    auto min_duration=OPT_GET("Timing/Check/Min Duration")->GetInt();
    auto min_gap=OPT_GET("Timing/Check/Min Gap")->GetInt();
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseKeyframes(); c->project->CloseVideo(); OPT_SET("App/Style Panel/Page")->SetInt(0);
        OPT_SET("Timing/Check/Min Duration")->SetInt(min_duration); OPT_SET("Timing/Check/Min Gap")->SetInt(min_gap);
        OPT_SET("Timing/Check/Selection Only")->SetBool(false); };
    auto vc=c->videoController.get();
    std::vector<int> ids;
    {auto write=c->subtitleAccess->TryWrite();auto &file=LiveWriteForTest(*c);
     auto &row=file.Events.front();row.Text="Hello, world";row.Start=1600;row.End=5600;
     auto add=[&](int start,int end,std::string text,std::string style){auto d=new AssDialogue(row);d->Start=start;d->End=end;d->Text=text;d->Style=style;file.Events.push_back(*d);};
     add(6000,6300,"Hi","Default");                 // too short
     add(6200,8000,"Next","Default");               // overlaps the one before
     int at=vc->TimeAtFrame(152,agi::vfr::START);
     add(at,at+2000,"Sign","Sign");                 // two frames after the cut at 150
     add(8050,9000,"Then","Default");               // 50 ms after the one before: flickers
     file.Commit("fixture",AssFile::COMMIT_NEW);
     for(auto const& d:file.Events) ids.push_back(d.Id);}
    c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0]);
    auto panel=new StylePanel(window,c.get());
    BOOST_SCOPE_EXIT_ALL(&){ panel->Destroy(); };
    panel->ShowPage(StylePanel::Page::Timing);
    auto page=panel->Timing();
    auto shown=[&]{page->UpdateNow();return page->Shown();};

    EXPECT_EQ(fmt_tl("%d too short",1)+wxS(" · ")+fmt_tl("%d overlapping",1)+wxS(" · ")+fmt_tl("%d too close",1)+wxS(" · ")+fmt_tl("%d off a cut",1),shown()["check.summary"]);
    EXPECT_EQ(wxS("#2 ")+fmt_tl("Too short: %s s",wxS("0.30"))+wxS("\n#3 ")+fmt_tl("Overlaps #%d by %d ms",2,100)
        +wxS("\n#4 ")+fmt_plural(2,"Start %d frame late","Start %d frames late",2)+wxS("\n#5 ")+fmt_tl("#%d ends only %d ms before",3,50),shown()["check.list"]);
    EXPECT_EQ(wxS(" disabled"),shown()["check.prev"].Right(9)) << "nothing before the first line";

    // From the current line, one problem on and back; picking one
    page->Press("check.next");
    EXPECT_EQ(ids[1],c->selectionController->ReadActiveLine()->Id);
    EXPECT_EQ(vc->FrameAtTime(6000,agi::vfr::START),vc->GetFrameN()) << "the video goes there";
    shown(); page->Press("check.next");
    EXPECT_EQ(ids[2],c->selectionController->ReadActiveLine()->Id);
    shown(); page->Press("check.prev");
    EXPECT_EQ(ids[1],c->selectionController->ReadActiveLine()->Id);
    page->PickIssue(2);
    EXPECT_EQ(ids[3],c->selectionController->ReadActiveLine()->Id);
    page->PickIssue(3);
    EXPECT_EQ(ids[4],c->selectionController->ReadActiveLine()->Id);
    EXPECT_EQ(vc->FrameAtTime(8050,agi::vfr::START),vc->GetFrameN());
    EXPECT_TRUE(shown()["check.list"].EndsWith(" (current)"));
    EXPECT_EQ(wxS(" disabled"),shown()["check.next"].Right(9)) << "nothing after the last";

    // Only the selected lines
    c->selectionController->SetSelectionAndActiveIDs({ids[0]},ids[0]);
    shown(); page->Choose("check.scope",1);
    EXPECT_TRUE(OPT_GET("Timing/Check/Selection Only")->GetBool());
    EXPECT_EQ(_("No timing problems found"),shown()["check.summary"]);
    page->Choose("check.scope",0);

    // A shorter shortest; then a problem mended on the page
    shown(); page->EnterNumber("check.min_duration",200,true);
    EXPECT_EQ(200,OPT_GET("Timing/Check/Min Duration")->GetInt());
    EXPECT_EQ(fmt_tl("%d overlapping",1)+wxS(" · ")+fmt_tl("%d too close",1)+wxS(" · ")+fmt_tl("%d off a cut",1),shown()["check.summary"]);
    // Gaps not looked for
    page->EnterNumber("check.min_gap",0,true);
    EXPECT_EQ(0,OPT_GET("Timing/Check/Min Gap")->GetInt());
    EXPECT_EQ(fmt_tl("%d overlapping",1)+wxS(" · ")+fmt_tl("%d off a cut",1),shown()["check.summary"]);
    c->selectionController->SetSelectionAndActiveIDs({ids[2]},ids[2]);
    shown(); page->EnterNumber("time.start",6300,true);
    EXPECT_EQ(6300,int(std::next(LiveRead(*c).Events.begin(),2)->Start));
    EXPECT_EQ(fmt_tl("%d off a cut",1),shown()["check.summary"]);
}

TEST_F(ordinary_edit, compare_tab_is_one_at_a_time_and_refuses_drops_while_open) {
    Input("current");
    auto path = WriteBaseline(*c, "compare-one.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    auto other = WriteBaseline(*c, "compare-two.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    CompareHook hook(nullptr);
    ASSERT_TRUE(OpenCompareTab(c.get(), path));
    auto page = tabs->Comparison();
    tabs->ShowEditor();
    // A second comparison brings the open one to the front instead.
    EXPECT_FALSE(OpenCompareTab(c.get(), other));
    EXPECT_EQ(page, tabs->Comparison()); EXPECT_TRUE(tabs->ComparisonInFront());
    ASSERT_EQ(1u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[0].Find("already open")) << hook.messages[0];
    tabs->ShowEditor();
    cmd::call("subtitle/compare", c.get());               // no file dialog: the open tab
    EXPECT_TRUE(tabs->ComparisonInFront()); EXPECT_EQ(1u, hook.messages.size());
    // Dropped files cannot replace the subtitles while they are compared.
    tabs->ShowEditor();
    c->project->LoadList({other}, true);
    EXPECT_TRUE(tabs->ComparisonInFront());
    EXPECT_EQ(0, hook.drop_prompts);
    ASSERT_EQ(2u, hook.messages.size());
    EXPECT_NE(wxNOT_FOUND, hook.messages[1].Find("being compared")) << hook.messages[1];
    // Closing the window without a veto ends it without asking.
    Press(page, "compare-reject-current");
    EXPECT_TRUE(tabs->CloseComparison(false));
    EXPECT_EQ(2u, hook.messages.size());
    EXPECT_TRUE(c->subtitleAccess->TryWrite());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
}

TEST_F(ordinary_edit, dark_appearance_uses_dark_defaults_only_for_unchanged_colours) {
    auto restore = OPT_GET("App/Theme")->GetInt();
    theme::Init();
    int notified = 0;
    agi::signal::Connection listener = theme::AddChangeListener([&] { ++notified; });
    std::string const background = "Colour/Subtitle Grid/Background/Background";
    ASSERT_TRUE(theme::HasDarkDefault(background));
    EXPECT_FALSE(theme::HasDarkDefault("Colour/Audio Display/Play Cursor")); // drawn on the dark waveform

    OPT_SET("App/Theme")->SetInt(int(theme::Mode::Light));
    EXPECT_FALSE(theme::IsDark());
    EXPECT_EQ(wxColour(255, 255, 255), theme::Colour(background));
    OPT_SET("App/Theme")->SetInt(int(theme::Mode::Dark));
    EXPECT_TRUE(theme::IsDark());
    EXPECT_EQ(wxColour(0x1e, 0x1f, 0x22), theme::Colour(background));
    EXPECT_EQ(2, notified);                                  // each change of the setting
    // A colour the user chose is kept in either appearance.
    OPT_SET(background)->SetColor(agi::Color(10, 20, 30));
    EXPECT_EQ(wxColour(10, 20, 30), theme::Colour(background));
    OPT_SET(background)->Reset();
    EXPECT_EQ(wxColour(0x1e, 0x1f, 0x22), theme::Colour(background));
    // Options without a dark default read as they are.
    EXPECT_EQ(to_wx(OPT_GET("Colour/Audio Display/Play Cursor")->GetColor()), theme::Colour("Colour/Audio Display/Play Cursor"));
    // The tab strip and banner follow the change.
    auto path = WriteBaseline(*c, "theme-tab.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    CompareHook hook(nullptr);
    ASSERT_TRUE(OpenCompareTab(c.get(), path));
    tabs->ShowEditor();
    EXPECT_EQ(theme::UiColours().notice_back, Named(tabs, "workspace-banner")->GetBackgroundColour());
    OPT_SET("App/Theme")->SetInt(int(theme::Mode::Light));
    EXPECT_EQ(theme::UiColours().notice_back, Named(tabs, "workspace-banner")->GetBackgroundColour());
    EXPECT_EQ(wxColour(0xff, 0xf4, 0xce), Named(tabs, "workspace-banner")->GetBackgroundColour());
    tabs->CloseComparison(false);
    OPT_SET("App/Theme")->SetInt(restore);
}

TEST_F(ordinary_edit, drawn_suggestions_offer_values_starting_with_the_typed_text) {
    auto frame=new wxPanel(window);
    auto field=new wxTextCtrl(frame,-1,"");
    std::vector<wxString> values{wxString::FromUTF8("待"),wxString::FromUTF8("待校"),wxString::FromUTF8("待定"),"Alice","alex"};
    auto suggest=new FlatSuggest(field,frame,[&]{ return values; });
    auto key=[&](int code){ wxKeyEvent e(wxEVT_CHAR_HOOK); e.m_keyCode=code; return suggest->HandleKey(e); };
    field->ChangeValue(wxString::FromUTF8("待")); suggest->Suggest();
    // Longer values only: the one already typed is not offered
    EXPECT_EQ((std::vector<wxString>{wxString::FromUTF8("待校"),wxString::FromUTF8("待定")}),suggest->Shown());
    EXPECT_EQ(0,suggest->Selected());
    EXPECT_TRUE(key(WXK_DOWN)); EXPECT_EQ(1,suggest->Selected());
    EXPECT_TRUE(key(WXK_DOWN)); EXPECT_EQ(0,suggest->Selected());   // round to the first
    EXPECT_TRUE(key(WXK_UP)); EXPECT_EQ(1,suggest->Selected());
    EXPECT_TRUE(key(WXK_RETURN));
    EXPECT_EQ(wxString::FromUTF8("待定"),field->GetValue());
    EXPECT_TRUE(suggest->Shown().empty());
    EXPECT_FALSE(key(WXK_RETURN)) << "closed: Enter goes on to the field";
    // Any case; Escape closes
    field->ChangeValue("al"); suggest->Suggest();
    EXPECT_EQ((std::vector<wxString>{"Alice","alex"}),suggest->Shown());
    EXPECT_TRUE(key(WXK_ESCAPE)); EXPECT_TRUE(suggest->Shown().empty());
    field->ChangeValue("zz"); suggest->Suggest(); EXPECT_TRUE(suggest->Shown().empty());
    frame->Destroy();
}

TEST_F(ordinary_edit, grid_colours_saved_at_a_former_default_follow_the_appearance_again) {
    std::string const header = "Colour/Subtitle Grid/Header";
    OPT_SET(header)->SetColor(agi::Color(165, 207, 231));        // the original light default, as saved in a config file
    OPT_SET("Colour/Subtitle Grid/Lines")->SetColor(agi::Color(1, 2, 3)); // the user's own
    theme::Init();
    EXPECT_TRUE(OPT_GET(header)->IsDefault());
    EXPECT_EQ(agi::Color(1, 2, 3), OPT_GET("Colour/Subtitle Grid/Lines")->GetColor());
    OPT_SET("Colour/Subtitle Grid/Lines")->Reset();
}

TEST_F(ordinary_edit, dropped_subtitles_compare_with_a_saved_document_without_replacing_it) {
    Input("current");
    auto original = WriteBaseline(*c, "drop-current.ass", [](AssFile&) {});
    c->subsController->Save(original, "utf-8");
    ASSERT_TRUE(c->subsController->HasFile()); ASSERT_FALSE(c->subsController->IsModified());
    auto path = WriteBaseline(*c, "drop-baseline.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    auto revision = c->subtitleEditGate.Revision(); auto undo = c->subsController->GetUndoDescription();
    CompareHook hook(nullptr); hook.drop_choice = wxID_YES;
    c->project->LoadList({path}, true);
    hook.Settle(*c);
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(1, hook.shown); EXPECT_EQ(0, hook.save_prompts);
    EXPECT_EQ(original, c->subsController->Filename()); EXPECT_FALSE(c->subsController->IsModified());
    EXPECT_EQ((std::vector<std::string>{"current"}), LiveTexts(*c));
    EXPECT_EQ(revision, c->subtitleEditGate.Revision()); EXPECT_EQ(undo, c->subsController->GetUndoDescription());
}
TEST_F(ordinary_edit, dropped_subtitles_compare_unsaved_work_and_apply_as_one_undo_step) {
    Input("unsaved"); ASSERT_FALSE(c->subsController->HasFile());
    auto path = WriteBaseline(*c, "drop-unsaved.ass", [](AssFile& f) { f.Events.front().Text = "baseline"; });
    CompareHook hook([&](wxWindow* dialog) {
        EXPECT_EQ((std::vector<std::string>{"unsaved"}), LiveTexts(*c));
        Press(dialog, "compare-reject-current");
        button(dialog->FindWindow(wxID_OK), wxID_OK);
    });
    hook.drop_choice = wxID_YES;
    c->project->LoadList({path}, true);
    hook.Settle(*c);
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(1, hook.shown); EXPECT_EQ(0, hook.save_prompts);
    EXPECT_FALSE(c->subsController->HasFile());
    EXPECT_EQ((std::vector<std::string>{"baseline"}), LiveTexts(*c));
    c->subsController->Undo(); EXPECT_EQ((std::vector<std::string>{"unsaved"}), LiveTexts(*c));
    c->subsController->Redo(); EXPECT_EQ((std::vector<std::string>{"baseline"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, cancelled_subtitle_drop_changes_neither_document_nor_other_dropped_media) {
    Input("keep"); auto revision = c->subtitleEditGate.Revision();
    auto path = WriteBaseline(*c, "drop-cancel.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    agi::fs::path timecodes((std::filesystem::current_path() / "drop-timecodes.txt").wstring());
    { std::ofstream out(timecodes); out << "# timecode format v2\n0\n40\n80\n"; }
    CompareHook hook(nullptr);
    c->project->LoadList({timecodes, path}, true);
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(0, hook.shown); EXPECT_EQ(0, hook.save_prompts);
    EXPECT_FALSE(c->project->CanCloseTimecodes()); EXPECT_FALSE(c->subsController->HasFile());
    EXPECT_EQ(revision, c->subtitleEditGate.Revision());
    EXPECT_EQ((std::vector<std::string>{"keep"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, normal_subtitle_drop_preserves_the_unsaved_changes_confirmation) {
    Input("keep");
    auto path = WriteBaseline(*c, "drop-normal.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    CompareHook hook(nullptr); hook.drop_choice = wxID_NO;
    c->project->LoadList({path}, true); // Cancel the save/discard prompt.
    EXPECT_EQ(1, hook.drop_prompts); EXPECT_EQ(1, hook.save_prompts); EXPECT_EQ(0, hook.shown);
    EXPECT_EQ((std::vector<std::string>{"keep"}), LiveTexts(*c));
    hook.save_choice = wxID_NO;
    c->project->LoadList({path}, true); // Explicitly discard and open normally.
    EXPECT_EQ(2, hook.drop_prompts); EXPECT_EQ(2, hook.save_prompts);
    EXPECT_EQ(path, c->subsController->Filename());
    EXPECT_EQ((std::vector<std::string>{"other"}), LiveTexts(*c));
}
TEST_F(ordinary_edit, empty_workspace_and_non_drop_opens_do_not_prompt_for_comparison) {
    c->project->CloseSubtitles();
    ASSERT_FALSE(c->subsController->HasFile()); ASSERT_FALSE(c->subsController->IsModified());
    auto path = WriteBaseline(*c, "drop-first.ass", [](AssFile& f) { f.Events.front().Text = "first"; });
    CompareHook hook(nullptr);
    c->project->LoadList({path}, true);
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_EQ(path, c->subsController->Filename());
    EXPECT_EQ((std::vector<std::string>{"first"}), LiveTexts(*c));
    auto other = WriteBaseline(*c, "drop-next.ass", [](AssFile& f) { f.Events.front().Text = "next"; });
    c->project->LoadList({other});
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_EQ(other, c->subsController->Filename());
}
TEST_F(ordinary_edit, timecode_only_and_busy_subtitle_drops_do_not_offer_comparison) {
    Input("keep");
    agi::fs::path timecodes((std::filesystem::current_path() / "drop-only-timecodes.txt").wstring());
    { std::ofstream out(timecodes); out << "# timecode format v2\n0\n40\n80\n"; }
    CompareHook hook(nullptr);
    c->project->LoadList({timecodes}, true);
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_TRUE(c->project->CanCloseTimecodes());
    auto path = WriteBaseline(*c, "drop-busy.ass", [](AssFile& f) { f.Events.front().Text = "other"; });
    auto comparison = c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    c->project->LoadList({path}, true);
    EXPECT_EQ(0, hook.drop_prompts); EXPECT_EQ(0, hook.shown);
    EXPECT_EQ((std::vector<std::string>{"keep"}), LiveTexts(*c));
    EXPECT_FALSE(hook.messages.empty());
}
TEST_F(audio_edit, main_window_blocks_sit_on_a_backdrop_without_borders) {
    // The main window's arrangement: the video and visual tools blocks, then
    // the audio block, the same gap between all of them.
    BOOST_SCOPE_EXIT_ALL() { OPT_SET("App/Theme")->SetInt(1); };
    OPT_SET("App/Theme")->SetInt(1);
    theme::Init();
    auto root=new wxPanel(window);root->SetSize(0,0,1400,700);
    theme::UseBackdrop(root);
    const int gap=theme::BlockGap(root);
    auto video_box=new VideoBox(root,false,c.get());
    auto audio_box=c->audioBox=new AudioBox(root,c.get());
    auto edit_box=new SubsEditBox(root,c.get());
    auto grid=new BaseGrid(root,c.get());
    auto tools=new wxBoxSizer(wxVERTICAL);tools->Add(audio_box,0,wxEXPAND);tools->Add(edit_box,1,wxEXPAND);
    auto top=new wxBoxSizer(wxHORIZONTAL);top->Add(video_box,0,wxEXPAND);top->Add(tools,1,wxEXPAND|wxLEFT,gap);
    root->SetSizer(top);
    video_box->SetToolbarGap(gap);
    VideoDisplay *display=nullptr;std::vector<wxToolBar*> bars;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto child:w->GetChildren()){
        if(auto d=dynamic_cast<VideoDisplay*>(child))display=d;
        if(auto b=dynamic_cast<wxToolBar*>(child);b&&b->HasFlag(wxTB_VERTICAL))bars.push_back(b);find(child);}};
    find(video_box);
    ASSERT_NE(nullptr,display);ASSERT_EQ(2u,bars.size());
    display->SetMinSize(wxSize(640,360));
    root->Layout();
    auto x_in_root=[&](wxWindow*w){int x=0;for(;w&&w!=root;w=w->GetParent())x+=w->GetPosition().x;return x;};
    wxWindow *video_block=display->GetParent(), *tools_block=bars[0]->GetParent();
    ASSERT_NE(video_block,tools_block);
    EXPECT_EQ(video_box,video_block->GetParent());EXPECT_EQ(video_box,tools_block->GetParent());
    int video_right=x_in_root(video_block)+video_block->GetSize().x;
    int tools_left=x_in_root(tools_block),tools_right=tools_left+tools_block->GetSize().x;
    EXPECT_EQ(gap,tools_left-video_right) << "between the video and the tools";
    EXPECT_EQ(gap,x_in_root(audio_box)-tools_right) << "between the tools and the audio";
    EXPECT_EQ(video_box->GetSize().y,tools_block->GetSize().y) << "the tools block runs the full height";

    for (int mode : {1, 2}) {
        OPT_SET("App/Theme")->SetInt(mode);
        // Blocks in the toolkit's panel colour, darker between them.
        for (wxWindow *block : {video_block, tools_block, static_cast<wxWindow*>(edit_box)})
            EXPECT_EQ(theme::Surface(), block->GetBackgroundColour()) << "mode " << mode;
        for (wxWindow *backdrop : {static_cast<wxWindow*>(root), static_cast<wxWindow*>(video_box), static_cast<wxWindow*>(audio_box)})
            EXPECT_EQ(theme::Backdrop(), backdrop->GetBackgroundColour()) << "mode " << mode;
        auto lightness=[](wxColour c){return c.Red()+c.Green()+c.Blue();};
        EXPECT_LT(lightness(theme::Backdrop()),lightness(theme::Surface()));
    }

    // No border on any block, nor on what fills them.
    AudioDisplay *waveform=nullptr;
    std::function<void(wxWindow*)> find_audio=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(auto d=dynamic_cast<AudioDisplay*>(ch))waveform=d;find_audio(ch);}};
    find_audio(audio_box);ASSERT_NE(nullptr,waveform);
    for (wxWindow *w : {static_cast<wxWindow*>(edit_box), static_cast<wxWindow*>(grid), static_cast<wxWindow*>(waveform), waveform->GetParent(), static_cast<wxWindow*>(c->karaoke)})
        EXPECT_EQ(wxBORDER_NONE, w->GetBorder()) << w->GetClassInfo()->GetClassName();
    EXPECT_FALSE(audio_box->HasFlag(wxSW_3D)) << "no raised resize strip";
    EXPECT_EQ(gap,audio_box->GetDefaultBorderSize()) << "the gap below the audio is its resize strip";
    int stc=0;
    std::function<void(wxWindow*)> editors=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(dynamic_cast<SubsTextEditCtrl*>(ch)){++stc;EXPECT_EQ(wxBORDER_NONE,ch->GetBorder());}editors(ch);}};
    editors(edit_box);EXPECT_EQ(1,stc);
}

TEST_F(audio_edit, scrollbar_thumb_edges_zoom_and_alt_wheel_zooms) {
    window->SetSize(1100,400);window->Show(); // the scrollbar captures the mouse while dragging
    auto box=c->audioBox=new AudioBox(window,c.get());
    box->SetSize(0,0,1000,260);box->Layout();wxYield();
    int sliders=0;
    std::function<void(wxWindow*)> count=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(dynamic_cast<wxSlider*>(ch))++sliders;count(ch);}};
    count(box);
    EXPECT_EQ(0,sliders) << "zoom and volume are drawn tracks; horizontal zoom lives on the scrollbar";
    AudioDisplay* display=nullptr;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(auto d=dynamic_cast<AudioDisplay*>(ch))display=d;find(ch);}};
    find(box);ASSERT_NE(nullptr,display);
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    display->SetZoomLevel(-50); // a wide thumb
    const int width=display->GetClientSize().x, bar_y=display->GetClientSize().y-5;
    const double duration=display->GetDuration();
    ASSERT_GT(width,500);ASSERT_GT(duration,display->MaxVisibleDuration()*2);
    auto mouse=[&](wxEventType type,int x,bool down) {
        wxMouseEvent event(type);event.SetLeftDown(down);event.SetPosition({x,bar_y});event.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(event);
    };
    auto visible=[&]{double ms=display->GetMillisecondsPerPixel();double left=display->GetScrollLeft()*ms;return std::pair{left,left+width*ms};};
    auto bar_x=[&](double t){return int(t*width/duration);};
    auto drag=[&](int from,int to){mouse(wxEVT_LEFT_DOWN,from,true);mouse(wxEVT_MOTION,(from+to)/2,true);mouse(wxEVT_MOTION,to,true);mouse(wxEVT_LEFT_UP,to,false);};
    const double bar_ms=duration/width; // time one pixel of the bar stands for

    // Scroll to the middle by dragging the thumb's middle.
    auto [l0,r0]=visible();
    ASSERT_GT(bar_x(r0)-bar_x(l0),60);
    drag((bar_x(l0)+bar_x(r0))/2,width/2);
    auto [l1,r1]=visible();
    EXPECT_GT(l1,l0);EXPECT_NEAR(r0-l0,r1-l1,1) << "moving does not zoom";
    EXPECT_NEAR(width/2,(bar_x(l1)+bar_x(r1))/2,2);

    // Right edge inwards: the left edge stays, less time is shown.
    drag(bar_x(r1),bar_x(r1)-40);
    auto [l2,r2]=visible();
    EXPECT_NEAR(l1,l2,display->GetMillisecondsPerPixel()+1);
    EXPECT_NEAR(r2,(bar_x(r1)-40)*bar_ms,bar_ms+display->GetMillisecondsPerPixel());
    EXPECT_GT(display->GetZoomLevel(),-50);
    EXPECT_EQ(AudioDisplay::NearestZoomLevel(display->GetMillisecondsPerPixel()),display->GetZoomLevel());
    EXPECT_EQ(display->GetZoomLevel(),OPT_GET("Audio/Zoom/Horizontal")->GetInt()) << "the zoom is kept";

    // Left edge outwards: the right edge stays, more time is shown.
    drag(bar_x(l2),bar_x(l2)-20);
    auto [l3,r3]=visible();
    EXPECT_NEAR(r2,r3,2*display->GetMillisecondsPerPixel()+1);
    EXPECT_GT(r3-l3,r2-l2);
    EXPECT_LT(l3,l2);

    // The span stops at the deepest zoom instead of crossing over...
    drag(bar_x(l3),bar_x(r3)+30);
    auto [l4,r4]=visible();
    EXPECT_NEAR(display->MinVisibleDuration(),r4-l4,display->GetMillisecondsPerPixel()+1);
    EXPECT_NEAR(r3,r4,2*display->GetMillisecondsPerPixel()+1);
    EXPECT_EQ(30,display->GetZoomLevel());
    // ...and at the widest.
    display->SetZoomLevel(-30);
    auto [l5,r5]=visible();
    drag(bar_x(r5),width-1);
    auto [l6,r6]=visible();
    EXPECT_NEAR(display->MaxVisibleDuration(),r6-l6,display->GetMillisecondsPerPixel()+1);
    EXPECT_NEAR(l5,l6,display->GetMillisecondsPerPixel()+1);

    // Alt + wheel zooms whatever the wheel's default is.
    for(bool default_zoom:{false,true}){
        OPT_SET("Audio/Wheel Default to Zoom")->SetBool(default_zoom);
        display->SetZoomLevel(0);
        wxMouseEvent wheel(wxEVT_MOUSEWHEEL);wheel.SetAltDown(true);wheel.m_wheelRotation=120;wheel.m_wheelDelta=120;
        wheel.SetPosition({width/2,80});wheel.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(wheel);
        EXPECT_EQ(1,display->GetZoomLevel()) << "wheel defaults to zoom: " << default_zoom;
        EXPECT_EQ(1,OPT_GET("Audio/Zoom/Horizontal")->GetInt());
    }
}
TEST_F(audio_edit, alt_wheel_zoom_does_not_leave_alt_to_the_menu_bar) {
    // After Alt + wheel, releasing Alt must not select the menu bar (Windows),
    // which would keep later turns counting as Alt + wheel; Alt alone later
    // still does.
    ResetAltUsedForWheel();
    EXPECT_FALSE(TakeAltUsedForWheel());
    NoteAltUsedForWheel();
    EXPECT_TRUE(TakeAltUsedForWheel()) << "the release right after zooming";
    EXPECT_FALSE(TakeAltUsedForWheel()) << "only once";
    NoteAltUsedForWheel();ResetAltUsedForWheel();
    EXPECT_FALSE(TakeAltUsedForWheel()) << "a turn without Alt forgets it";

    // The wheel itself: Alt zooms, a plain turn right after scrolls.
    auto box=c->audioBox=new AudioBox(window,c.get());
    box->SetSize(0,0,1000,260);box->Layout();
    AudioDisplay* display=nullptr;
    std::function<void(wxWindow*)> find=[&](wxWindow*w){for(auto ch:w->GetChildren()){if(auto d=dynamic_cast<AudioDisplay*>(ch))display=d;find(ch);}};
    find(box);ASSERT_NE(nullptr,display);
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    OPT_SET("Audio/Wheel Default to Zoom")->SetBool(false);
    display->SetZoomLevel(0);display->ScrollPixelToLeft(2000);
    auto wheel=[&](bool alt){
        wxMouseEvent event(wxEVT_MOUSEWHEEL);event.SetAltDown(alt);event.m_wheelRotation=-120;event.m_wheelDelta=120;
        event.SetPosition({400,80});event.SetEventObject(display);display->GetEventHandler()->ProcessEvent(event);
    };
    wheel(true);
    EXPECT_EQ(-1,display->GetZoomLevel());
    int scroll=display->GetScrollLeft();
    wheel(false);
    EXPECT_EQ(-1,display->GetZoomLevel()) << "no more zooming once Alt is up";
    EXPECT_NE(scroll,display->GetScrollLeft()) << "it scrolls";
    EXPECT_FALSE(TakeAltUsedForWheel());
}

TEST_F(audio_edit, level_tracks_set_zoom_and_volume_with_link_and_reset) {
    window->SetSize(1100,400);window->Show(); // the tracks capture the mouse while dragging
    auto box=c->audioBox=new AudioBox(window,c.get());
    box->SetSize(0,0,1000,300);box->Layout();wxYield();
    c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
    OPT_SET("Audio/Link")->SetBool(false);
    box->ResetLevels();
    AudioLevels *levels=box->Levels();
    ASSERT_NE(nullptr,levels);
    ASSERT_GT(levels->GetClientSize().y,120);
    auto mouse=[&](wxEventType type,wxPoint p,bool down,bool shift=false){
        wxMouseEvent e(type);e.SetLeftDown(down);e.SetShiftDown(shift);e.SetPosition(p);e.SetEventObject(levels);
        levels->GetEventHandler()->ProcessEvent(e);
    };
    using Part=AudioLevels::Part;
    auto zoom_handle=[&]{return levels->HandleAt(Part::Zoom,levels->GetZoom());};
    auto volume_handle=[&]{return levels->HandleAt(Part::Volume,levels->GetVolume());};
    EXPECT_EQ(Part::Zoom,levels->HitTest(zoom_handle()));
    EXPECT_EQ(Part::Volume,levels->HitTest(volume_handle()));
    EXPECT_LT(zoom_handle().x,volume_handle().x) << "zoom on the left, volume on the right";

    // Drag the zoom handle up: zoom grows, the option and the waveform follow.
    wxPoint start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);
    mouse(wxEVT_MOTION,start-wxPoint(0,30),true);
    mouse(wxEVT_LEFT_UP,start-wxPoint(0,30),false);
    int zoom=levels->GetZoom();
    EXPECT_GT(zoom,AudioLevels::neutral_position);
    EXPECT_EQ(zoom,OPT_GET("Audio/Zoom/Vertical")->GetInt());
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetVolume()) << "not linked: the volume stays";

    // Shift drags a quarter as fast.
    start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);
    mouse(wxEVT_MOTION,start+wxPoint(0,40),true,true);
    mouse(wxEVT_LEFT_UP,start+wxPoint(0,40),false,true);
    int fine=zoom-levels->GetZoom();
    start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);
    mouse(wxEVT_MOTION,start+wxPoint(0,40),true);
    mouse(wxEVT_LEFT_UP,start+wxPoint(0,40),false);
    int coarse=zoom-fine-levels->GetZoom();
    EXPECT_GT(fine,0);EXPECT_NEAR(coarse,fine*4,4);

    // Volume: a click on its track jumps there; double-click back to neutral.
    wxRect column(levels->HandleAt(Part::Volume,AudioLevels::max_position),levels->HandleAt(Part::Volume,AudioLevels::min_position));
    mouse(wxEVT_LEFT_DOWN,wxPoint(column.x,column.y+5),true);
    mouse(wxEVT_LEFT_UP,wxPoint(column.x,column.y+5),false);
    EXPECT_GT(levels->GetVolume(),90);
    EXPECT_EQ(levels->GetVolume(),OPT_GET("Audio/Volume")->GetInt());
    mouse(wxEVT_LEFT_DCLICK,volume_handle(),false);
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetVolume());

    // Wheel: one step per notch.
    int before=levels->GetVolume();
    wxMouseEvent wheel(wxEVT_MOUSEWHEEL);wheel.m_wheelRotation=240;wheel.m_wheelDelta=120;wheel.SetPosition(volume_handle());wheel.SetEventObject(levels);
    levels->GetEventHandler()->ProcessEvent(wheel);
    EXPECT_EQ(before+2,levels->GetVolume());

    // The link button, left in the bottom row: one track sets both.
    wxSize size=levels->GetClientSize();
    wxPoint link(size.x/4,size.y-5), reset(size.x*3/4,size.y-5);
    EXPECT_EQ(Part::Link,levels->HitTest(link));
    EXPECT_EQ(Part::Reset,levels->HitTest(reset));
    EXPECT_LT(link.x,reset.x) << "reset to the right of link";
    mouse(wxEVT_LEFT_DOWN,link,true);mouse(wxEVT_LEFT_UP,link,false);
    EXPECT_TRUE(OPT_GET("Audio/Link")->GetBool());
    EXPECT_TRUE(levels->IsLinked());
    EXPECT_EQ(levels->GetZoom(),levels->GetVolume()) << "linking takes the zoom's value";
    EXPECT_EQ(Part::Zoom,levels->HitTest(wxPoint(size.x*3/4,zoom_handle().y))) << "one track across";
    start=zoom_handle();
    mouse(wxEVT_LEFT_DOWN,start,true);mouse(wxEVT_MOTION,start-wxPoint(0,10),true);mouse(wxEVT_LEFT_UP,start-wxPoint(0,10),false);
    EXPECT_EQ(levels->GetZoom(),levels->GetVolume());
    EXPECT_EQ(levels->GetZoom(),OPT_GET("Audio/Volume")->GetInt());

    // Reset, right of link: both neutral, still linked.
    mouse(wxEVT_LEFT_DOWN,reset,true);mouse(wxEVT_LEFT_UP,reset,false);
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetZoom());
    EXPECT_EQ(AudioLevels::neutral_position,levels->GetVolume());
    EXPECT_EQ(AudioLevels::neutral_position,OPT_GET("Audio/Zoom/Vertical")->GetInt());
    EXPECT_EQ(AudioLevels::neutral_position,OPT_GET("Audio/Volume")->GetInt());
    EXPECT_TRUE(OPT_GET("Audio/Link")->GetBool());
    ASSERT_NE(nullptr,cmd::get("audio/opt/reset_levels")) << "also in the command palette";
    OPT_SET("Audio/Link")->SetBool(false);
    EXPECT_FALSE(levels->IsLinked());

    // Texts below the tracks.
    EXPECT_EQ(wxString::FromUTF8("1.0\u00d7"),AudioLevels::ZoomText(50));
    EXPECT_EQ(wxString::FromUTF8("8.0\u00d7"),AudioLevels::ZoomText(100));
    EXPECT_EQ("0dB",AudioLevels::VolumeText(50));
    EXPECT_EQ("+18dB",AudioLevels::VolumeText(100));
    EXPECT_EQ("-18dB",AudioLevels::VolumeText(25));
}
TEST_F(ordinary_edit, text_import_names_its_fields_and_saves_them) {
    OPT_SET("Tool/Import/Text/Actor Separator")->SetString(":");
    OPT_SET("Tool/Import/Text/Comment Starter")->SetString("#");
    OPT_SET("Tool/Import/Text/Include Blank")->SetBool(false);
    bool ran=false, accepted=false;
    {
        RunModal hook(_("Text import options"),[&](wxDialog *dialog){
            auto separator=fields_named(dialog,_("Actor separator")), comment=fields_named(dialog,_("Comment starter"));
            ASSERT_EQ(1u,separator.size()); ASSERT_EQ(1u,comment.size());
            EXPECT_EQ(":",separator[0]->GetValue()); EXPECT_EQ("#",comment[0]->GetValue());
            FlatCheck *blank=nullptr; for(auto child:descendants(dialog))if(auto k=dynamic_cast<FlatCheck*>(child))blank=k;
            ASSERT_NE(nullptr,blank); EXPECT_FALSE(blank->GetValue());
            separator[0]->SetValue("|"); blank->Toggle();
            EXPECT_EQ(nullptr,dynamic_cast<wxButton*>(dialog->FindWindow(wxID_OK))) << "the foot buttons are flat";
            ran=true;
            button(dialog,wxID_OK);
        });
        accepted=ShowPlainTextImportDialog();
    }
    EXPECT_TRUE(ran); EXPECT_TRUE(accepted);
    EXPECT_EQ("|",OPT_GET("Tool/Import/Text/Actor Separator")->GetString());
    EXPECT_EQ("#",OPT_GET("Tool/Import/Text/Comment Starter")->GetString());
    EXPECT_TRUE(OPT_GET("Tool/Import/Text/Include Blank")->GetBool());
}
TEST_F(ordinary_edit, dummy_video_follows_its_size_shortcuts_and_frame_rate) {
    OPT_SET("Video/Dummy/Last/Width")->SetInt(1280);
    OPT_SET("Video/Dummy/Last/Height")->SetInt(720);
    OPT_SET("Video/Dummy/FPS String")->SetString("25");
    OPT_SET("Video/Dummy/Last/Length")->SetInt(250);
    OPT_SET("Video/Dummy/Pattern")->SetBool(false);
    bool ran=false;
    std::string made;
    {
        RunModal hook(_("Dummy video options"),[&](wxDialog *dialog){
            FlatChoice *shortcuts=nullptr; FlatCheck *pattern=nullptr; wxTextCtrl *fps=nullptr;
            for(auto child:descendants(dialog)){
                if(auto ch=dynamic_cast<FlatChoice*>(child))shortcuts=ch;
                if(auto k=dynamic_cast<FlatCheck*>(child))pattern=k;
                if(auto t=dynamic_cast<wxTextCtrl*>(child);t && t->GetValue()=="25")fps=t;
            }
            ASSERT_NE(nullptr,shortcuts); ASSERT_NE(nullptr,pattern); ASSERT_NE(nullptr,fps);
            auto width=fields_named(dialog,wxGETTEXT_IN_CONTEXT("resolution","Width")), height=fields_named(dialog,wxGETTEXT_IN_CONTEXT("resolution","Height"));
            ASSERT_EQ(1u,width.size()); ASSERT_EQ(1u,height.size());
            EXPECT_TRUE(shortcuts->GetStringSelection().StartsWith(L"1280×720"));
            width[0]->SetValue("640"); height[0]->SetValue("480");
            EXPECT_TRUE(shortcuts->GetStringSelection().StartsWith(L"640×480")) << "typing a size shows its shortcut";
            width[0]->SetValue("641");
            EXPECT_EQ(-1,shortcuts->GetSelection()) << "a size with no shortcut shows none";
            shortcuts->Choose(7);
            EXPECT_EQ("1280",width[0]->GetValue()); EXPECT_EQ("720",height[0]->GetValue());
            auto ok=dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_OK)); ASSERT_NE(nullptr,ok);
            fps->SetValue("");
            EXPECT_FALSE(ok->IsEnabled()) << "no frame rate, nothing to make";
            fps->SetValue("30");
            EXPECT_TRUE(ok->IsEnabled());
            pattern->Toggle();
            ran=true;
            button(dialog,wxID_OK);
        });
        made=CreateDummyVideo(window);
    }
    EXPECT_TRUE(ran);
    EXPECT_TRUE(made.starts_with("?dummy:30:250:1280:720:")) << made;
    EXPECT_TRUE(made.ends_with(":c")) << made;
    EXPECT_EQ(1280,OPT_GET("Video/Dummy/Last/Width")->GetInt());
    EXPECT_TRUE(OPT_GET("Video/Dummy/Pattern")->GetBool());
}
TEST_F(ordinary_edit, video_details_names_each_detail_in_grey) {
    c->project->LoadVideo("?dummy:25:250:640:480:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    bool ran=false;
    {
        RunModal hook(_("Video Details"),[&](wxDialog *dialog){
            std::vector<wxTextCtrl*> values; std::vector<wxString> names;
            for(auto child:descendants(dialog)){
                if(auto t=dynamic_cast<wxTextCtrl*>(child);t && dynamic_cast<FlatFrame*>(t->GetParent()))values.push_back(t);
                else if(auto l=dynamic_cast<wxStaticText*>(child))names.push_back(l->GetLabel());
            }
            ASSERT_EQ(9u,values.size());
            for(auto v:values)EXPECT_FALSE(v->IsEditable());
            EXPECT_EQ(L"640x480 (4:3)",values[2]->GetValue());
            EXPECT_NE(names.end(),std::find(names.begin(),names.end(),flat::WithoutColon(_("Resolution:")))) << "named without the colon";
            auto ok=dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_OK)); ASSERT_NE(nullptr,ok); EXPECT_TRUE(ok->IsAccent());
            ran=true;
            ok->Press();
        });
        ShowVideoDetailsDialog(c.get());
    }
    EXPECT_TRUE(ran);
}
TEST_F(visual_edit, video_properties_prompt_chooses_from_a_flat_list) {
    {std::ofstream video("properties.y4m",std::ios::binary);video<<"YUV4MPEG2 W32 H24 F25:1 Ip A1:1 C420jpeg\nFRAME\n";std::string pixels(32*24*3/2,'\x80');video.write(pixels.data(),pixels.size());}
    OPT_SET("Video/No YCbCr Matrix in Script")->SetInt(0);OPT_SET("Video/PlayRes Mismatch")->SetInt(0);OPT_SET("Video/No LayoutRes in Script")->SetInt(0);OPT_SET("Video/LayoutRes Mismatch")->SetInt(0);
    AcceptMessages messages;c->project->LoadVideo("properties.y4m");ASSERT_NE(nullptr,c->project->VideoProvider());
    {auto write=c->subtitleAccess->TryWrite();LiveWriteForTest(*c).SetScriptInfo("PlayResX","80");LiveWriteForTest(*c).SetScriptInfo("PlayResY","60");LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_SCRIPTINFO);}
    OPT_SET("Video/PlayRes Mismatch")->SetInt(1);
    OPT_SET("Video/Last PlayRes Mismatch Choice")->SetInt(1);
    bool ran=false;
    {RunModal choose(_("Resolution mismatch"),[&](wxDialog* dialog){
        FlatList *list=nullptr; for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child))list=l;
        ASSERT_NE(nullptr,list); EXPECT_EQ(1,list->GetSelection()) << "the last choice chosen";
        EXPECT_NE(nullptr,dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_HELP)));
        wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;esc.SetEventObject(dialog);dialog->GetEventHandler()->ProcessEvent(esc);
        ran=true;
    });UpdateVideoProperties(c.get(),c->project->VideoProvider());}
    EXPECT_TRUE(ran);
    EXPECT_EQ(80,LiveRead(*c).GetScriptInfoAsInt("PlayResX")) << "cancelled, left as it is";
    EXPECT_EQ(1,OPT_GET("Video/Last PlayRes Mismatch Choice")->GetInt()) << "cancelling chooses nothing";
    {RunModal choose(_("Resolution mismatch"),[&](wxDialog* dialog){
        for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child))l->DoubleClick(0);
    });UpdateVideoProperties(c.get(),c->project->VideoProvider());}
    EXPECT_EQ(80,LiveRead(*c).GetScriptInfoAsInt("PlayResX"));
    EXPECT_EQ(0,OPT_GET("Video/Last PlayRes Mismatch Choice")->GetInt()) << "a double click chooses";
}
/// Autosave folders holding @p names, for the autosave dialog
static void make_autosaves(std::vector<std::string> const& names) {
    static int made=0;
    auto here=std::filesystem::current_path()/("autosaves"+std::to_string(++made));
    // The numbering starts again each run: what an earlier run left goes
    std::filesystem::remove_all(here);
    std::filesystem::create_directories(here/"autosave"); std::filesystem::create_directories(here/"backup");
    for(auto const& name:names)std::ofstream(here/"autosave"/name)<<"[Script Info]\n";
    OPT_SET("Path/Auto/Save")->SetString((here/"autosave").string());
    OPT_SET("Path/Auto/Backup")->SetString((here/"backup").string());
}
TEST_F(ordinary_edit, autosave_lists_files_then_their_versions) {
    make_autosaves({"episode.2026-01-02-03-04-05.AUTOSAVE.ass","episode.2026-01-03-03-04-05.AUTOSAVE.ass","movie.2025-12-01-00-00-00.AUTOSAVE.ass"});
    bool ran=false; std::string chosen;
    {
        RunModal hook(_("Open autosave file"),[&](wxDialog *dialog){
            std::vector<FlatList*> lists; for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child))lists.push_back(l);
            ASSERT_EQ(2u,lists.size());
            auto files=lists[0], versions=lists[1];
            ASSERT_EQ(2u,files->GetCount());
            EXPECT_EQ("episode",files->GetString(0)) << "the newest first";
            EXPECT_FALSE(files->GetRow(0).note.empty()) << "with the date of its newest version";
            EXPECT_EQ(2u,versions->GetCount()); EXPECT_EQ(0,versions->GetSelection());
            files->Click(1);
            EXPECT_EQ(1u,versions->GetCount()) << "the versions of the file chosen";
            files->Click(0); versions->Click(1);
            auto open=dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_OK)); ASSERT_NE(nullptr,open);
            EXPECT_EQ(_("Open"),open->GetLabel()); EXPECT_TRUE(open->IsEnabled());
            ran=true;
            versions->DoubleClick(1);
        });
        chosen=PickAutosaveFile(window);
    }
    EXPECT_TRUE(ran);
    EXPECT_TRUE(chosen.ends_with("episode.2026-01-02-03-04-05.AUTOSAVE.ass")) << chosen;
}
TEST_F(ordinary_edit, autosave_with_nothing_to_open_says_so) {
    make_autosaves({});
    // Files other tests recovered set aside while this one looks
    auto recovered=config::path->Decode("?user/recovered"), aside=recovered.parent_path()/"recovered.aside";
    bool moved=std::filesystem::exists(recovered);
    if(moved)std::filesystem::rename(recovered,aside);
    bool ran=false;
    {
        RunModal hook(_("Open autosave file"),[&](wxDialog *dialog){
            FlatList *files=nullptr; for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child)){files=l;break;}
            ASSERT_NE(nullptr,files); EXPECT_TRUE(files->IsEmpty());
            EXPECT_FALSE(dialog->FindWindow(wxID_OK)->IsEnabled());
            ran=true;
        });
        EXPECT_EQ("",PickAutosaveFile(window));
    }
    if(moved)std::filesystem::rename(aside,recovered);
    EXPECT_TRUE(ran);
}
TEST_F(ordinary_edit, attachments_list_names_groups_and_sizes) {
    for(auto name:{"one.ttf","two.ttf"}){std::ofstream data(name,std::ios::binary);data<<"attachment "<<name;}
    { auto write=c->subtitleAccess->TryWrite();for(auto name:{"one.ttf","two.ttf"})LiveWriteForTest(*c).InsertAttachment(agi::fs::path(name));LiveWriteForTest(*c).Commit("attachment",AssFile::COMMIT_ATTACHMENT); }
    bool ran=false;
    {
        RunModal hook(_("Attachment List"),[&](wxDialog *dialog){
            FlatList *list=nullptr; for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child))list=l;
            ASSERT_NE(nullptr,list); ASSERT_EQ(2u,list->GetCount());
            EXPECT_TRUE(list->GetString(0).StartsWith("one")) << list->GetString(0);
            EXPECT_EQ("Fonts",list->GetRow(0).detail) << "the group without its brackets";
            EXPECT_EQ(PrettySize(LiveRead(*c).Attachments.front().GetSize()),list->GetRow(0).note);
            auto extract=button_labelled(dialog,_("E&xtract")), remove=button_labelled(dialog,_("&Delete"));
            ASSERT_NE(nullptr,extract); ASSERT_NE(nullptr,remove);
            EXPECT_FALSE(extract->IsEnabled()); EXPECT_FALSE(remove->IsEnabled());
            list->Click(0); list->Click(1,true);
            EXPECT_TRUE(extract->IsEnabled()); EXPECT_TRUE(remove->IsEnabled());
            remove->Press();
            EXPECT_TRUE(list->IsEmpty()) << "both chosen deleted";
            EXPECT_FALSE(remove->IsEnabled());
            auto close=dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_CANCEL)); ASSERT_NE(nullptr,close);
            EXPECT_EQ(_("&Close"),close->GetLabel());
            ran=true;
            close->Press();
            EXPECT_FALSE(dialog->IsShown());
        });
        ShowAttachmentsDialog(c.get());
    }
    EXPECT_TRUE(ran);
    EXPECT_TRUE(LiveRead(*c).Attachments.empty());
}
/// A script as the automation manager sees one, loaded with no engine
struct ListedScript final : Automation4::Script {
    std::string name, description; bool loaded; std::vector<std::string> warnings;
    ListedScript(std::string name, std::string description, bool loaded=true, std::vector<std::string> warnings={})
    : Script(agi::fs::path(name+".lua")), name(std::move(name)), description(std::move(description)), loaded(loaded), warnings(std::move(warnings)) { }
    void Reload() override { }
    std::string GetName() const override { return name; }
    std::string GetDescription() const override { return description; }
    std::string GetAuthor() const override { return ""; }
    std::string GetVersion() const override { return ""; }
    bool GetLoadedState() const override { return loaded; }
    std::vector<std::string> GetWarnings() const override { return warnings; }
    std::vector<cmd::Command*> GetMacros() const override { return {}; }
    std::vector<Automation4::ExportFilter*> GetFilters() const override { return {}; }
};
/// The global manager while it lives: a script that loads, one that loaded
/// with warnings and one that did not
struct AutomationScripts {
    AutomationScripts() {
        config::global_scripts=new Automation4::AutoloadScriptManager("?user/no-autoload-here");
        config::global_scripts->Add(std::make_unique<ListedScript>("Good","Does good\nover two lines"));
        config::global_scripts->Add(std::make_unique<ListedScript>("Careful","",true,std::vector<std::string>{"careful"}));
        config::global_scripts->Add(std::make_unique<ListedScript>("Broken","",false));
    }
    ~AutomationScripts() { wxTheApp->ProcessIdle(); delete config::global_scripts; config::global_scripts=nullptr; }
};
TEST_F(ordinary_edit, automation_manager_lists_scripts_with_their_state) {
    AcceptMessages messages;
    AutomationScripts scripts;
    c->local_scripts->Add(std::make_unique<ListedScript>("Mine",""));
    ShowAutomationDialog(c.get());
    wxDialog *dialog=nullptr; for(auto w:wxTopLevelWindows)if(auto d=dynamic_cast<wxDialog*>(w);d && d->GetTitle()==_("Automation Manager") && d->IsShown())dialog=d;
    ASSERT_NE(nullptr,dialog);
    FlatList *list=nullptr; for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child))list=l;
    ASSERT_NE(nullptr,list);
    ASSERT_EQ(4u,list->GetCount());
    EXPECT_EQ("Mine",list->GetString(0)) << "the local scripts first";
    EXPECT_EQ(wxGETTEXT_IN_CONTEXT("automation script","Local"),list->GetRow(0).note); EXPECT_FALSE(list->GetRow(0).warn);
    int good=1, careful=2, broken=3;
    EXPECT_EQ("Good",list->GetString(good)); EXPECT_FALSE(list->GetRow(good).warn);
    EXPECT_EQ(_("Loaded with warnings"),list->GetRow(careful).note); EXPECT_TRUE(list->GetRow(careful).warn);
    EXPECT_TRUE(list->GetRow(broken).warn);
    EXPECT_EQ(wxGETTEXT_IN_CONTEXT("automation script","Global"),list->GetRow(good).note);
    EXPECT_TRUE(list->GetRow(good).detail.EndsWith(L"— Does good over two lines")) << list->GetRow(good).detail;
    EXPECT_EQ(_("Failed to load"),list->GetRow(broken).note);
    auto remove=button_labelled(dialog,_("&Remove")), reload=button_labelled(dialog,_("Re&load"));
    ASSERT_NE(nullptr,remove); ASSERT_NE(nullptr,reload);
    EXPECT_FALSE(reload->IsEnabled());
    list->Click(good);
    EXPECT_TRUE(reload->IsEnabled()); EXPECT_FALSE(remove->IsEnabled()) << "a global script is not removed here";
    list->Click(0);
    EXPECT_TRUE(remove->IsEnabled());
    remove->Press();
    EXPECT_EQ(3u,list->GetCount()); EXPECT_TRUE(c->local_scripts->GetScripts().empty());
    EXPECT_EQ(_("&Close"),dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_CANCEL))->GetLabel());
    dialog->Close();
    EXPECT_FALSE(dialog->IsShown());
}
TEST_F(ordinary_edit, ebu_export_is_drawn_choices_a_switch_and_a_time_code) {
    EbuExportSettings settings("Subtitle Format/EBU STL");
    settings.tv_standard=EbuExportSettings::STL25; settings.text_encoding=EbuExportSettings::utf8;
    settings.display_standard=EbuExportSettings::DSC_Open; settings.max_line_length=42;
    settings.line_wrapping_mode=EbuExportSettings::AbortOverLength; settings.translate_alignments=false; settings.inclusive_end_times=false;
    settings.timecode_offset={0,0,0,0};
    int ran=0, result=0;
    {
        RunModal hook(_("Export to EBU STL format"),[&](wxDialog *dialog){
            std::vector<FlatChoice*> choices; FlatSwitch *display=nullptr; std::vector<FlatCheck*> checks; wxTextCtrl *offset=nullptr, *length=nullptr;
            for(auto child:descendants(dialog)){
                if(auto ch=dynamic_cast<FlatChoice*>(child))choices.push_back(ch);
                if(auto sw=dynamic_cast<FlatSwitch*>(child))display=sw;
                if(auto k=dynamic_cast<FlatCheck*>(child))checks.push_back(k);
                if(auto t=dynamic_cast<wxTextCtrl*>(child)){ if(t->GetValue()=="00:00:00:00")offset=t; else if(t->GetValue()=="42")length=t; }
            }
            ASSERT_EQ(3u,choices.size()); ASSERT_NE(nullptr,display); ASSERT_EQ(2u,checks.size()); ASSERT_NE(nullptr,offset); ASSERT_NE(nullptr,length);
            EXPECT_EQ(_("25 fps (STL25.01)"),choices[0]->GetStringSelection());
            EXPECT_EQ(_("UTF-8 Unicode (non-standard)"),choices[1]->GetStringSelection());
            EXPECT_EQ(_("Abort if any lines are too long"),choices[2]->GetStringSelection());
            EXPECT_EQ(0,display->GetSelection());
            choices[0]->Choose(5); display->Choose(1); length->SetValue("60"); checks[0]->Toggle(); checks[1]->Toggle();
            if(ran==0){
                offset->SetValue("1:00");
                AcceptMessages messages;
                button(dialog,wxID_OK);
                EXPECT_TRUE(dialog->IsShown()) << "a time code in the wrong form is not taken";
                offset->SetValue("01:00:00:00");
            }
            ++ran;
            button(dialog,wxID_OK);
        });
        result=ShowEbuExportConfigurationDialog(window,settings);
    }
    EXPECT_EQ(1,ran); EXPECT_EQ(wxID_OK,result);
    EXPECT_EQ(EbuExportSettings::STL30,settings.tv_standard);
    EXPECT_EQ(EbuExportSettings::DSC_Level1,settings.display_standard);
    EXPECT_EQ(60,settings.max_line_length);
    EXPECT_TRUE(settings.translate_alignments); EXPECT_TRUE(settings.inclusive_end_times);
    EXPECT_EQ(1,settings.timecode_offset.h); EXPECT_EQ(0,settings.timecode_offset.m);
}
TEST_F(ordinary_edit, ebu_export_keeps_level_2_teletext_unless_another_is_chosen) {
    EbuExportSettings settings("Subtitle Format/EBU STL");
    settings.display_standard=EbuExportSettings::DSC_Level2;
    {
        RunModal hook(_("Export to EBU STL format"),[&](wxDialog *dialog){ button(dialog,wxID_OK); });
        EXPECT_EQ(wxID_OK,ShowEbuExportConfigurationDialog(window,settings));
    }
    EXPECT_EQ(EbuExportSettings::DSC_Level2,settings.display_standard);
}
TEST_F(ordinary_edit, kanji_timer_is_drawn_choices_checks_and_flat_buttons) {
    {auto write=c->subtitleAccess->TryWrite();auto style=new AssStyle();style->name="Target";LiveWriteForTest(*c).Styles.push_back(*style);
        LiveWriteForTest(*c).Events.front().Text=R"({\k10}a)";
        auto row=new AssDialogue();row->Style="Target";row->Text="字";LiveWriteForTest(*c).Events.push_back(*row);
        LiveWriteForTest(*c).Commit("fixture",AssFile::COMMIT_DIAG_ADDREM|AssFile::COMMIT_STYLES);
    }
    auto text=LiveRead(*c).Events.back().Text.get();
    bool ran=false;
    {
        RunModal hook(_("Kanji timer"),[&](wxDialog *dialog){
            std::vector<FlatChoice*> styles; FlatCheck *interpolate=nullptr;
            for(auto child:descendants(dialog)){
                if(auto ch=dynamic_cast<FlatChoice*>(child))styles.push_back(ch);
                if(auto k=dynamic_cast<FlatCheck*>(child))interpolate=k;
                EXPECT_EQ(nullptr,dynamic_cast<wxButton*>(child)) << "every button is flat";
            }
            ASSERT_EQ(2u,styles.size()); ASSERT_NE(nullptr,interpolate);
            EXPECT_EQ((std::vector<wxString>{"Default","Target"}),styles[0]->GetItems());
            auto accept=button_labelled(dialog,_("&Accept Line")); ASSERT_NE(nullptr,accept); EXPECT_TRUE(accept->IsAccent());
            styles[0]->SetStringSelection("Default"); styles[1]->SetStringSelection("Target");
            button_labelled(dialog,_("S&tart!"))->Press();
            button_labelled(dialog,_("&Link"))->Press(); accept->Press();
            wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;esc.SetEventObject(dialog);dialog->GetEventHandler()->ProcessEvent(esc);
            EXPECT_FALSE(dialog->IsShown()) << "Escape leaves";
            ran=true;
        });
        ShowKanjiTimerDialog(c.get());
    }
    EXPECT_TRUE(ran);
    EXPECT_EQ(text,LiveRead(*c).Events.back().Text.get()) << "Escape leaves without applying";
}
TEST_F(ordinary_edit, selected_choices_are_ticked_in_a_flat_list) {
    wxArrayString choices; choices.Add("One"); choices.Add("Two"); choices.Add("Three");
    wxArrayInt selections; selections.Add(1);
    bool ran=false; int count=0;
    {
        RunModal hook("Pick",[&](wxDialog *dialog){
            FlatList *list=nullptr; for(auto child:descendants(dialog))if(auto l=dynamic_cast<FlatList*>(child))list=l;
            ASSERT_NE(nullptr,list); ASSERT_EQ(3u,list->GetCount());
            EXPECT_FALSE(list->IsChecked(0)); EXPECT_TRUE(list->IsChecked(1)) << "the selections given ticked";
            button_labelled(dialog,_("Select &All"))->Press();
            EXPECT_TRUE(list->IsChecked(0)); EXPECT_TRUE(list->IsChecked(2));
            button_labelled(dialog,_("Select &None"))->Press();
            EXPECT_FALSE(list->IsChecked(1));
            list->Check(2);
            ran=true;
            button(dialog,wxID_OK);
        });
        count=GetSelectedChoices(window,selections,"Choose","Pick",choices);
    }
    EXPECT_TRUE(ran); EXPECT_EQ(1,count);
    ASSERT_EQ(1u,selections.size()); EXPECT_EQ(2,selections[0]);
    {
        RunModal hook("Pick",[&](wxDialog *dialog){ button(dialog,wxID_CANCEL); });
        EXPECT_EQ(-1,GetSelectedChoices(window,selections,"Choose","Pick",choices));
    }
}
TEST_F(ordinary_edit, progress_cancels_with_a_flat_button_and_stays_for_its_log) {
    bool ran=false;
    {
        RunModal hook("Working",[&](wxDialog *dialog){
            auto cancel=dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_CANCEL)); ASSERT_NE(nullptr,cancel);
            EXPECT_EQ(_("Cancel"),cancel->GetLabel());
            wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;esc.SetEventObject(dialog);dialog->GetEventHandler()->ProcessEvent(esc);
            EXPECT_EQ(_("Cancelling..."),cancel->GetLabel()) << "Escape presses Cancel";
            EXPECT_FALSE(cancel->IsEnabled());
            // The task sees it and stops, and the dialog ends itself
            for(int i=0;i<400 && dialog->IsShown();++i){wxYield();wxMilliSleep(5);}
            EXPECT_FALSE(dialog->IsShown());
            ran=true;
        });
        DialogProgress progress(window,"Working","Something long");
        EXPECT_THROW(progress.Run([](agi::ProgressSink *ps){ while(!ps->IsCancelled())wxMilliSleep(5); }),agi::UserCancelException);
    }
    EXPECT_TRUE(ran);
    ran=false;
    {
        RunModal hook("Working",[&](wxDialog *dialog){
            auto close=dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_CANCEL)); ASSERT_NE(nullptr,close);
            for(int i=0;i<200 && close->GetLabel()!=_("Close");++i){wxYield();wxMilliSleep(5);}
            EXPECT_EQ(_("Close"),close->GetLabel()) << "left open to read what it wrote";
            ran=true;
            close->Press();
        });
        DialogProgress progress(window,"Working","Something long");
        EXPECT_NO_THROW(progress.Run([](agi::ProgressSink *ps){ ps->Log("written"); }));
    }
    EXPECT_TRUE(ran);
    ran=false;
    {
        // The bar is drawn: it glides to how far the task has got, the per
        // cent under it; not known how far, a piece sweeps and no number
        std::atomic<int> step{0};
        RunModal hook("Working",[&](wxDialog *dialog){
            ProgressBar *bar=nullptr; for(auto child:descendants(dialog))if(auto b=dynamic_cast<ProgressBar*>(child))bar=b;
            ASSERT_NE(nullptr,bar);
            for(int i=0;i<400 && bar->GetValue()!=0.5;++i){wxYield();wxMilliSleep(5);}
            EXPECT_DOUBLE_EQ(0.5,bar->GetValue());
            auto has_label=[&](wxString const& label){ for(auto child:descendants(dialog))if(auto t=dynamic_cast<wxStaticText*>(child))if(t->GetLabel()==label)return true; return false; };
            EXPECT_TRUE(has_label("50%"));
            for(int i=0;i<400 && bar->GetShown()!=0.5;++i){wxYield();wxMilliSleep(5);}
            EXPECT_DOUBLE_EQ(0.5,bar->GetShown()) << "it glides all the way there";
            step=1;
            for(int i=0;i<400 && !bar->IsIndeterminate();++i){wxYield();wxMilliSleep(5);}
            EXPECT_TRUE(bar->IsIndeterminate());
            EXPECT_FALSE(has_label("50%"));
            ran=true;
            wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;esc.SetEventObject(dialog);dialog->GetEventHandler()->ProcessEvent(esc);
            for(int i=0;i<400 && dialog->IsShown();++i){wxYield();wxMilliSleep(5);}
        });
        DialogProgress progress(window,"Working","Something long");
        EXPECT_THROW(progress.Run([&](agi::ProgressSink *ps){
            ps->SetProgress(50,100);
            while(step==0 && !ps->IsCancelled())wxMilliSleep(5);
            ps->SetIndeterminate();
            while(!ps->IsCancelled())wxMilliSleep(5);
        }),agi::UserCancelException);
    }
    EXPECT_TRUE(ran);
}
TEST_F(ordinary_edit, color_picker_is_drawn_number_fields_a_mode_switch_and_code_fields) {
    OPT_SET("Tool/Colour Picker/Mode")->SetInt(0);
    bool ran=false;
    agi::Color chosen;
    {
        RunModal hook("Select Color",[&](wxDialog *dialog){
            FlatSwitch *mode=nullptr; for(auto child:descendants(dialog))if(auto sw=dynamic_cast<FlatSwitch*>(child))mode=sw;
            ASSERT_NE(nullptr,mode); EXPECT_EQ(5u,mode->GetCount()); EXPECT_EQ(0,mode->GetSelection());
            auto red=fields_named(dialog,flat::WithoutColon(_("Red:"))), ass=fields_named(dialog,"ASS"), html=fields_named(dialog,"HTML");
            ASSERT_EQ(1u,red.size()); ASSERT_EQ(1u,ass.size()); ASSERT_EQ(1u,html.size());
            EXPECT_EQ(2u,fields_named(dialog,flat::WithoutColon(_("Hue:"))).size()) << "HSL's and HSV's";
            EXPECT_EQ(L"&H0000FF&",ass[0]->GetValue());
            red[0]->SetValue("128");
            EXPECT_EQ(L"#800000",html[0]->GetValue().Upper()) << "a number typed updates the codes";
            ass[0]->SetValue("&H00FF00&");
            EXPECT_EQ("0",red[0]->GetValue()) << "a code typed updates the numbers";
            EXPECT_EQ("255",fields_named(dialog,flat::WithoutColon(_("Green:")))[0]->GetValue());
            mode->Choose(3);
            EXPECT_EQ(3,OPT_GET("Tool/Colour Picker/Mode")->GetInt()) << "the mode chosen kept";
            EXPECT_NE(nullptr,dynamic_cast<FlatButton*>(dialog->FindWindow(wxID_HELP)));
            ran=true;
            button(dialog,wxID_OK);
        });
        GetColorFromUser(window,agi::Color(255,0,0),false,[&](agi::Color colour){ chosen=colour; });
    }
    EXPECT_TRUE(ran);
    EXPECT_EQ(agi::Color(0,255,0),chosen);
}

TEST(style_preview, backdrop_keeps_the_text_visible) {
    agi::Color const blue_grey(125,153,176); // the default preview colour
    AssStyle s;
    // White text with a black outline shows on anything: the chosen colour
    s.primary=agi::Color(255,255,255);s.outline=agi::Color(0,0,0);s.outline_w=2;
    EXPECT_EQ(blue_grey,PreviewBackdrop(s,blue_grey));
    // Black text, black outline: a light background instead
    s.primary=agi::Color(0,0,0);
    auto light=PreviewBackdrop(s,blue_grey);
    EXPECT_NE(blue_grey,light);
    EXPECT_GT(light.r+light.g+light.b,600);
    // ... but a light colour chosen is kept
    EXPECT_EQ(agi::Color(250,250,250),PreviewBackdrop(s,agi::Color(250,250,250)));
    // Black text without an outline, the same
    s.outline_w=0;
    EXPECT_EQ(light,PreviewBackdrop(s,blue_grey));
    // A transparent fill: the outline is the text's colour
    s.primary=agi::Color(0,0,0,255);s.outline=agi::Color(255,255,255);s.outline_w=2;
    EXPECT_EQ(agi::Color(0,0,0),PreviewBackdrop(s,agi::Color(0,0,0))) << "a white outline shows on black";
    s.outline=agi::Color(0,0,0);
    EXPECT_NE(agi::Color(0,0,0),PreviewBackdrop(s,agi::Color(0,0,0))) << "a black one does not";
    // White text alone on white: a dark background
    s.primary=agi::Color(255,255,255);s.outline_w=0;
    auto dark=PreviewBackdrop(s,agi::Color(255,255,255));
    EXPECT_LT(dark.r+dark.g+dark.b,200);
}

namespace {
/// Frames drawn from a function of the frame number, 25 fps
class DrawnVideo final : public VideoProvider {
public:
    int frames, w, h;
    std::function<void(int n, int x, int y, unsigned char *bgr)> draw;
    int reads=0;
    DrawnVideo(int frames,int w,int h):frames(frames),w(w),h(h){}
    void GetFrame(int n, VideoFrame &frame) override {
        ++reads;
        frame.width=w;frame.height=h;frame.pitch=w*4;frame.flipped=false;
        frame.data.assign(size_t(w)*h*4,255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)draw(n,x,y,&frame.data[(size_t(y)*w+x)*4]);
    }
    void SetColorSpace(agi::ycbcr::Header) override {}
    int GetFrameCount() const override { return frames; }
    int GetWidth() const override { return w; }
    int GetHeight() const override { return h; }
    double GetDAR() const override { return double(w)/h; }
    agi::vfr::Framerate GetFPS() const override { return agi::vfr::Framerate(25,1); }
    std::vector<int> GetKeyFrames() const override { return {}; }
    agi::ycbcr::header_colorspace GetColorSpace() const override { return agi::ycbcr::header_colorspace::unspecified(); }
    std::string GetDecoderName() const override { return "drawn"; }
};
/// A shot: a textured picture in its own colours, panning 2 px a frame
void Shot(int shot, int offset, int x, int y, unsigned char *bgr) {
    static const int base[][3]={{40,40,200},{60,160,30},{200,60,40},{90,220,230},{25,25,25}};
    double texture=60*std::sin((x+2*offset)/15.0)*std::cos(y/12.0);
    for(int c=0;c<3;++c)bgr[c]=static_cast<unsigned char>(std::clamp(base[shot][c]+texture,0.,255.));
}
struct CancelledSink final : agi::ProgressSink {
    void SetIndeterminate() override {}
    void SetTitle(std::string const&) override {}
    void SetMessage(std::string const&) override {}
    void SetProgress(int64_t,int64_t) override {}
    void Log(std::string const&) override {}
    bool IsCancelled() override { return true; }
};
}

TEST(scene_detect, finds_cuts_and_fades_but_not_motion_or_flashes) {
    DrawnVideo video(180,320,180);
    video.draw=[](int n,int x,int y,unsigned char *bgr){
        if(n<40)return Shot(0,n,x,y,bgr);              // A, panning
        if(n==60){bgr[0]=bgr[1]=bgr[2]=255;return;}    // a one-frame flash in B
        if(n<80)return Shot(1,n,x,y,bgr);              // cut to B
        if(n<100){                                     // B fades into C over 20 frames
            unsigned char b[3],c[3];Shot(1,n,x,y,b);Shot(2,n,x,y,c);
            double t=(n-79)/21.;
            for(int i=0;i<3;++i)bgr[i]=static_cast<unsigned char>(b[i]*(1-t)+c[i]*t);
            return;
        }
        if(n<140)return Shot(2,n,x,y,bgr);
        if(n<143)return Shot(3,n,x,y,bgr);             // cut to D, then to E 3 frames on
        Shot(4,n,x,y,bgr);
    };
    auto cuts=scene_detect::Detect(video,nullptr,scene_detect::Params{});
    ASSERT_EQ(4u,cuts.size()) << ::testing::PrintToString(cuts);
    EXPECT_EQ(0,cuts[0]);
    EXPECT_EQ(40,cuts[1]);
    EXPECT_NEAR(90,cuts[2],2) << "the fade, at its middle";
    EXPECT_TRUE(cuts[3]==140||cuts[3]==143) << "cuts 3 frames apart are one: " << cuts[3];
    EXPECT_EQ(180,video.reads) << "each frame read once";
    scene_detect::Params hard;
    hard.gradual=false;
    video.reads=0;
    auto only_cuts=scene_detect::Detect(video,nullptr,hard);
    ASSERT_EQ(3u,only_cuts.size()) << "fades not looked for: " << ::testing::PrintToString(only_cuts);
    EXPECT_EQ(40,only_cuts[1]);

    CancelledSink cancelled;
    EXPECT_TRUE(scene_detect::Detect(video,&cancelled,scene_detect::Params{}).empty());
}

TEST(scene_detect, another_angle_in_the_same_colours_is_a_cut_and_a_flat_dissolve_one_at_its_middle) {
    // Drawn as cartoons are: flat colours, a sky and the ground, a few
    // shapes. The second shot is the same place from elsewhere: the same
    // colours in much the same amounts, laid out otherwise
    auto place=[](int layout,int x,int y,unsigned char *bgr){
        static const unsigned char sky[3]={230,190,120},ground[3]={70,150,90},house[3]={60,60,200},tree[3]={40,110,40};
        int const horizon=layout?60:110;
        unsigned char const *c=y<horizon?sky:ground;
        if(layout==0&&x>40&&x<120&&y>60&&y<110)c=house;
        if(layout==0&&x>220&&x<260&&y>30&&y<110)c=tree;
        if(layout==1&&x>180&&x<290&&y>20&&y<60)c=house;
        if(layout==1&&x>30&&x<60&&y>10&&y<60)c=tree;
        for(int i=0;i<3;++i)bgr[i]=c[i];
    };
    DrawnVideo video(120,320,180);
    video.draw=[&](int n,int x,int y,unsigned char *bgr){
        if(n<40)return place(0,x+n/4,y,bgr);
        if(n<80)return place(1,x+n/4,y,bgr);     // cut to another angle
        unsigned char a[3],b[3];                   // dissolves back over 20 frames
        place(1,x+n/4,y,a);place(0,x+n/4,y,b);
        double t=std::min(1.,(n-79)/21.);
        for(int i=0;i<3;++i)bgr[i]=static_cast<unsigned char>(a[i]*(1-t)+b[i]*t);
    };
    auto cuts=scene_detect::Detect(video,nullptr,scene_detect::ForSensitivity(1));
    ASSERT_EQ(3u,cuts.size()) << "medium sensitivity: " << ::testing::PrintToString(cuts);
    EXPECT_EQ(40,cuts[1]);
    EXPECT_NEAR(90,cuts[2],2) << "the dissolve, at its middle, once";
    auto hard=scene_detect::ForSensitivity(1);
    hard.gradual=false;
    EXPECT_EQ((std::vector<int>{0,40}),scene_detect::Detect(video,nullptr,hard)) << "dissolves not looked for";
}

TEST(scene_detect, dissolves_and_fades_short_and_long_are_found_and_a_pan_is_not) {
    // Shots of their own colours, each with a texture moving 2 px a frame
    auto mix=[](int a,int b,double t,int n,int x,int y,unsigned char *bgr){
        unsigned char p[3],q[3];Shot(a,n,x,y,p);Shot(b,n,x,y,q);
        for(int i=0;i<3;++i)bgr[i]=static_cast<unsigned char>(p[i]*(1-t)+q[i]*t);
    };
    // Shot 4 is near black: a fade
    struct Gradual { int from, length; };
    for(auto g:{Gradual{60,6},Gradual{60,12},Gradual{60,30},Gradual{60,45}}) {
        DrawnVideo video(160,160,90);
        video.draw=[&](int n,int x,int y,unsigned char *bgr){
            if(n<g.from)return Shot(0,n,x,y,bgr);
            if(n<g.from+g.length)return mix(0,2,(n-g.from+1.)/(g.length+1),n,x,y,bgr);
            Shot(2,n,x,y,bgr);
        };
        auto cuts=scene_detect::Detect(video,nullptr,scene_detect::ForSensitivity(1));
        ASSERT_EQ(2u,cuts.size()) << g.length << " frames: " << ::testing::PrintToString(cuts);
        EXPECT_NEAR(g.from+g.length/2.,cuts[1],3) << g.length << " frames: at its middle";
    }
    // Faded out to black and in again: once, about the black
    DrawnVideo fades(160,160,90);
    fades.draw=[&](int n,int x,int y,unsigned char *bgr){
        if(n<50)return Shot(0,n,x,y,bgr);
        if(n<60)return mix(0,4,(n-49)/11.,n,x,y,bgr);
        if(n<64)return Shot(4,n,x,y,bgr);
        if(n<74)return mix(4,1,(n-63)/11.,n,x,y,bgr);
        Shot(1,n,x,y,bgr);
    };
    auto cuts=scene_detect::Detect(fades,nullptr,scene_detect::ForSensitivity(1));
    ASSERT_GE(cuts.size(),2u) << ::testing::PrintToString(cuts);
    ASSERT_LE(cuts.size(),3u) << "out and in, at most: " << ::testing::PrintToString(cuts);
    EXPECT_GE(cuts[1],50);EXPECT_LE(cuts.back(),74);

    // The camera panning fast over one place, at high sensitivity: no
    // dissolve however the picture changes
    DrawnVideo pan(160,160,90);
    pan.draw=[](int n,int x,int y,unsigned char *bgr){Shot(3,3*n,x,y,bgr);};
    EXPECT_EQ((std::vector<int>{0}),scene_detect::Detect(pan,nullptr,scene_detect::ForSensitivity(2)));
    auto m=scene_detect::Measure(pan,nullptr,3);
    double most=0;for(double f:m.far_difference)most=std::max(most,f);
    EXPECT_GT(most,0.3) << "frames 12 apart do differ";

    // Tilting up a smooth sky: the frame half way is exactly the two either
    // side mixed, as in a dissolve, but moving the picture explains it
    DrawnVideo tilt(80,160,180);
    tilt.draw=[](int n,int,int y,unsigned char *bgr){
        double v=(y+4*n)*0.5;
        bgr[0]=static_cast<unsigned char>(v);bgr[1]=static_cast<unsigned char>(255-v);bgr[2]=120;
    };
    auto sky=scene_detect::Measure(tilt,nullptr,3);
    EXPECT_GT(sky.far_difference[40],0.9) << "every cell changes";
    EXPECT_LT(sky.blend[40],0.05) << "and looks mixed";
    EXPECT_EQ((std::vector<int>{0}),scene_detect::Cuts(sky,scene_detect::ForSensitivity(2)));
}

TEST(scene_detect, black_bars_and_the_subtitles_band_are_left_out) {
    // A picture letterboxed: black above and below, a subtitle burnt in
    // near its bottom now and then
    auto frame=[](int n,int x,int y,unsigned char *bgr){
        if(y<20||y>=160){bgr[0]=bgr[1]=bgr[2]=0;return;}
        if(n%30>=15&&y>=128&&y<156&&x>=30&&x<290&&(x/6+y/5)%2){bgr[0]=bgr[1]=bgr[2]=255;return;}
        Shot(n<60?0:1,0,x,y,bgr);
    };
    DrawnVideo video(120,320,180);
    video.draw=frame;
    auto picture=scene_detect::FindPicture(video,24);
    EXPECT_EQ(0,picture.left);EXPECT_EQ(scene_detect::thumb_w,picture.right);
    EXPECT_NEAR(20*scene_detect::thumb_h/180.,picture.top,1) << "the bar above";
    EXPECT_NEAR(160*scene_detect::thumb_h/180.,picture.bottom,1) << "the bar below";

    // Without the band the subtitles' coming and going are cuts at high
    // sensitivity; with it only the cut is
    auto high=scene_detect::ForSensitivity(2);
    auto whole=scene_detect::Cuts(scene_detect::Measure(video,nullptr,3,picture),high);
    EXPECT_GT(whole.size(),2u) << "the subtitles seen: " << ::testing::PrintToString(whole);
    auto band=scene_detect::WithoutSubtitles(picture,25);
    EXPECT_EQ(picture.top,band.top);
    EXPECT_LT(band.bottom,picture.bottom);
    EXPECT_EQ((std::vector<int>{0,60}),scene_detect::Cuts(scene_detect::Measure(video,nullptr,3,band),high));

    // The bars change nothing but the share: the same cut is as strong
    // as in the picture without them
    DrawnVideo bare(120,320,140);
    bare.draw=[](int n,int x,int y,unsigned char *bgr){Shot(n<60?0:1,0,x,y+20,bgr);};
    auto barred=scene_detect::Measure(video,nullptr,3,picture);
    auto plain=scene_detect::Measure(bare,nullptr,3);
    EXPECT_NEAR(plain.diffs[60],barred.diffs[60],0.1);
    EXPECT_LT(scene_detect::Measure(video,nullptr,3).diffs[60],barred.diffs[60]) << "the bars counted dilute it";

    // A dark video is not all bar
    DrawnVideo dark(30,320,180);
    dark.draw=[](int,int,int,unsigned char *bgr){bgr[0]=bgr[1]=bgr[2]=5;};
    EXPECT_EQ(scene_detect::Area{},scene_detect::FindPicture(dark,8));
    EXPECT_EQ(scene_detect::Area{},scene_detect::FindPicture(std::vector<scene_detect::Signature>{}));
}

TEST(scene_detect, signature_reads_any_pitch_and_flipped_frames) {
    VideoFrame plain{{},50,30,200,false},padded{{},50,30,232,false},flipped{{},50,30,200,true};
    plain.data.resize(200*30);padded.data.resize(232*30);flipped.data.resize(200*30);
    for(int y=0;y<30;++y)for(int x=0;x<50;++x)for(int c=0;c<4;++c){
        unsigned char v=static_cast<unsigned char>((x*5+y*7+c*40)%256);
        plain.data[y*200+x*4+c]=v;padded.data[y*232+x*4+c]=v;flipped.data[(29-y)*200+x*4+c]=v;
    }
    auto a=scene_detect::Sign(plain);
    EXPECT_EQ(0.,scene_detect::Difference(a,scene_detect::Sign(padded)));
    EXPECT_EQ(0.,scene_detect::Difference(a,scene_detect::Sign(flipped)));
    VideoFrame black{std::vector<unsigned char>(200*30,0),50,30,200,false};
    EXPECT_GT(scene_detect::Difference(a,scene_detect::Sign(black)),0.3);
}

TEST(scene_detect, close_cuts_keep_the_strongest_and_the_start_is_not_one) {
    scene_detect::Params p;
    std::vector<double> diffs(60,0.01),across(60,1.);
    diffs[3]=0.9;                  // too near the start
    diffs[20]=0.5;diffs[23]=0.8;   // three frames apart: the stronger
    diffs[40]=0.6;across[40]=0.05; // the picture came back: a flash
    diffs[50]=0.2;
    EXPECT_EQ((std::vector<int>{23,50}),scene_detect::FindCuts(diffs,across,p));
    EXPECT_EQ((std::vector<int>{23}),scene_detect::FindCuts(diffs,across,scene_detect::ForSensitivity(0))) << "low sensitivity misses the weak cut";
}

TEST(scene_detect, things_moving_each_their_own_way_are_not_a_cut_and_a_cut_into_fast_motion_is) {
    // A still scene; at frame 40 three things in it jump a few cells, each
    // its own way, as arms and ears do. At 60 a cut to a shot panning fast.
    struct Thing { int x,y,w,h,dx,dy; unsigned char colour[3]; };
    static const Thing things[]={{40,30,70,55,15,0,{30,30,220}},{150,90,60,60,-15,10,{40,200,60}},{230,40,55,70,0,-15,{220,140,30}}};
    DrawnVideo video(100,320,180);
    video.draw=[](int n,int x,int y,unsigned char *bgr){
        if(n>=60)return Shot(1,20*n,x,y,bgr);
        bgr[0]=static_cast<unsigned char>(200-y/3);bgr[1]=static_cast<unsigned char>(150+x/8);bgr[2]=90;
        for(auto const& t:things){
            int const ox=t.x+(n>=40?t.dx:0),oy=t.y+(n>=40?t.dy:0);
            if(x>=ox&&x<ox+t.w&&y>=oy&&y<oy+t.h){
                // Each with a pattern of its own, moving with it
                int const stripe=((x-ox)/6+(y-oy)/9)%2;
                for(int c=0;c<3;++c)bgr[c]=static_cast<unsigned char>(t.colour[c]*(stripe?1.:0.6));
            }
        }
    };
    auto m=scene_detect::Measure(video,nullptr,3);
    ASSERT_EQ(100u,m.unmatched.size());
    EXPECT_GE(m.diffs[40],scene_detect::ForSensitivity(1).threshold) << "enough changes to be taken for a cut";
    EXPECT_LT(m.unmatched[40],0.4) << "but each part is found moved a little";
    EXPECT_GT(m.unmatched[60],0.9) << "another picture";
    auto high=scene_detect::ForSensitivity(2);
    high.gradual=false;
    EXPECT_EQ((std::vector<int>{0,60}),scene_detect::Cuts(m,high));
    auto unchecked=high;
    unchecked.least_unmatched=0;
    EXPECT_EQ((std::vector<int>{40,60}),scene_detect::FindCuts(m.diffs,m.across,unchecked,m.unmatched)) << "the things moving were a cut before";
    // The frames after the cut change as much as it does: it is a cut by
    // the still frames before it, being so wholly another picture
    EXPECT_GT(m.diffs[70],0.5);
    std::vector<double> partly(m.unmatched.size(),0.85);
    EXPECT_EQ((std::vector<int>{40}),scene_detect::FindCuts(m.diffs,m.across,unchecked,partly)) << "not when less of it is another";
    EXPECT_EQ(scene_detect::UnmatchedShare(scene_detect::Signature{},scene_detect::Signature{}),0.) << "nothing changed";
}

TEST(scene_detect, parts_read_on_their_own_find_what_one_reading_finds) {
    int const n=4*scene_detect::min_part_frames+7;
    auto draw=[](int f,int x,int y,unsigned char *bgr){
        if(f%97==50){bgr[0]=bgr[1]=bgr[2]=255;return;} // flashes, some at the parts' edges
        Shot((f/131)%5,f,x,y,bgr);                      // a cut every 131 frames
    };
    std::vector<std::unique_ptr<DrawnVideo>> videos;std::vector<VideoProvider*> raw;
    for(int k=0;k<4;++k){videos.push_back(std::make_unique<DrawnVideo>(n,64,36));videos.back()->draw=draw;raw.push_back(videos.back().get());}
    auto whole=scene_detect::Measure(*raw[0],nullptr,3);
    videos[0]->reads=0;
    auto parts=scene_detect::Measure(raw,nullptr,3);
    EXPECT_EQ(whole.diffs,parts.diffs);
    EXPECT_EQ(whole.across,parts.across);
    EXPECT_EQ(whole.far_difference,parts.far_difference);
    EXPECT_EQ(whole.blend,parts.blend);
    EXPECT_EQ(whole.moved,parts.moved);
    EXPECT_EQ(whole.unmatched,parts.unmatched);
    int reads=0;for(auto const& v:videos){EXPECT_GT(v->reads,0) << "each reader has a part";reads+=v->reads;}
    EXPECT_LE(reads,n+3*(scene_detect::gradual_span+3)) << "each frame read once, but for a few each side of a part";
    EXPECT_EQ(scene_detect::Cuts(whole,scene_detect::Params{}),scene_detect::Cuts(parts,scene_detect::Params{}));

    // Too short to be worth cutting up: one reader
    DrawnVideo shortv(scene_detect::min_part_frames+10,64,36),other(scene_detect::min_part_frames+10,64,36);
    shortv.draw=other.draw=draw;
    scene_detect::Measure(std::vector<VideoProvider*>{&shortv,&other},nullptr,3);
    EXPECT_EQ(0,other.reads);

    CancelledSink cancelled;
    EXPECT_TRUE(scene_detect::Measure(raw,&cancelled,3).empty());

    // A reader that fails: the error, once all have stopped
    struct Failing final : VideoProvider {
        int n;explicit Failing(int n):n(n){}
        void GetFrame(int,VideoFrame&) override { throw VideoDecodeError("broken"); }
        void SetColorSpace(agi::ycbcr::Header) override {}
        int GetFrameCount() const override { return n; }
        int GetWidth() const override { return 64; }
        int GetHeight() const override { return 36; }
        double GetDAR() const override { return 16./9; }
        agi::vfr::Framerate GetFPS() const override { return agi::vfr::Framerate(25,1); }
        std::vector<int> GetKeyFrames() const override { return {}; }
        agi::ycbcr::header_colorspace GetColorSpace() const override { return agi::ycbcr::header_colorspace::unspecified(); }
        std::string GetDecoderName() const override { return "failing"; }
    } failing(n);
    EXPECT_THROW(scene_detect::Measure(std::vector<VideoProvider*>{raw[0],&failing},nullptr,3),VideoDecodeError);
}

TEST(scene_detect, measures_are_kept_and_read_back) {
    scene_detect::Measures m;
    m.flash_span=3;
    m.diffs={0.,0.25,0.5};
    m.across={std::numeric_limits<double>::infinity(),0.125,1.};
    m.far_difference={0.,0.5,0.75};m.blend={1.,0.25,0.};m.moved={1.,1.,0.5};m.unmatched={1.,0.25,0.75};
    m.area={1,63,2,30};
    agi::fs::path file((std::filesystem::current_path()/"kept.measures").wstring());
    scene_detect::SaveMeasures(file,m);
    auto back=scene_detect::LoadMeasures(file);
    EXPECT_EQ(3,back.flash_span);
    EXPECT_EQ(m.diffs,back.diffs);
    EXPECT_EQ(m.across,back.across);
    EXPECT_EQ(m.far_difference,back.far_difference);EXPECT_EQ(m.blend,back.blend);EXPECT_EQ(m.moved,back.moved);EXPECT_EQ(m.unmatched,back.unmatched);
    EXPECT_EQ(m.area,back.area);
    EXPECT_EQ((std::vector<int>{0}),scene_detect::Cuts(back,scene_detect::Params{})) << "frames too near the start";
    {agi::io::Save junk(file);junk.Get()<<"not measures";}
    EXPECT_TRUE(scene_detect::LoadMeasures(file).empty());
    EXPECT_TRUE(scene_detect::LoadMeasures(agi::fs::path("no-such.measures")).empty());

    DrawnVideo video(10,640,360);
    video.draw=[](int,int,int,unsigned char*){};
    EXPECT_FALSE(video.ScaleDownForAnalysis(scene_detect::analysis_width)) << "a provider that cannot keeps its frames whole";
}

TEST(keyframe_snap, moves_near_ends_onto_scene_changes) {
    agi::vfr::Framerate fps(25,1);
    AssDialogue near_both,far_start,across;
    near_both.Start=1950;near_both.End=5950;  // 50 ms before frame 50; ends 50 ms before 150
    far_start.Start=1500;far_start.End=3000;  // 500 ms from any
    across.Start=fps.TimeAtFrame(50,agi::vfr::START);across.End=fps.TimeAtFrame(149,agi::vfr::END); // on them, frame 100 in between
    KeyframeSnapThresholds limits{100,100,100,100};
    SnapToKeyframes({&near_both,&far_start,&across},{0,50,100,150},0,fps,limits);
    EXPECT_EQ(fps.TimeAtFrame(50,agi::vfr::START),int(near_both.Start));
    EXPECT_EQ(fps.TimeAtFrame(149,agi::vfr::END),int(near_both.End)) << "ends with the frame before the cut";
    EXPECT_EQ(1500,int(far_start.Start));EXPECT_EQ(3000,int(far_start.End));
    EXPECT_EQ(fps.TimeAtFrame(50,agi::vfr::START),int(across.Start));
    EXPECT_EQ(fps.TimeAtFrame(149,agi::vfr::END),int(across.End)) << "not split at frame 100";

    // A long shot with lines all through it: only the ends near its cuts
    // move, each line by itself; those in its middle stay
    AssDialogue first,middle,last;
    first.Start=fps.TimeAtFrame(50,agi::vfr::START)-300;first.End=2900; // starts just before the cut at 50
    middle.Start=3000;middle.End=3400;                                 // 600 ms and more from either cut
    last.Start=3500;last.End=fps.TimeAtFrame(99,agi::vfr::END)+300;     // ends just after the cut at 100
    auto moved=SnapToKeyframes({&first,&middle,&last},{0,50,100,150},0,fps,{500,500,500,500});
    EXPECT_EQ(fps.TimeAtFrame(50,agi::vfr::START),int(first.Start));EXPECT_EQ(2900,int(first.End));
    EXPECT_EQ(3000,int(middle.Start));EXPECT_EQ(3400,int(middle.End));
    EXPECT_EQ(3500,int(last.Start));EXPECT_EQ(fps.TimeAtFrame(99,agi::vfr::END),int(last.End));
    EXPECT_EQ(1,moved.starts);EXPECT_EQ(1,moved.ends);

    // A short line about one cut: moving both its ends onto the cut would
    // leave nothing, so only the one keeping more of it moves
    int const cut=fps.TimeAtFrame(50,agi::vfr::START);
    AssDialogue mostly_before,mostly_after;
    mostly_before.Start=cut-180;mostly_before.End=cut+100;
    mostly_after.Start=cut-100;mostly_after.End=cut+180;
    SnapToKeyframes({&mostly_before,&mostly_after},{0,50,100,150},0,fps,{500,500,500,500});
    EXPECT_EQ(cut-180,int(mostly_before.Start));EXPECT_EQ(fps.TimeAtFrame(49,agi::vfr::END),int(mostly_before.End)) << "ends at the cut";
    EXPECT_EQ(cut,int(mostly_after.Start)) << "starts at the cut";EXPECT_EQ(cut+180,int(mostly_after.End));
}

TEST_F(ordinary_edit, detect_scene_changes_loads_them_as_keyframes_and_keeps_them) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseKeyframes(); c->project->CloseVideo(); };
    EXPECT_FALSE(c->project->CanCloseKeyframes());
    std::filesystem::remove_all(std::filesystem::path(config::path->Decode("?user/scenes").wstring()));
    cmd::call("keyframe/detect",c.get());
    EXPECT_EQ((std::vector<int>{0}),c->project->Keyframes()) << "one plain shot";
    EXPECT_TRUE(c->project->CanCloseKeyframes());
    std::vector<agi::fs::path> cached,measured;
    for(auto const& f:std::filesystem::directory_iterator(std::filesystem::path(config::path->Decode("?user/scenes").wstring()))){
        agi::fs::path file(f.path().wstring());
        auto name=file.filename().string();
        if(name.ends_with(".key.txt"))cached.push_back(file);
        else if(name.ends_with(".measures"))measured.push_back(file);
    }
    ASSERT_EQ(1u,cached.size());
    ASSERT_EQ(1u,measured.size()) << "what reading the video found is kept too";
    // Found once: the next time is the kept list
    agi::keyframe::Save(cached[0],{0,10,20});
    cmd::call("keyframe/detect",c.get());
    EXPECT_EQ((std::vector<int>{0,10,20}),c->project->Keyframes());
    // Another sensitivity: its cuts come from the kept measures, the video
    // (one plain shot, no cuts in it) not read again
    auto measures=scene_detect::LoadMeasures(measured[0]);
    ASSERT_EQ(250u,measures.diffs.size());
    measures.diffs[100]=0.9;measures.across[100]=1.;
    scene_detect::SaveMeasures(measured[0],measures);
    int const sensitivity=OPT_GET("Tool/Scene Detection/Sensitivity")->GetInt();
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Tool/Scene Detection/Sensitivity")->SetInt(sensitivity); };
    OPT_SET("Tool/Scene Detection/Sensitivity")->SetInt(sensitivity==2?0:2);
    cmd::call("keyframe/detect",c.get());
    EXPECT_EQ((std::vector<int>{0,100}),c->project->Keyframes());

    // Dissolves looked for: from the same measures, the video not read
    // again. One made up in them at frame 200, mixed from 194 to 206.
    measures=scene_detect::LoadMeasures(measured[0]);
    for(int i=200;i<=206;++i){measures.far_difference[i]=0.9;measures.blend[i]=0.;measures.moved[i]=0.9;}
    scene_detect::SaveMeasures(measured[0],measures);
    // (the cuts already decided from them, before it was made up, forgotten)
    for(auto const& f:std::filesystem::directory_iterator(std::filesystem::path(config::path->Decode("?user/scenes").wstring())))
        if(f.path().filename().string().ends_with(".key.txt"))std::filesystem::remove(f.path());
    bool const gradual=OPT_GET("Tool/Scene Detection/Gradual")->GetBool();
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Tool/Scene Detection/Gradual")->SetBool(gradual); };
    OPT_SET("Tool/Scene Detection/Gradual")->SetBool(false);
    cmd::call("keyframe/detect",c.get());
    EXPECT_EQ((std::vector<int>{0,100}),c->project->Keyframes()) << "dissolves not looked for";
    OPT_SET("Tool/Scene Detection/Gradual")->SetBool(true);
    cmd::call("keyframe/detect",c.get());
    EXPECT_EQ((std::vector<int>{0,100,197}),c->project->Keyframes()) << "the dissolve, at its middle";

    // Another band left out for subtitles: other cells compared, so the
    // video is read again, into measures of their own
    int const band=OPT_GET("Tool/Scene Detection/Subtitle Band")->GetInt();
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Tool/Scene Detection/Subtitle Band")->SetInt(band); };
    OPT_SET("Tool/Scene Detection/Subtitle Band")->SetInt(band==30?10:30);
    cmd::call("keyframe/detect",c.get());
    EXPECT_EQ((std::vector<int>{0}),c->project->Keyframes()) << "read again: one plain shot";
    int kept=0;
    for(auto const& f:std::filesystem::directory_iterator(std::filesystem::path(config::path->Decode("?user/scenes").wstring())))
        kept+=f.path().filename().string().ends_with(".measures");
    EXPECT_EQ(2,kept);
    auto banded=scene_detect::LoadMeasures(measured[0]);
    ASSERT_EQ(250u,banded.far_difference.size());
    EXPECT_FLOAT_EQ(0.9f,static_cast<float>(banded.far_difference[200])) << "the first band's are as they were";
    c->project->CloseKeyframes();
    EXPECT_FALSE(c->project->CanCloseKeyframes());
}

TEST_F(ordinary_edit, detect_scene_changes_times_empty_subtitles_a_line_a_shot) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseKeyframes(); c->project->CloseVideo(); };
    auto const& fps=c->project->Timecodes();
    std::filesystem::remove_all(std::filesystem::path(config::path->Decode("?user/scenes").wstring()));
    // A new file: its one empty line makes way for one a shot
    ASSERT_EQ(1u,LiveRead(*c).Events.size());
    cmd::call("keyframe/detect",c.get());
    ASSERT_EQ(1u,LiveRead(*c).Events.size()) << "one plain shot";
    EXPECT_EQ(0,int(LiveRead(*c).Events.front().Start));
    EXPECT_EQ(fps.TimeAtFrame(249,agi::vfr::END),int(LiveRead(*c).Events.front().End));

    agi::fs::path cached;
    for(auto const& f:std::filesystem::directory_iterator(std::filesystem::path(config::path->Decode("?user/scenes").wstring())))
        if(f.path().filename().string().ends_with(".key.txt"))cached=agi::fs::path(f.path().wstring());
    ASSERT_FALSE(cached.empty());
    agi::keyframe::Save(cached,{0,100,200});
    // Still all empty: timed again, from each cut to the frame before the next
    cmd::call("keyframe/detect",c.get());
    ASSERT_EQ(3u,LiveRead(*c).Events.size());
    std::vector<std::pair<int,int>> times;
    for(auto const& line:LiveRead(*c).Events){ times.emplace_back(line.Start,line.End); EXPECT_EQ("",line.Text.get()); }
    EXPECT_EQ((std::vector<std::pair<int,int>>{
        {0,fps.TimeAtFrame(99,agi::vfr::END)},
        {fps.TimeAtFrame(100,agi::vfr::START),fps.TimeAtFrame(199,agi::vfr::END)},
        {fps.TimeAtFrame(200,agi::vfr::START),fps.TimeAtFrame(249,agi::vfr::END)}}),times);
    EXPECT_EQ(3u,c->selectionController->ReadSelectedLines().size()) << "the lines made are selected";
    // One undo step back to the one line
    c->subsController->Undo();
    EXPECT_EQ(1u,LiveRead(*c).Events.size());
    c->subsController->Redo();
    ASSERT_EQ(3u,LiveRead(*c).Events.size());

    // Something written: the lines are left as they are
    { auto write=c->subtitleAccess->TryWrite(); LiveWriteForTest(*c).Events.front().Text="hello"; LiveWriteForTest(*c).Commit("type",AssFile::COMMIT_DIAG_TEXT); }
    agi::keyframe::Save(cached,{0,50});
    cmd::call("keyframe/detect",c.get());
    EXPECT_EQ(3u,LiveRead(*c).Events.size());
    EXPECT_EQ((std::vector<int>{0,50}),c->project->Keyframes()) << "the keyframes are still loaded";
}

TEST_F(ordinary_edit, timeline_from_scene_changes_gives_the_shots_with_no_line_one_and_keeps_the_rest) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    agi::fs::path kf((std::filesystem::current_path()/"timeline-keyframes.txt").wstring());
    agi::keyframe::Save(kf,{0,100,200});
    c->project->LoadKeyframes(kf);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseKeyframes(); c->project->CloseVideo(); };
    auto const& fps=c->project->Timecodes();
    auto command=cmd::get("keyframe/timeline");
    ASSERT_TRUE(command->Validate(c.get()));
    // A line written in the middle shot, and a comment over the first: a
    // comment is not shown, so that shot still has none
    {auto write=c->subtitleAccess->TryWrite();auto& f=LiveWriteForTest(*c);
     auto& row=f.Events.front();row.Start=fps.TimeAtFrame(110,agi::vfr::START);row.End=fps.TimeAtFrame(180,agi::vfr::END);row.Text="hello";
     auto note=new AssDialogue;note->Comment=true;note->Start=0;note->End=1000;note->Text="note";f.Events.push_back(*note);
     f.Commit("lines",AssFile::COMMIT_DIAG_ADDREM);}
    auto description=c->subsController->GetUndoDescription();
    cmd::call("keyframe/timeline",c.get());
    std::vector<std::tuple<int,int,std::string>> lines;
    for(auto const& line:LiveRead(*c).Events) lines.emplace_back(line.Start,line.End,line.Text.get());
    EXPECT_EQ((std::vector<std::tuple<int,int,std::string>>{
        {0,fps.TimeAtFrame(99,agi::vfr::END),""},
        {fps.TimeAtFrame(110,agi::vfr::START),fps.TimeAtFrame(180,agi::vfr::END),"hello"},
        {0,1000,"note"},
        {fps.TimeAtFrame(200,agi::vfr::START),fps.TimeAtFrame(249,agi::vfr::END),""}}),lines) << "among the others by time, all kept";
    EXPECT_EQ(2u,c->selectionController->ReadSelectedLines().size());
    c->subsController->Undo();
    EXPECT_EQ(2u,LiveRead(*c).Events.size()) << "one undo step";
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    c->subsController->Redo();
    // Every shot has a line now: nothing added, no empty undo step
    cmd::call("keyframe/timeline",c.get());
    EXPECT_EQ(4u,LiveRead(*c).Events.size());
    c->subsController->Undo();
    EXPECT_EQ(2u,LiveRead(*c).Events.size());
}

TEST_F(ordinary_edit, snap_to_nearby_scene_changes_moves_the_selected_lines_in_one_step) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    agi::fs::path kf((std::filesystem::current_path()/"snap-keyframes.txt").wstring());
    agi::keyframe::Save(kf,{0,50,150});
    c->project->LoadKeyframes(kf);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseKeyframes(); c->project->CloseVideo(); };
    // Timed to the audio, a line starts and ends further from the cuts than
    // the timing post-processor's few frames: the default distance out
    // reaches the end, 400 ms on; the start, 400 ms in, stays
    {auto write=c->subtitleAccess->TryWrite();auto& row=LiveWriteForTest(*c).Events.front();
     row.Start=1600;row.End=5600;LiveWriteForTest(*c).Commit("times",AssFile::COMMIT_DIAG_TIME);}
    {auto id=LiveRead(*c).Events.front().Id;c->selectionController->SetSelectionAndActiveIDs({id},id);}
    auto description=c->subsController->GetUndoDescription();
    ASSERT_TRUE(cmd::get("time/snap/scene_nearby")->Validate(c.get()));
    cmd::call("time/snap/scene_nearby",c.get());
    auto const& row=LiveRead(*c).Events.front();
    EXPECT_EQ(1600,int(row.Start));
    EXPECT_EQ(c->project->Timecodes().TimeAtFrame(149,agi::vfr::END),int(row.End));
    c->subsController->Undo();
    // Set to reach in that far, the start goes too
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Timing/Scene Snap/Start Later")->SetInt(100); };
    OPT_SET("Timing/Scene Snap/Start Later")->SetInt(500);
    cmd::call("time/snap/scene_nearby",c.get());
    EXPECT_EQ(c->project->Timecodes().TimeAtFrame(50,agi::vfr::START),int(LiveRead(*c).Events.front().Start));
    EXPECT_EQ(c->project->Timecodes().TimeAtFrame(149,agi::vfr::END),int(LiveRead(*c).Events.front().End));
    c->subsController->Undo();
    EXPECT_EQ(1600,int(LiveRead(*c).Events.front().Start));EXPECT_EQ(5600,int(LiveRead(*c).Events.front().End));
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    // Nothing within the distance: nothing changed, no empty undo step
    OPT_SET("Timing/Scene Snap/Start Later")->SetInt(100);
    OPT_SET("Timing/Scene Snap/End Later")->SetInt(100);
    BOOST_SCOPE_EXIT_ALL(&){ OPT_SET("Timing/Scene Snap/End Later")->SetInt(500); };
    cmd::call("time/snap/scene_nearby",c.get());
    EXPECT_EQ(1600,int(LiveRead(*c).Events.front().Start));EXPECT_EQ(5600,int(LiveRead(*c).Events.front().End));
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
}

TEST_F(ordinary_edit, one_scene_snap_distance_set_before_goes_out_and_a_little_way_in) {
    BOOST_SCOPE_EXIT_ALL(&){
        OPT_SET("Timing/Scene Snap Distance")->SetInt(-1);
        for(auto o:{"Start Earlier","End Later"})OPT_SET(std::string("Timing/Scene Snap/")+o)->SetInt(500);
        for(auto o:{"Start Later","End Earlier"})OPT_SET(std::string("Timing/Scene Snap/")+o)->SetInt(100); };
    // Nothing set before: the four stay as they are
    OPT_SET("Timing/Scene Snap/Start Later")->SetInt(40);
    MigrateSceneSnapDistance();
    EXPECT_EQ(40,OPT_GET("Timing/Scene Snap/Start Later")->GetInt());
    OPT_SET("Timing/Scene Snap Distance")->SetInt(300);
    MigrateSceneSnapDistance();
    EXPECT_EQ(300,OPT_GET("Timing/Scene Snap/Start Earlier")->GetInt());
    EXPECT_EQ(300,OPT_GET("Timing/Scene Snap/End Later")->GetInt());
    EXPECT_EQ(100,OPT_GET("Timing/Scene Snap/Start Later")->GetInt());
    EXPECT_EQ(100,OPT_GET("Timing/Scene Snap/End Earlier")->GetInt());
    EXPECT_EQ(-1,OPT_GET("Timing/Scene Snap Distance")->GetInt()) << "once";
    OPT_SET("Timing/Scene Snap Distance")->SetInt(60);
    MigrateSceneSnapDistance();
    EXPECT_EQ(60,OPT_GET("Timing/Scene Snap/Start Later")->GetInt()) << "less than 100 in stays as it was";
}

namespace {
std::string MagiaConfig() {
    // The shape of MagiaTimeline 1.1's config.yml, cut short
    return "# MagiaTimeline config\n"
           "source:\n- src.mp4\n# - src2.mp4\n"
           "destination:\n- ...\n# - MagiaTimelineOutput2\n"
           "outputSrt: false\n"
           "assTemplate: template.asst\n"
           "enableOpenCL: false\n"
           "maxResWidth: 2000\nmaxResHeight: 1200\n"
           "strategy: dtd\n"
           "preset: default\n"
           "mr:\n  default:\n    dialogOutlineRect: [0.25, 0.75, 0.60, 0.95]\n  narrow:\n    dialogOutlineRect: [0.21, 0.79, 0.60, 0.97]\n"
           "otl:\n  hotelzdm:\n    dialogRect: [0.00, 1.00, 0.75, 1.00]\n    textHSVRanges:\n      - [0, 0, 240, 180, 16, 255]\n    sobelThreshold: 200\n"
           "bcs:\n  default:\n    dialogRect: [0.00, 1.00, 0.75, 1.00]\n    featureThreshold: 0.3\n"
           "dtd:\n  default:\n    dialogRect: [0.00, 1.00, 0.75, 1.00]\n    featureThreshold: 2 # how much must change\n"
           "engine: speculative\n"
           "framewise:\n  # sampleInterval: in how many frames to sample a frame\n  sampleInterval: 1\n  debug: true\n"
           "speculative:\n  emptyGapForceCheck: 1000\n"
           "extraJobs: []\n"
           "ocr:\n  standaloneOutput: true\n  standaloneOutputSuffix: \"-ocr.txt\"\n  doPaddle: true\n  doTesseract: false\n  tesseractLang: \"jpn\"\n";
}
std::string Magia10Config() {
    // 1.0's: no dtd, no size limit, its OCR written to a file of its own
    return "source:\n- src.mp4\n"
           "destination:\n- MagiaTimelineOutput\n"
           "assTemplate: template.asst\n"
           "enableOpenCL: false\n"
           "contentRect: [0.0, 1.0, 0.0, 1.0]\n"
           "strategy: bcs\n"
           "preset: default\n"
           "bcs:\n  default:\n    dialogRect: [0.00, 1.00, 0.75, 1.00]\n"
           "otl:\n  default:\n    dialogRect: [0.00, 1.00, 0.75, 1.00]\n"
           "engine: speculative\n"
           "framewise:\n  sampleInterval: 1\n"
           "speculative:\n  emptyGapForceCheck: 1000\n"
           "extraJobs:\n"
           "ocr:\n  suffix: \"-ocr.txt\"\n  separator: \"@\"\n  doPaddle: true\n  paddleLang: \"japan\"\n  doTesseract: true\n  tesseractLang: \"jpn\"\n";
}
agi::fs::path MagiaFolder(std::string const& name) {
    auto dir=std::filesystem::current_path()/name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return agi::fs::path(dir.wstring());
}
void WriteText(agi::fs::path const& p,std::string const& text,bool executable=false) {
    std::filesystem::create_directories(std::filesystem::path(p.parent_path().wstring()));
    {std::ofstream out(std::filesystem::path(p.wstring()),std::ios::binary);out<<text;}
    if(executable)std::filesystem::permissions(std::filesystem::path(p.wstring()),std::filesystem::perms::owner_all|std::filesystem::perms::group_read|std::filesystem::perms::group_exec,std::filesystem::perm_options::replace);
}
std::string ReadText(agi::fs::path const& p) {
    std::ifstream in(std::filesystem::path(p.wstring()),std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
}

TEST(magia_timeline, config_is_given_the_video_and_where_to_write_and_keeps_the_rest) {
    auto out=magia_timeline::WriteConfig(MagiaConfig(),"/videos/ep 01's.mkv","/tmp/out","");
    ASSERT_TRUE(out);
    EXPECT_NE(std::string::npos,out->find("source:\n- \"/videos/ep 01's.mkv\"\n")) << *out;
    EXPECT_NE(std::string::npos,out->find("destination:\n- \"/tmp/out\"\n")) << *out;
    EXPECT_EQ(std::string::npos,out->find("src.mp4")) << "the old list and its commented items are gone";
    EXPECT_EQ(std::string::npos,out->find("MagiaTimelineOutput2"));
    EXPECT_NE(std::string::npos,out->find("strategy: dtd\n")) << "its own strategy, none being chosen";
    EXPECT_NE(std::string::npos,out->find("outputSrt: false\n"));
    EXPECT_NE(std::string::npos,out->find("    dialogRect: [0.00, 1.00, 0.75, 1.00]\n")) << "the strategies' settings untouched";
    auto chinese=magia_timeline::WriteConfig(MagiaConfig(),"C:\\视频\\第1话 \"终\".mkv","C:\\Users\\小明\\out\U0001F600","");
    ASSERT_TRUE(chinese);
    EXPECT_NE(std::string::npos,chinese->find("source:\n- \"C:\\\\\\u89C6\\u9891\\\\\\u7B2C1\\u8BDD \\\"\\u7EC8\\\".mkv\"\n")) << *chinese;
    EXPECT_NE(std::string::npos,chinese->find("destination:\n- \"C:\\\\Users\\\\\\u5C0F\\u660E\\\\out\\U0001F600\"\n")) << *chinese;
    for(unsigned char ch:*chinese) EXPECT_LT(ch,0x80) << "ASCII alone, read alike in any code page";
    EXPECT_EQ("abc\n",magia_timeline::Readable("abc\n"));
    EXPECT_EQ("视频",magia_timeline::Readable("视频"));
    auto chosen=magia_timeline::WriteConfig(MagiaConfig(),"v.mkv","o","bcs");
    ASSERT_TRUE(chosen);
    EXPECT_NE(std::string::npos,chosen->find("strategy: bcs\n"));
    EXPECT_EQ(std::string::npos,chosen->find("strategy: dtd"));
    EXPECT_FALSE(magia_timeline::WriteConfig("strategy: bcs\n","v","o",""));
}

TEST(magia_timeline, its_config_is_read_for_what_it_offers) {
    auto in=magia_timeline::Inspect(MagiaConfig());
    EXPECT_EQ((std::vector<std::string>{"mr","otl","bcs","dtd"}),in.strategies) << "the keys holding presets, in its order";
    EXPECT_EQ((std::vector<std::string>{"default","narrow"}),in.presets["mr"]);
    EXPECT_EQ("dtd",in.Get("strategy"));
    EXPECT_EQ("2",in.Get("dtd/default/featureThreshold")) << "without the comment after it";
    EXPECT_EQ("[[0, 0, 240, 180, 16, 255]]",in.Get("otl/hotelzdm/textHSVRanges")) << "a list an item a line";
    EXPECT_EQ("1",in.Get("framewise/sampleInterval")) << "past the comment above it";
    EXPECT_EQ("\"jpn\"",in.Get("ocr/tesseractLang"));
    EXPECT_EQ("[]",in.Get("extraJobs"));
    EXPECT_TRUE(in.Has("maxResWidth"));
    EXPECT_FALSE(in.Has("ocr/suffix"));
    EXPECT_FALSE(in.Has("framewise")) << "a key holding others has no value of its own";

    auto old=magia_timeline::Inspect(Magia10Config());
    EXPECT_EQ((std::vector<std::string>{"bcs","otl"}),old.strategies);
    EXPECT_FALSE(old.Has("maxResWidth"));
    EXPECT_TRUE(old.Has("ocr/suffix"));
    EXPECT_TRUE(old.Has("extraJobs")) << "there, with nothing after it";
    EXPECT_EQ("",old.Get("extraJobs"));

    auto rect=magia_timeline::ParseRect("[0.00, 1.00, 0.75, 1.00]");
    ASSERT_TRUE(rect);
    EXPECT_DOUBLE_EQ(0.75,rect->top);
    EXPECT_EQ("[0.0, 1.0, 0.75, 1.0]",magia_timeline::FormatRect(*rect));
    EXPECT_FALSE(magia_timeline::ParseRect("[0.0, 1.0, 0.5]"));
    EXPECT_FALSE(magia_timeline::ParseRect("[0.0, 1.0, x, 1.0]"));
    EXPECT_EQ("jpn",magia_timeline::Unquote("\"jpn\""));

    // What was chosen before, over what is installed
    magia_timeline::Values saved{{"strategy","dtd"},{"maxResWidth","1000"},{"bcs/default/dialogRect","[0.1, 0.9, 0.8, 1.0]"},{"preset","narrow"}};
    EXPECT_EQ(saved,magia_timeline::ParseSaved(magia_timeline::FormatSaved(saved)));
    auto on_old=magia_timeline::Effective(old,saved);
    EXPECT_EQ("bcs",on_old["strategy"]) << "a strategy it has not: its own";
    EXPECT_EQ("default",on_old["preset"]) << "a preset the strategy has not: its own";
    EXPECT_EQ(0u,on_old.count("maxResWidth")) << "a setting it has not is not given to it";
    EXPECT_EQ("[0.1, 0.9, 0.8, 1.0]",on_old["bcs/default/dialogRect"]);
    auto on_new=magia_timeline::Effective(in,saved);
    EXPECT_EQ("dtd",on_new["strategy"]);
    EXPECT_EQ("default",on_new["preset"]) << "dtd has no narrow";
    EXPECT_EQ("1000",on_new["maxResWidth"]);
    EXPECT_EQ((magia_timeline::Values{{"bcs/default/dialogRect","[0.1, 0.9, 0.8, 1.0]"},{"maxResWidth","1000"}}),magia_timeline::Changes(in,on_new))
        << "only what is not as installed is kept";
    EXPECT_TRUE(magia_timeline::Changes(in,in.values).empty());

    EXPECT_TRUE(magia_timeline::CanSkipFrames("bcs"));
    EXPECT_FALSE(magia_timeline::CanSkipFrames("mr"));
    EXPECT_TRUE(magia_timeline::CanReadText("dtd"));
    EXPECT_FALSE(magia_timeline::CanReadText("prk"));
}

TEST(magia_timeline, config_is_written_with_what_was_chosen_where_it_is_and_nothing_added) {
    magia_timeline::Values chosen{{"strategy","bcs"},{"bcs/default/dialogRect","[0.1, 0.9, 0.8, 1.0]"},
        {"framewise/sampleInterval","3"},{"extraJobs","[ocr]"},{"otl/hotelzdm/textHSVRanges","[[1, 2, 3, 4, 5, 6]]"}};
    auto out=magia_timeline::WriteConfig(MagiaConfig(),"v.mkv","o",chosen);
    ASSERT_TRUE(out);
    EXPECT_NE(std::string::npos,out->find("strategy: bcs\n"));
    EXPECT_NE(std::string::npos,out->find("bcs:\n  default:\n    dialogRect: [0.1, 0.9, 0.8, 1.0]\n    featureThreshold: 0.3\n")) << *out;
    EXPECT_NE(std::string::npos,out->find("dtd:\n  default:\n    dialogRect: [0.00, 1.00, 0.75, 1.00]\n")) << "the same key of another strategy untouched";
    EXPECT_NE(std::string::npos,out->find("  # sampleInterval: in how many frames to sample a frame\n  sampleInterval: 3\n  debug: true\n")) << *out;
    EXPECT_NE(std::string::npos,out->find("    textHSVRanges: [[1, 2, 3, 4, 5, 6]]\n    sobelThreshold: 200\n")) << "its old items gone, what follows kept: " << *out;
    EXPECT_NE(std::string::npos,out->find("extraJobs: [ocr]\nocr:\n"));
    EXPECT_NE(std::string::npos,out->find("featureThreshold: 2 # how much must change\n")) << "a line not chosen is left as it was";

    auto old=magia_timeline::WriteConfig(Magia10Config(),"v.mkv","o",magia_timeline::Values{{"maxResWidth","999"},{"extraJobs","[ocr]"}});
    ASSERT_TRUE(old);
    EXPECT_EQ(std::string::npos,old->find("maxResWidth")) << "not added to a version that has it not";
    EXPECT_NE(std::string::npos,old->find("extraJobs: [ocr]\nocr:\n")) << *old;
}

TEST(magia_timeline, text_read_by_1_0_is_put_on_the_stretches_of_its_names) {
    std::vector<magia_timeline::Interval> intervals{{0,1000,"","Subtitle_Dialog_0"},{1000,2000,"","Subtitle_Dialog_1"},{2000,3000,"あ","Subtitle_Dialog_2"}};
    magia_timeline::AddReadText(intervals,"Subtitle_Dialog_0,こんにちは@世界\r\nSubtitle_Dialog_2,ignored\nbroken line\n");
    EXPECT_EQ("こんにちは@世界",intervals[0].text);
    EXPECT_EQ("",intervals[1].text);
    EXPECT_EQ("あ",intervals[2].text) << "text already read kept";
}

TEST(magia_timeline, found_by_its_folder_its_script_or_its_program) {
    auto none=MagiaFolder("magia-none");
    EXPECT_FALSE(magia_timeline::FindLaunch(none));
    EXPECT_FALSE(magia_timeline::FindLaunch(agi::fs::path()));

    auto source=MagiaFolder("magia-source");
    WriteText(source/"MagiaTimeline.py","print()\n");
    auto bare=magia_timeline::FindLaunch(source);
    ASSERT_TRUE(bare);
    EXPECT_EQ("python3",bare->program.string()) << "no venv: the Python on the path";
    WriteText(source/"venv/bin/python3","",true);
    auto launch=magia_timeline::FindLaunch(source);
    ASSERT_TRUE(launch);
    EXPECT_EQ((source/"venv/bin/python3").string(),launch->program.string());
    EXPECT_EQ(source.string(),launch->folder.string()) << "run in its folder, where its config's paths start";
    EXPECT_EQ((std::vector<std::string>{"-u",(source/"MagiaTimeline.py").string(),"--config","/c.yml"}),magia_timeline::Arguments(*launch,"/c.yml"));
    auto by_script=magia_timeline::FindLaunch(source/"MagiaTimeline.py");
    ASSERT_TRUE(by_script);
    EXPECT_EQ(launch->program.string(),by_script->program.string());

    auto packaged=MagiaFolder("magia-packaged");
    WriteText(packaged/"MagiaTimeline.exe","",true);
    auto exe=magia_timeline::FindLaunch(packaged);
    ASSERT_TRUE(exe);
    EXPECT_EQ((packaged/"MagiaTimeline.exe").string(),exe->program.string());
    EXPECT_EQ((std::vector<std::string>{"--config","c.yml"}),magia_timeline::Arguments(*exe,"c.yml"));
}

TEST(magia_timeline, its_errors_are_read_from_what_it_prints) {
    EXPECT_FALSE(magia_timeline::ReportsError("Task 0: a -> b\nOverall Elapsed 3 s\n"));
    EXPECT_TRUE(magia_timeline::ReportsError("Exception caught:  bad config\n"));
    EXPECT_TRUE(magia_timeline::ReportsError("Traceback (most recent call last):\n  File x\n"));
    EXPECT_EQ("50%\nd\n",magia_timeline::LastLines("a\nb\r\nc\r50%\rd\n",2)) << "a progress bar redrawn with \\r: each drawing a line";
    EXPECT_EQ("download x\nException caught:  tesseract is not installed\n",
        magia_timeline::WithoutProgress(" 18%|\xe2\x96\x88   | 1714/9740 [00:00<00:01, 5515.30it/s]\r100%|\xe2\x96\x88\xe2\x96\x88| 9740/9740\ndownload x\n  0%|          | 0/2138\nException caught:  tesseract is not installed\n"))
        << "its progress bars dropped, what it said kept";
    EXPECT_EQ("50% done\n",magia_timeline::WithoutProgress("50% done\n")) << "a percentage alone is not a bar";
}

TEST(magia_timeline, how_far_it_is_is_read_from_what_it_prints) {
    std::string const start="Task 0: a.mp4 -> b\nResolution: 1920x1080\n==== Running Engine ====\n";
    EXPECT_FALSE(magia_timeline::ProgressOf(start,100000)) << "starting: no telling";
    EXPECT_FALSE(magia_timeline::ProgressOf(start+"INFO: Could not find files for the given pattern(s).\r\n",100000)) << "what something else it ran said tells nothing";

    std::string const building=start+"==== IIR Building ====\n";
    EXPECT_EQ(0.0,magia_timeline::ProgressOf(building,100000)) << "building, before its first frame";
    EXPECT_FALSE(magia_timeline::ProgressOf(building,0)) << "the video's length unknown";
    EXPECT_DOUBLE_EQ(0.25,*magia_timeline::ProgressOf(building+"frame 00:00:10.00\nframe 00:00:25.00\n",100000));
    EXPECT_DOUBLE_EQ(0.25,*magia_timeline::ProgressOf(building+"frame 00:00:25.00\r\nsomething said\r\n",100000)) << "the last frame, past what else was said";
    EXPECT_DOUBLE_EQ(0.61,*magia_timeline::ProgressOf(building+"frame 01:01:00.00\n",6000000)) << "hours and minutes";
    EXPECT_DOUBLE_EQ(1.0,*magia_timeline::ProgressOf(building+"frame 00:02:00.00\n",100000)) << "past the end it is at the end";
    EXPECT_DOUBLE_EQ(0.5,*magia_timeline::ProgressOf(start+"==== FPIR Building ====\nframe 00:00:50.00\n",100000)) << "the engine going frame by frame";

    std::string const built=building+"frame 00:01:40.00\n==== IIR Passes ====\niirPassMerge\nTimeline Elapsed 3.00 s\n";
    EXPECT_FALSE(magia_timeline::ProgressOf(built,100000)) << "past building: no telling";

    std::string const reading=built+"==== Extra Job: ocr ====\nIIRTextExtractionPass: processing 200 intervals\nStandalone output disabled. Writing to ass file.\n";
    EXPECT_EQ(0.0,magia_timeline::ProgressOf(reading,100000)) << "reading, before the first";
    EXPECT_DOUBLE_EQ(0.5,*magia_timeline::ProgressOf(reading+"Subtitle_Dialog_0\nSubtitle_Dialog_10\nSubtitle_Dialog_99\n",100000)) << "the hundredth of 200 read";
    EXPECT_DOUBLE_EQ(0.5,*magia_timeline::ProgressOf(reading+"Subtitle_Dialog_99\n",0)) << "reading needs no video length";
    EXPECT_FALSE(magia_timeline::ProgressOf(reading+"Subtitle_Dialog_199\n==== IIR to ASS ====\n",100000)) << "past reading: no telling";
}

namespace {
/// Stands in for MagiaTimeline's virtual environment's Python: checks it is
/// run as it would be, says what it was given, and writes what it writes
std::string const fake_magia=R"SH(#!/bin/sh
cfg=""
while [ $# -gt 0 ]; do if [ "$1" = "--config" ]; then cfg="$2"; shift; fi; shift; done
if [ ! -f config.yml ]; then echo "Exception caught:  not run in its own folder"; exit 0; fi
dest=$(sed -n "/^destination:/{n;s/^- \"\(.*\)\"$/\1/p}" "$cfg")
src=$(sed -n "/^source:/{n;s/^- \"\(.*\)\"$/\1/p}" "$cfg")
echo "src=$src" > seen.txt
sed -n 's/^strategy: /strategy=/p' "$cfg" >> seen.txt
cp "$cfg" used.yml
echo "Task 0: $src -> $dest"
if [ "$(cat mode.txt 2>/dev/null)" = "hang" ]; then sleep 30 & echo $! > pid.txt; wait; fi
if [ "$(cat mode.txt 2>/dev/null)" = "fail" ]; then echo "Exception caught:  the video could not be opened"; read x; exit 0; fi
t0=Subtitle_Dialog_0; t1=Subtitle_Dialog_1
if grep -q '^extraJobs: \[ocr\]' "$cfg"; then
  if grep -q '^  suffix:' "$cfg"; then printf 'Subtitle_Dialog_1,\346\226\207\345\255\227\n' > "$dest-ocr.txt"
  else t1=$(printf '\346\226\207\345\255\227'); fi
fi
cat > "$dest.ass" <<ASS
[Script Info]
ScriptType: v4.00+
PlayResX: 1280
PlayResY: 720

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Microsoft YaHei,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,100,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:03.00,0:00:05.50,Default,,0,0,0,,$t1
Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,$t0
ASS
echo "==== IIR to ASS ===="
read x
echo "Overall Elapsed 1 s"
)SH";
}

namespace {
/// Answers the dialog it is run with: does @p act in it, then runs or cancels
struct MagiaDialog : wxModalDialogHook {
    std::function<void(wxDialog*)> act;
    int answer=wxID_OK, shown=0;
    explicit MagiaDialog(std::function<void(wxDialog*)> act={}):act(std::move(act)){Register();}
    int Enter(wxDialog* dialog) override {
        if(dynamic_cast<wxMessageDialog*>(dialog) || dialog->GetTitle()!=_("Time Hardsubs with MagiaTimeline"))return wxID_NONE;
        ++shown;
        if(act)act(dialog);
        return answer;
    }
    void Exit(wxDialog*) override {}
};
template<class T> T* MagiaControl(wxWindow* parent,wxString const& name) {
    for(auto w:descendants(parent))if(w->GetName()==name)if(auto t=dynamic_cast<T*>(w))return t;
    return nullptr;
}
}

TEST_F(ordinary_edit, magiatimeline_adds_an_empty_line_for_each_subtitle_it_finds_in_one_step) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseVideo(); OPT_SET("Tool/MagiaTimeline/Path")->SetString(""); OPT_SET("Tool/MagiaTimeline/Settings")->SetString(""); };
    auto folder=MagiaFolder("magia-fake");
    WriteText(folder/"MagiaTimeline.py","");
    WriteText(folder/"config.yml",MagiaConfig());
    WriteText(folder/"venv/bin/python3",fake_magia,true);
    OPT_SET("Tool/MagiaTimeline/Path")->SetString(folder.string());
    OPT_SET("Tool/MagiaTimeline/Settings")->SetString("strategy=bcs\n");
    ASSERT_TRUE(cmd::get("tool/magiatimeline")->Validate(c.get()));
    MagiaDialog run;

    size_t const before=LiveRead(*c).Events.size();
    std::string const style=LiveRead(*c).Events.front().Style;
    auto description=c->subsController->GetUndoDescription();
    cmd::call("tool/magiatimeline",c.get());
    auto seen=ReadText(folder/"seen.txt");
    EXPECT_NE(std::string::npos,seen.find("src="+c->project->VideoName().string())) << seen;
    EXPECT_NE(std::string::npos,seen.find("strategy=bcs")) << seen;
    auto const& events=LiveRead(*c).Events;
    ASSERT_EQ(before+2,events.size());
    auto it=events.begin();std::advance(it,before);
    EXPECT_EQ(1000,int(it->Start));EXPECT_EQ(2000,int(it->End)) << "in order of time";
    EXPECT_TRUE(it->Text.get().empty()) << "its names for the lines are not text";
    EXPECT_EQ(style,it->Style.get());
    ++it;
    EXPECT_EQ(3000,int(it->Start));EXPECT_EQ(5500,int(it->End));
    EXPECT_EQ(2u,c->selectionController->SelectedIDs().size()) << "the new lines selected";
    c->subsController->Undo();
    EXPECT_EQ(before,LiveRead(*c).Events.size()) << "one step";
    EXPECT_EQ(description,c->subsController->GetUndoDescription());

    // It says it failed: nothing added, and what it said is shown
    WriteText(folder/"mode.txt","fail");
    RecordMessages messages;
    cmd::call("tool/magiatimeline",c.get());
    EXPECT_EQ(before,LiveRead(*c).Events.size());
    ASSERT_EQ(1u,messages.messages.size());
    EXPECT_NE(wxString::npos,messages.messages[0].find("the video could not be opened")) << messages.messages[0];
}

TEST_F(ordinary_edit, magiatimeline_cancelled_stops_it_and_what_it_started_and_adds_nothing) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseVideo(); OPT_SET("Tool/MagiaTimeline/Path")->SetString(""); };
    MagiaDialog run;
    auto folder=MagiaFolder("magia-hang");
    WriteText(folder/"MagiaTimeline.py","");
    WriteText(folder/"config.yml",MagiaConfig());
    WriteText(folder/"venv/bin/python3",fake_magia,true);
    WriteText(folder/"mode.txt","hang");
    OPT_SET("Tool/MagiaTimeline/Path")->SetString(folder.string());
    // Cancel once what it started is running
    struct Cancel : wxModalDialogHook {
        agi::fs::path pid_file;
        explicit Cancel(agi::fs::path p):pid_file(std::move(p)){Register();}
        int Enter(wxDialog* dialog) override {
            if(dialog->GetTitle()!="MagiaTimeline")return wxID_NONE;
            auto press=std::make_shared<std::function<void()>>();
            *press=[this,dialog,press]{
                if(!std::filesystem::exists(std::filesystem::path(pid_file.wstring()))){dialog->CallAfter(*press);wxMilliSleep(10);return;}
                wxCommandEvent e(wxEVT_BUTTON,wxID_CANCEL);e.SetEventObject(dialog);dialog->GetEventHandler()->ProcessEvent(e);
            };
            dialog->CallAfter(*press);
            return wxID_NONE;
        }
        void Exit(wxDialog*) override {}
    } cancel(folder/"pid.txt");
    size_t const before=LiveRead(*c).Events.size();
    auto started=std::chrono::steady_clock::now();
    cmd::call("tool/magiatimeline",c.get());
    EXPECT_LT(std::chrono::steady_clock::now()-started,std::chrono::seconds(10)) << "not waited out";
    EXPECT_EQ(before,LiveRead(*c).Events.size());
    int const pid=std::stoi(ReadText(folder/"pid.txt"));
    bool gone=false;
    for(int i=0;i<50&&!gone;++i){gone=::kill(pid,0)!=0;if(!gone)wxMilliSleep(50);}
    EXPECT_TRUE(gone) << "what it started is killed with it";
}

TEST_F(ordinary_edit, magiatimeline_is_run_as_chosen_in_its_dialog_and_the_choice_kept) {
    c->project->LoadVideo("?dummy:25:250:64:48:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    BOOST_SCOPE_EXIT_ALL(&){ c->project->CloseVideo(); OPT_SET("Tool/MagiaTimeline/Path")->SetString(""); OPT_SET("Tool/MagiaTimeline/Settings")->SetString(""); };
    auto folder=MagiaFolder("magia-dialog");
    WriteText(folder/"MagiaTimeline.py","");
    WriteText(folder/"config.yml",MagiaConfig());
    WriteText(folder/"venv/bin/python3",fake_magia,true);
    OPT_SET("Tool/MagiaTimeline/Path")->SetString(folder.string());
    OPT_SET("Tool/MagiaTimeline/Settings")->SetString("");
    size_t const before=LiveRead(*c).Events.size();

    {
        MagiaDialog run([&](wxDialog* d) {
            auto strategy=MagiaControl<FlatChoice>(d,"strategy");
            auto preset=MagiaControl<FlatChoice>(d,"preset");
            auto engine=MagiaControl<FlatSwitch>(d,"engine");
            auto ocr=MagiaControl<FlatCheck>(d,"ocr");
            ASSERT_TRUE(strategy&&preset&&engine&&ocr);
            ASSERT_EQ(4u,strategy->GetItems().size()) << "the strategies its config has";
            EXPECT_EQ(3,strategy->GetSelection()) << "its own, dtd";
            EXPECT_NE(nullptr,MagiaControl<FlatFrame>(d,"maxResWidth")) << "1.1 has a size limit";
            EXPECT_NE(nullptr,MagiaControl<FlatSwitch>(d,"ocrEngine"));
            EXPECT_FALSE(ocr->GetValue());
            EXPECT_TRUE(ocr->IsEnabled());

            strategy->Choose(0); // mr: every frame, no OCR
            EXPECT_EQ(2u,preset->GetItems().size());
            EXPECT_EQ(1,engine->GetSelection()) << "a strategy for one game looks at every frame";
            EXPECT_FALSE(engine->IsItemEnabled(0));
            EXPECT_FALSE(ocr->IsEnabled());
            preset->Choose(1);

            strategy->Choose(2); // bcs
            EXPECT_TRUE(engine->IsItemEnabled(0));
            EXPECT_FALSE(ocr->IsEnabled()) << "OCR only when frames are skipped";
            engine->Choose(0);
            EXPECT_TRUE(ocr->IsEnabled());
            ocr->Toggle();
            EXPECT_TRUE(ocr->GetValue());
            auto language=MagiaControl<FlatFrame>(d,"ocrLanguage");
            ASSERT_NE(nullptr,language);
            EXPECT_FALSE(language->IsShown()) << "PaddleOCR's language is not set in 1.1";

            // The subtitles' rectangle dragged on the frame
            auto picker=MagiaControl<MagiaRectPicker>(d,"picker");
            auto region=MagiaControl<FlatChoice>(d,"region");
            ASSERT_TRUE(picker&&region);
            ASSERT_EQ(1u,region->GetItems().size()) << "bcs's default has dialogRect alone; 1.1's config here has no contentRect";
            EXPECT_EQ(wxGETTEXT_IN_CONTEXT("region","Subtitles"),region->GetStringSelection());
            EXPECT_TRUE(picker->IsEditing());
            auto area=picker->ImageArea();
            auto at=[&](double x,double y){return wxPoint(area.x+int(std::lround(x*area.width)),area.y+int(std::lround(y*area.height)));};
            picker->Press(at(0.1,0.5));
            picker->Drag(at(0.5,0.7));
            picker->Release(at(0.9,0.9));
            auto r=picker->GetRect();
            EXPECT_NEAR(0.1,r.left,0.01);EXPECT_NEAR(0.9,r.right,0.01);EXPECT_NEAR(0.5,r.top,0.01);EXPECT_NEAR(0.9,r.bottom,0.01);
            // Its top side dragged down
            picker->Press(at(0.5,0.5));
            picker->Release(at(0.5,0.6));
            EXPECT_NEAR(0.6,picker->GetRect().top,0.01);
            EXPECT_NEAR(0.9,picker->GetRect().bottom,0.01) << "the other sides stay";
            // Moved from inside, kept in the frame
            picker->Press(at(0.5,0.75));
            picker->Release(at(0.5,0.95));
            EXPECT_NEAR(0.7,picker->GetRect().top,0.01);
            EXPECT_NEAR(1.0,picker->GetRect().bottom,0.01);

            // The sensitivity bcs has, scrubbed
            auto threshold=MagiaControl<FlatFrame>(d,"featureThreshold");
            ASSERT_NE(nullptr,threshold);
            EXPECT_TRUE(threshold->IsShown());
            threshold->Scrub(2,true);
        });
        cmd::call("tool/magiatimeline",c.get());
        EXPECT_EQ(1,run.shown);
    }
    auto used=ReadText(folder/"used.yml");
    EXPECT_NE(std::string::npos,used.find("strategy: bcs\n")) << used;
    EXPECT_NE(std::string::npos,used.find("preset: default\n")) << "mr's narrow is not bcs's: " << used;
    EXPECT_NE(std::string::npos,used.find("engine: speculative\n"));
    EXPECT_NE(std::string::npos,used.find("extraJobs: [ocr]\n"));
    EXPECT_NE(std::string::npos,used.find("  standaloneOutput: false\n")) << "its text put in the lines it writes";
    auto bcs_rect=[](std::string const& yml){
        auto at=yml.find("bcs:\n  default:\n    dialogRect: ");
        if(at==std::string::npos)return std::optional<magia_timeline::Rect>();
        at+=std::string("bcs:\n  default:\n    dialogRect: ").size();
        return magia_timeline::ParseRect(yml.substr(at,yml.find('\n',at)-at));
    };
    auto rect=bcs_rect(used);
    ASSERT_TRUE(rect) << used;
    EXPECT_NEAR(0.1,rect->left,0.01);EXPECT_NEAR(0.9,rect->right,0.01);EXPECT_NEAR(0.7,rect->top,0.01);EXPECT_NEAR(1.0,rect->bottom,0.01);
    EXPECT_NE(std::string::npos,used.find("]\n    featureThreshold: 0.5\n")) << used;
    EXPECT_NE(std::string::npos,used.find("dtd:\n  default:\n    dialogRect: [0.00, 1.00, 0.75, 1.00]\n")) << "another strategy's untouched";
    auto const& events=LiveRead(*c).Events;
    ASSERT_EQ(before+2,events.size());
    auto it=events.begin();std::advance(it,before);
    EXPECT_EQ("",it->Text.get()) << "a name is not text";
    ++it;
    EXPECT_EQ("文字",it->Text.get()) << "the text it read";
    auto kept=magia_timeline::ParseSaved(OPT_GET("Tool/MagiaTimeline/Settings")->GetString());
    EXPECT_EQ("bcs",kept["strategy"]);
    EXPECT_EQ("[ocr]",kept["extraJobs"]);
    EXPECT_EQ(0u,kept.count("engine")) << "as its own: not kept";

    // Next time it starts from the choice; its own settings brought back,
    // and cancelled: not run, nothing kept
    c->subsController->Undo();
    {
        MagiaDialog run([&](wxDialog* d) {
            auto strategy=MagiaControl<FlatChoice>(d,"strategy");
            auto ocr=MagiaControl<FlatCheck>(d,"ocr");
            ASSERT_TRUE(strategy&&ocr);
            EXPECT_EQ(2,strategy->GetSelection());
            EXPECT_TRUE(ocr->GetValue());
            auto restore=MagiaControl<FlatButton>(d,"restore");
            ASSERT_NE(nullptr,restore);
            restore->Press();
            EXPECT_EQ(3,strategy->GetSelection());
            EXPECT_FALSE(ocr->GetValue());
        });
        run.answer=wxID_CANCEL;
        std::filesystem::remove(std::filesystem::path((folder/"used.yml").wstring()));
        cmd::call("tool/magiatimeline",c.get());
        EXPECT_EQ(1,run.shown);
    }
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path((folder/"used.yml").wstring()))) << "not run";
    EXPECT_EQ(before,LiveRead(*c).Events.size());
    EXPECT_EQ("bcs",magia_timeline::ParseSaved(OPT_GET("Tool/MagiaTimeline/Settings")->GetString())["strategy"]);

    // 1.0: what it has not is not offered; its OCR read from its own file
    WriteText(folder/"config.yml",Magia10Config());
    OPT_SET("Tool/MagiaTimeline/Settings")->SetString("strategy=dtd\nmaxResWidth=1000\n");
    {
        MagiaDialog run([&](wxDialog* d) {
            auto strategy=MagiaControl<FlatChoice>(d,"strategy");
            ASSERT_NE(nullptr,strategy);
            EXPECT_EQ(2u,strategy->GetItems().size());
            EXPECT_EQ(0,strategy->GetSelection()) << "dtd it has not: its own bcs";
            EXPECT_EQ(nullptr,MagiaControl<FlatFrame>(d,"maxResWidth"));
            // Its black bars cut away first: the subtitles' rectangle is in
            // shares of what is left
            auto picker=MagiaControl<MagiaRectPicker>(d,"picker");
            auto region=MagiaControl<FlatChoice>(d,"region");
            ASSERT_TRUE(picker&&region);
            ASSERT_EQ(2u,region->GetItems().size());
            region->Choose(1);
            EXPECT_EQ(_("Picture (without black bars)"),region->GetStringSelection());
            auto area=picker->ImageArea();
            auto at=[&](double x,double y){return wxPoint(area.x+int(std::lround(x*area.width)),area.y+int(std::lround(y*area.height)));};
            picker->Press(at(0.5,0.0));
            picker->Release(at(0.5,0.1));
            picker->Press(at(0.5,1.0));
            picker->Release(at(0.5,0.9));
            region->Choose(0);
            picker->Press(at(0.02,0.5));
            picker->Release(at(0.98,0.9));
            auto r=picker->GetRect();
            EXPECT_NEAR(0.5,r.top,0.01) << "half way down what is left";
            EXPECT_NEAR(1.0,r.bottom,0.01);
            // A strategy without them: none of the sensitivity shown
            auto threshold=MagiaControl<FlatFrame>(d,"featureThreshold");
            ASSERT_NE(nullptr,threshold);
            EXPECT_FALSE(threshold->IsShown()) << "1.0's bcs here has no featureThreshold";
            auto engine=MagiaControl<FlatSwitch>(d,"ocrEngine");
            ASSERT_NE(nullptr,engine) << "1.0 can use either";
            MagiaControl<FlatCheck>(d,"ocr")->Toggle();
            EXPECT_EQ(0,engine->GetSelection()) << "both on in 1.0: PaddleOCR shown";
            auto language=MagiaControl<FlatFrame>(d,"ocrLanguage");
            ASSERT_NE(nullptr,language);
            EXPECT_TRUE(language->IsEnabled());
            engine->Choose(1);
            auto text=dynamic_cast<wxTextCtrl*>(language->GetContent());
            ASSERT_NE(nullptr,text);
            EXPECT_EQ("jpn",text->GetValue());
            text->SetValue("chi_sim");
        });
        cmd::call("tool/magiatimeline",c.get());
    }
    used=ReadText(folder/"used.yml");
    EXPECT_EQ(std::string::npos,used.find("maxResWidth"));
    EXPECT_NE(std::string::npos,used.find("contentRect: [0.0, 1.0, 0.1, 0.9]\n")) << used;
    rect=bcs_rect(used);
    ASSERT_TRUE(rect) << used;
    EXPECT_NEAR(0.02,rect->left,0.01);EXPECT_NEAR(0.98,rect->right,0.01);EXPECT_NEAR(0.5,rect->top,0.01);EXPECT_NEAR(1.0,rect->bottom,0.01);
    EXPECT_NE(std::string::npos,used.find("extraJobs: [ocr]\n")) << used;
    EXPECT_NE(std::string::npos,used.find("  doPaddle: false\n  paddleLang: \"japan\"\n  doTesseract: true\n  tesseractLang: \"chi_sim\"\n")) << used;
    auto const& again=LiveRead(*c).Events;
    ASSERT_EQ(before+2,again.size());
    EXPECT_EQ("文字",again.back().Text.get()) << "read from 1.0's own file, by the name";
    EXPECT_EQ("",std::next(again.begin(),before)->Text.get());

    // OCR turned on and the engine left as shown: 1.0 has both on, which
    // is shown as PaddleOCR and run so, Tesseract off
    c->subsController->Undo();
    OPT_SET("Tool/MagiaTimeline/Settings")->SetString("");
    {
        MagiaDialog run([&](wxDialog* d) {
            auto engine=MagiaControl<FlatSwitch>(d,"ocrEngine");
            ASSERT_NE(nullptr,engine);
            MagiaControl<FlatCheck>(d,"ocr")->Toggle();
            EXPECT_EQ(0,engine->GetSelection());
        });
        cmd::call("tool/magiatimeline",c.get());
    }
    used=ReadText(folder/"used.yml");
    EXPECT_NE(std::string::npos,used.find("  doPaddle: true\n  paddleLang: \"japan\"\n  doTesseract: false\n")) << used;
}

TEST(script_text, the_thumb_shows_how_much_is_on_the_screen_and_where) {
    EXPECT_FALSE(script_text::ThumbFor(0,100,100,200,20)) << "it all fits";
    EXPECT_FALSE(script_text::ThumbFor(0,0,0,200,20));
    auto top=script_text::ThumbFor(0,100,400,200,20);
    ASSERT_TRUE(top);
    EXPECT_EQ(0,top->top);
    EXPECT_EQ(50,top->height) << "a quarter on the screen, a quarter of the track";
    auto bottom=script_text::ThumbFor(300,100,400,200,20);
    EXPECT_EQ(150,bottom->top) << "at the end, at the bottom";
    EXPECT_EQ(150,script_text::ThumbFor(9999,100,400,200,20)->top);
    EXPECT_EQ(20,script_text::ThumbFor(0,10,100000,200,20)->height) << "never shorter than the least";
    for (int pos : {0,37,150,300})
        EXPECT_NEAR(pos,script_text::PosForThumb(script_text::ThumbFor(pos,100,400,200,20)->top,100,400,200,20),2) << "a pixel of the thumb is 2 of the text";
    EXPECT_EQ(300,script_text::PosForThumb(500,100,400,200,20)) << "dragged past the end";
    EXPECT_EQ(0,script_text::PosForThumb(-30,100,400,200,20));
}

TEST_F(ordinary_edit, script_panel_draws_its_own_thin_scroll_bar_on_the_field) {
    OPT_SET("App/Style Panel/Show")->SetBool(true);
    OPT_SET("App/Script Panel/Show")->SetBool(true);
    auto frame=new wxFrame(nullptr,-1,"script bar");
    BOOST_SCOPE_EXIT_ALL(&){ delete frame; OPT_SET("App/Script Panel/Show")->SetBool(false); };
    auto script=new ScriptPanel(frame,c.get());
    frame->SetClientSize(script->FromDIP(300),script->FromDIP(500));frame->Show();
    script->SetSize(frame->GetClientSize());script->Layout();
    for(int i=0;i<10;++i) wxYield();
    script->TextBox()->ChangeValue("short");
    for(int i=0;i<10;++i) wxYield();
    EXPECT_FALSE(script->ScrollThumb()) << "the text fits: no bar";
    wxString t; for(int i=0;i<200;++i) t+=wxString::Format("line %d of the script\n",i);
    script->TextBox()->ChangeValue(t);
    for(int i=0;i<20;++i){ wxYield(); wxMilliSleep(5); }
    auto [pos,page,range]=script->ScrollState();
#ifdef __WXGTK__
    ASSERT_GT(range,page) << "longer than the box";
    auto thumb=script->ScrollThumb();
    ASSERT_TRUE(thumb);
    wxRect box=script->TextBox()->GetScreenRect();
    box.SetPosition(script->ScreenToClient(box.GetPosition()));
    EXPECT_GT(thumb->x,box.GetRight()) << "right of the text, not over it";
    EXPECT_LT(thumb->GetRight(),script->GetClientSize().x);
    EXPECT_LE(thumb->width,script->FromDIP(6)) << "thin";
    EXPECT_LT(thumb->height,box.height);
    // Scrolled, by the thumb or the text's own keys, the thumb follows
    int top=thumb->y;
    EXPECT_EQ(0,pos);
    script->ScrollTextTo(range);
    EXPECT_EQ(range-page,std::get<0>(script->ScrollState()));
    EXPECT_GT(script->ScrollThumb()->y,top);
    EXPECT_EQ(box.GetBottom(),script->ScrollThumb()->GetBottom()) << "at the end, at the bottom";
    script->ScrollTextTo(-50);
    EXPECT_EQ(0,std::get<0>(script->ScrollState()));
    EXPECT_EQ(box.y,script->ScrollThumb()->y) << "at the start, at the top";
#else
    (void)pos;(void)page;(void)range;
#endif
}

TEST_F(ordinary_edit, keyboard_regions_go_round_with_f6_and_keep_tab_inside) {
    auto frame=new wxFrame(nullptr,-1,"regions");
    BOOST_SCOPE_EXIT_ALL(&){ regions::Clear(); delete frame; };
    auto backdrop=new wxPanel(frame);
    // One control on its own (as the grid is), a panel of two, one hidden,
    // and one with a toolbar
    auto grid=new wxTextCtrl(backdrop,-1,"grid");
    auto edit=new wxPanel(backdrop);
    auto a=new wxTextCtrl(edit,-1,"a"), b=new wxTextCtrl(edit,-1,"b");
    { auto s=new wxBoxSizer(wxVERTICAL); s->Add(a); s->Add(b); edit->SetSizer(s); }
    auto hidden=new wxPanel(backdrop);
    new wxTextCtrl(hidden,-1,"h");
    auto tools=new wxPanel(backdrop);
    auto bar=new wxToolBar(tools,-1);
    bar->AddTool(1,"t",wxBitmap(16,16));
    bar->Realize();
    auto button=new wxButton(tools,-1,"ok");
    { auto s=new wxBoxSizer(wxVERTICAL); s->Add(bar); s->Add(button); tools->SetSizer(s); }
    auto sizer=new wxBoxSizer(wxHORIZONTAL);
    for (wxWindow *w : {static_cast<wxWindow*>(grid),static_cast<wxWindow*>(edit),static_cast<wxWindow*>(hidden),static_cast<wxWindow*>(tools)})
        sizer->Add(w,0,wxALL,10);
    backdrop->SetSizer(sizer);
    hidden->Hide();
    regions::Setup(backdrop,{{grid},{edit},{hidden},{tools}});
    frame->SetSize(700,300);frame->Show();backdrop->SetSize(frame->GetClientSize());backdrop->Layout();
    for(int i=0;i<10;++i) wxYield();

    EXPECT_EQ(0,regions::RegionOf(grid));
    EXPECT_EQ(1,regions::RegionOf(b));
    EXPECT_EQ(3,regions::RegionOf(bar));
    EXPECT_EQ(-1,regions::RegionOf(backdrop));
    EXPECT_EQ((std::vector<wxWindow*>{a,b}),regions::Stops(1));
    EXPECT_EQ((std::vector<wxWindow*>{button}),regions::Stops(3)) << "a toolbar's buttons have no keys";
    EXPECT_EQ((std::vector<wxWindow*>{grid}),regions::Stops(0));
    EXPECT_EQ(a,regions::TabTarget(b,true)) << "round to the first, not on to the next region";
    EXPECT_EQ(b,regions::TabTarget(a,false));
    EXPECT_EQ(grid,regions::TabTarget(grid,true));

    a->SetFocus();
    for(int i=0;i<5;++i) wxYield();
    ASSERT_EQ(a,wxWindow::FindFocus());
    EXPECT_EQ(1,regions::Current());
    EXPECT_EQ(nullptr,regions::Outlined()) << "put there, not moved by keys: no line";

    // F6 on, past the hidden one, and round
    EXPECT_TRUE(regions::Next(true));
    EXPECT_EQ(button,wxWindow::FindFocus());
    EXPECT_EQ(tools,regions::Outlined());
    EXPECT_TRUE(regions::Next(true));
    EXPECT_EQ(grid,wxWindow::FindFocus());
    EXPECT_TRUE(regions::Next(false));
    EXPECT_EQ(button,wxWindow::FindFocus());
    // The command F6 is bound to
    cmd::call("app/region/previous",c.get());
    EXPECT_EQ(a,wxWindow::FindFocus());
    cmd::call("app/region/next",c.get());
    EXPECT_EQ(button,wxWindow::FindFocus());

    // The line is drawn in the gap round the block
    regions::Next(false);
    ASSERT_EQ(edit,regions::Outlined());
    {
        wxSize size=backdrop->GetClientSize();
        wxBitmap bitmap(size);
        wxMemoryDC dc(bitmap);
        dc.SetBackground(*wxWHITE_BRUSH);dc.Clear();
        regions::DrawOutline(backdrop,dc);
        dc.SelectObject(wxNullBitmap);
        auto image=bitmap.ConvertToImage();
        wxRect r=edit->GetRect();
        int y=r.y+r.height/2;
        bool line=false;
        for(int x=std::max(0,r.x-8);x<r.x;++x)
            line|=image.GetRed(x,y)<200;
        EXPECT_TRUE(line) << "left of the block";
        EXPECT_EQ(255,image.GetRed(r.x+r.width/2,y)) << "nothing over the block";
    }

    // Tab and Shift+Tab in a window that takes every key
    wxKeyEvent tab(wxEVT_CHAR_HOOK);
    tab.m_keyCode=WXK_TAB;
    EXPECT_TRUE(regions::HandleKey(a,tab));
    EXPECT_EQ(b,wxWindow::FindFocus());
    EXPECT_TRUE(regions::HandleKey(b,tab));
    EXPECT_EQ(a,wxWindow::FindFocus());
    tab.SetShiftDown(true);
    EXPECT_TRUE(regions::HandleKey(a,tab));
    EXPECT_EQ(b,wxWindow::FindFocus());
    wxKeyEvent other(wxEVT_CHAR_HOOK);
    other.m_keyCode='X';
    EXPECT_FALSE(regions::HandleKey(b,other));

    // A click hides the line
    EXPECT_EQ(edit,regions::Outlined());
    wxMouseEvent click(wxEVT_LEFT_DOWN);
    click.SetEventObject(b);
    b->ProcessWindowEvent(click);
    EXPECT_EQ(nullptr,regions::Outlined());

    // A toolbar's click leaves the keyboard where it was, or gives it to
    // the window it is set to give it to
    auto click_tool=[&]{
        wxCommandEvent event(wxEVT_TOOL,1);
        event.SetEventObject(bar);
        bar->ProcessWindowEvent(event);
        for(int i=0;i<5;++i) wxYield();
    };
    click_tool();
    EXPECT_EQ(b,wxWindow::FindFocus());
    regions::FocusAfterClicks(bar,grid);
    click_tool();
    EXPECT_EQ(grid,wxWindow::FindFocus());
}

TEST_F(ordinary_edit, region_line_runs_through_the_strip_under_a_block_resized_from_below) {
    // The audio is such a block: its strip is the gap to the edit box, so
    // the backdrop can't draw there and the block draws its piece itself
    auto frame=new wxFrame(nullptr,-1,"regions");
    BOOST_SCOPE_EXIT_ALL(&){ regions::Clear(); delete frame; };
    auto backdrop=new wxPanel(frame);
    auto sash=new wxSashWindow(backdrop,-1,wxDefaultPosition,wxSize(300,100));
    sash->SetSashVisible(wxSASH_BOTTOM,true);
    sash->SetDefaultBorderSize(8);
    auto inner=new wxPanel(sash);
    new wxTextCtrl(inner,-1,"in");
    auto other=new wxTextCtrl(backdrop,-1,"other");
    auto sizer=new wxBoxSizer(wxVERTICAL);
    sizer->Add(sash,0,wxLEFT|wxRIGHT|wxTOP,10);
    sizer->Add(other,0,wxALL,10);
    backdrop->SetSizer(sizer);
    regions::Setup(backdrop,{{sash},{other}});
    frame->SetSize(400,300);frame->Show();backdrop->SetSize(frame->GetClientSize());backdrop->Layout();
    for(int i=0;i<10;++i) wxYield();
    other->SetFocus();
    for(int i=0;i<5;++i) wxYield();
    ASSERT_TRUE(regions::Next(true));
    ASSERT_EQ(sash,regions::Outlined());

    auto render=[](wxWindow *window){
        wxBitmap bitmap(window->GetClientSize());
        wxMemoryDC dc(bitmap);
        dc.SetBackground(*wxWHITE_BRUSH);dc.Clear();
        regions::DrawOutline(window,dc);
        dc.SelectObject(wxNullBitmap);
        return bitmap.ConvertToImage();
    };
    wxSize size=sash->GetClientSize();
    auto in_sash=render(sash);
    bool line=false;
    for(int y=size.y-8;y<size.y;++y)
        line|=in_sash.GetRed(size.x/2,y)<200;
    EXPECT_TRUE(line) << "the bottom of the line, in the strip";
    EXPECT_EQ(255,in_sash.GetRed(size.x/2,(size.y-8)/2)) << "nothing over the block";

    auto on_backdrop=render(backdrop);
    wxRect r=sash->GetRect();
    bool below=false;
    for(int y=r.GetBottom()+1;y<r.GetBottom()+10;++y)
        below|=on_backdrop.GetRed(r.x+r.width/2,y)<200;
    EXPECT_FALSE(below) << "not under the strip, in the next block";
}

TEST_F(selection_ui, ctrl_moves_the_keyboards_row_and_space_picks_rows_here_and_there) {
    auto key=[&](wxEventType type,int code,bool ctrl,bool shift=false){
        wxKeyEvent event(type);event.m_keyCode=code;event.SetControlDown(ctrl);event.SetShiftDown(shift);
        event.SetEventObject(grid);grid->GetEventHandler()->ProcessEvent(event);
    };
    auto selected=[&]{ return c->selectionController->SelectedIDs(); };
    EXPECT_EQ(0,grid->CursorRow()) << "on the active line";

    // Ctrl and the arrows: the keyboard's row alone
    key(wxEVT_KEY_DOWN,WXK_DOWN,true);key(wxEVT_KEY_DOWN,WXK_DOWN,true);
    EXPECT_EQ(2,grid->CursorRow());
    EXPECT_EQ(ids[0],c->selectionController->ActiveID());
    EXPECT_EQ((std::set<int>{ids[0]}),selected());

    // Ctrl+Space, through the grid's own hotkey (not the window's "focus
    // the seek bar"): in the selection, as the active line
    key(wxEVT_CHAR_HOOK,WXK_SPACE,true);
    EXPECT_EQ((std::set<int>{ids[0],ids[2]}),selected());
    EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ(2,grid->CursorRow());

    // Taken out again: the active line goes to the nearest selected row,
    // the keyboard stays
    key(wxEVT_KEY_DOWN,WXK_DOWN,true);
    cmd::call("grid/select/toggle",c.get());
    EXPECT_EQ((std::set<int>{ids[0],ids[2],ids[3]}),selected());
    cmd::call("grid/select/toggle",c.get());
    EXPECT_EQ((std::set<int>{ids[0],ids[2]}),selected());
    EXPECT_EQ(ids[2],c->selectionController->ActiveID());
    EXPECT_EQ(3,grid->CursorRow());
    key(wxEVT_KEY_DOWN,WXK_UP,true);
    cmd::call("grid/select/toggle",c.get());
    EXPECT_EQ((std::set<int>{ids[0]}),selected());
    EXPECT_EQ(ids[0],c->selectionController->ActiveID()) << "the only one left";
    EXPECT_EQ(2,grid->CursorRow());
    key(wxEVT_KEY_DOWN,WXK_HOME,true);
    cmd::call("grid/select/toggle",c.get());
    EXPECT_EQ((std::set<int>{ids[0]}),selected()) << "the last selected row stays";

    // A plain arrow goes on from the keyboard's row
    key(wxEVT_KEY_DOWN,WXK_DOWN,true);key(wxEVT_KEY_DOWN,WXK_DOWN,true);
    key(wxEVT_KEY_DOWN,WXK_DOWN,false);
    EXPECT_EQ((std::set<int>{ids[3]}),selected());
    EXPECT_EQ(ids[3],c->selectionController->ActiveID());

    // Shift+Space: from the row the selection extends from to the keyboard's
    key(wxEVT_KEY_DOWN,WXK_UP,true);key(wxEVT_KEY_DOWN,WXK_UP,true);
    key(wxEVT_CHAR_HOOK,WXK_SPACE,false,true);
    EXPECT_EQ((std::set<int>{ids[1],ids[2],ids[3]}),selected());
    EXPECT_EQ(ids[1],c->selectionController->ActiveID());
    // Ctrl+Shift+Space adds to it
    c->selectionController->SetSelectionAndActiveIDs({ids[3]},ids[3],false);
    key(wxEVT_KEY_DOWN,WXK_UP,true);key(wxEVT_KEY_DOWN,WXK_UP,true);key(wxEVT_KEY_DOWN,WXK_UP,true);
    cmd::call("grid/select/toggle",c.get());
    EXPECT_EQ((std::set<int>{ids[0],ids[3]}),selected());
    key(wxEVT_KEY_DOWN,WXK_DOWN,true);
    key(wxEVT_CHAR_HOOK,WXK_SPACE,true,true);
    EXPECT_EQ((std::set<int>{ids[0],ids[1],ids[3]}),selected()) << "from row 0, where it was extended from, added";

    // A click puts the keyboard on the clicked row
    Mouse(2);
    EXPECT_EQ(2,grid->CursorRow());
}

TEST_F(ordinary_edit, video_and_audio_have_arrow_keys_and_space_of_their_own) {
    // The command each key runs in each region, as a key press finds it
    auto runs=[&](const char *context,int code,int modifiers){ return std::string(hotkey::inst->Scan(context,hotkey::keypress_to_str(code,modifiers),false)); };
    EXPECT_EQ("grid/select/toggle",runs("Subtitle Grid",WXK_SPACE,wxMOD_CMD));
    EXPECT_EQ("grid/select/range",runs("Subtitle Grid",WXK_SPACE,wxMOD_SHIFT));
    EXPECT_EQ("grid/select/range/add",runs("Subtitle Grid",WXK_SPACE,wxMOD_CMD|wxMOD_SHIFT));
    EXPECT_EQ("video/play",runs("Subtitle Grid",WXK_SPACE,0)) << "Space still plays the video in the grid";
    EXPECT_EQ("video/focus_seek",runs("Subtitle Edit Box",WXK_SPACE,wxMOD_CMD)) << "Ctrl+Space elsewhere as it was";
    EXPECT_EQ("video/line/prev",runs("Video",WXK_UP,0));
    EXPECT_EQ("video/line/next",runs("Video",WXK_DOWN,0));
    EXPECT_EQ("video/jump/start",runs("Video",WXK_HOME,0));
    EXPECT_EQ("video/jump/end",runs("Video",WXK_END,0));
    EXPECT_EQ("video/frame/next",runs("Video",WXK_RIGHT,0)) << "the arrows it had stay";
    EXPECT_EQ("time/start/decrease",runs("Audio",WXK_LEFT,wxMOD_SHIFT));
    EXPECT_EQ("time/start/increase",runs("Audio",WXK_RIGHT,wxMOD_SHIFT));
    EXPECT_EQ("time/length/decrease",runs("Audio",WXK_LEFT,wxMOD_CMD));
    EXPECT_EQ("time/length/increase",runs("Audio",WXK_RIGHT,wxMOD_CMD));
    EXPECT_EQ("grid/line/prev",runs("Audio",WXK_UP,0));
    EXPECT_EQ("grid/line/next",runs("Audio",WXK_DOWN,0));
    EXPECT_EQ("audio/play/toggle",runs("Audio",WXK_SPACE,0));
    EXPECT_EQ("time/next",runs("Audio",WXK_RIGHT,0));

    // Up and Down in the video: the next line, the video at its start
    c->project->LoadVideo("?dummy:25:250:640:480:64:64:64:");
    ASSERT_NE(nullptr,c->project->VideoProvider());
    PumpFirstFrame(c.get(),window);
    int first=0,second=0;
    {
        auto write=c->subtitleAccess->TryWrite();
        auto& row=LiveWriteForTest(*c).Events.front();row.Start=1000;row.End=2000;first=row.Id;
        auto next=new AssDialogue;next->Start=4000;next->End=5000;
        LiveWriteForTest(*c).Events.push_back(*next);second=next->Id;
        LiveWriteForTest(*c).Commit("two lines",AssFile::COMMIT_DIAG_ADDREM|AssFile::COMMIT_DIAG_FULL);
    }
    c->selectionController->SetSelectionAndActiveIDs({first},first,false);
    cmd::call("video/line/next",c.get());
    EXPECT_EQ(second,c->selectionController->ActiveID());
    EXPECT_EQ(c->videoController->FrameAtTime(4000,agi::vfr::START),c->videoController->GetFrameN());
    cmd::call("video/line/prev",c.get());
    EXPECT_EQ(first,c->selectionController->ActiveID());
    EXPECT_EQ(c->videoController->FrameAtTime(1000,agi::vfr::START),c->videoController->GetFrameN());
}
