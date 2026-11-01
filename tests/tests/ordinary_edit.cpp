// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <gtest/gtest.h>
#include "include/aegisub/context.h"
#include "ass_file.h"
#include "ass_dialogue.h"
#include "ass_attachment.h"
#include "ass_info.h"
#include "ass_style.h"
#include "subtitle_project_access.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "timeedit_ctrl.h"
#include "base_grid.h"
#include "options.h"
#include "command/command.h"
#include "command/edit.h"
#include "subtitle_binding.h"
#include "search_replace_engine.h"
#include "text_selection_controller.h"
#include "dialog_style_editor.h"
#include "dialogs.h"
#include "dialog_manager.h"
#include "compat.h"
#include "utils.h"
#include "audio_controller.h"
#include "audio_timing.h"
#include "audio_display.h"
#include "audio_box.h"
#include "audio_karaoke.h"
#include "audio_edit_session.h"
#include "ass_karaoke.h"
#include "project.h"
#include "video_display.h"
#include "video_controller.h"
#include "async_video_provider.h"
#include "video_frame.h"
#include "visual_tool_drag.h"
#include "visual_tool_cross.h"
#include "visual_tool_scale.h"
#include "visual_tool_rotatexy.h"
#include "visual_tool_rotatez.h"
#include "visual_tool_clip.h"
#include "visual_tool_vector_clip.h"
#include "visual_tool_rectangle.h"
#include "subtitle_format.h"
#include <wx/toolbar.h>
#include <wx/evtloop.h>
#include <thread>
#include <chrono>
#include <libaegisub/ass/karaoke.h>
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
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <fstream>
#include <filesystem>

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
        if(dialog->GetTitle()!=title)return wxID_NONE;
        dialog->CallAfter([this,dialog] {
            action(dialog);
            if(dialog->IsModal())dialog->EndModal(wxID_CANCEL);
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
class ordinary_edit : public testing::Test {
protected:
    std::unique_ptr<agi::Context> c;
    wxFrame *window = nullptr;
    SubsEditBox *box = nullptr;
    SubsTextEditCtrl *editor = nullptr;
    void SetUp() override {
        c = std::make_unique<agi::Context>();
        window = new wxFrame(nullptr, wxID_ANY, "isolated editing tests");
        c->parent = window;
        box = new SubsEditBox(window, c.get());
        for (auto child : box->GetChildren())
            if (auto text = dynamic_cast<SubsTextEditCtrl*>(child)) editor = text;
        ASSERT_NE(nullptr, editor);
        auto write = c->subtitleAccess->TryWrite();
        c->ass->LoadDefault(); c->ass->Commit("", AssFile::COMMIT_NEW);
        auto line = &c->ass->Events.front();
        c->selectionController->SetSelectionAndActive({line}, line);
    }
    void TearDown() override { delete window; c.reset(); }
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
    auto id = c->ass->Events.front().Id;
    Input(" 中 文 {\\i1} ");
    EXPECT_EQ(" 中 文 {\\i1} ", c->ass->Events.front().Text.get());
    EXPECT_EQ(id, c->ass->Events.front().Id);
    EXPECT_FALSE(c->subsController->IsUndoStackEmpty());
    auto before = c->ass->Events.front().Text.get();
    auto comparison = c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    Input("blocked");
    EXPECT_EQ(before, c->ass->Events.front().Text.get());
    EXPECT_EQ(before, editor->GetTextRaw().data());
    comparison.reset();
    c->subsController->Undo(); EXPECT_EQ("", c->ass->Events.front().Text.get());
    c->subsController->Redo(); EXPECT_EQ(before, c->ass->Events.front().Text.get());
}
TEST_F(ordinary_edit, real_metadata_and_millisecond_time_events_share_multiline_write_lifetime) {
    {
        auto write=c->subtitleAccess->TryWrite();
        auto other=new AssDialogue; c->ass->Events.push_back(*other);
        c->ass->Commit("insert fixture", AssFile::COMMIT_DIAG_ADDREM);
        c->selectionController->SetSelectedSet({&c->ass->Events.front(), other});
    }
    TimeEdit *start=nullptr; wxSpinCtrl *layer=nullptr; wxComboBox *actor=nullptr;
    for (auto child:box->GetChildren()) {
        if (auto t=dynamic_cast<TimeEdit*>(child); t && t->GetToolTipText()=="Start time") start=t;
        if (auto s=dynamic_cast<wxSpinCtrl*>(child)) layer=s;
        if (auto a=dynamic_cast<wxComboBox*>(child); a && a->GetToolTipText().StartsWith("Actor name")) actor=a;
    }
    ASSERT_NE(nullptr,start); ASSERT_NE(nullptr,layer); ASSERT_NE(nullptr,actor);
    auto send=[&](wxWindow* child, int value, wxString text={}) {
        if (auto combo=dynamic_cast<wxComboBox*>(child)) combo->ChangeValue(text);
        wxCommandEvent event(wxEVT_TEXT,child->GetId()); event.SetInt(value); event.SetString(text);
        event.SetEventObject(child); box->GetEventHandler()->ProcessEvent(event);
    };
    start->SetTime(1001); send(start,0);
    send(layer,7); send(actor,0,"actor");
    for(auto const& row:c->ass->Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    start->SetTime(1002); send(start,0); send(layer,8); send(actor,0,"blocked");
    for(auto const& row:c->ass->Events) { EXPECT_EQ(1001,row.Start.GetMilliseconds()); EXPECT_EQ(7,row.Layer); EXPECT_EQ("actor",row.Actor.get()); }
}
TEST_F(ordinary_edit, grid_number_cell_direct_event_is_gated) {
    auto grid=new BaseGrid(window,c.get()); c->subsGrid=grid;
    { auto write=c->subtitleAccess->TryWrite(); c->ass->Commit("",AssFile::COMMIT_NEW); }
    wxFont font=*wxNORMAL_FONT; font.SetPointSize(OPT_GET("Subtitle/Grid/Font Size")->GetInt());
    auto face=FontFace("Subtitle/Grid"); if(!face.empty())font.SetFaceName(face);
    wxClientDC dc(grid); dc.SetFont(font);
    auto click=[&] {
        wxMouseEvent event(wxEVT_LEFT_DOWN); event.SetPosition({1,dc.GetCharHeight()+6});
        event.SetEventObject(grid); grid->GetEventHandler()->ProcessEvent(event);
    };
    click(); EXPECT_TRUE(c->ass->Events.front().Comment);
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    click(); EXPECT_TRUE(c->ass->Events.front().Comment);
    comparison.reset(); click(); EXPECT_FALSE(c->ass->Events.front().Comment);
    c->subsGrid=nullptr;
}
TEST_F(ordinary_edit, paste_insert_delete_sort_and_history_restore_ids_order_and_selection) {
    auto original=c->ass->Events.front().Id;
    cmd::PasteSubtitleLines(c.get(), "Dialogue: 0,0:00:02.001,0:00:03.002,Default,B,0,0,0,,second\nDialogue: 1,0:00:01.001,0:00:02.002,Default,A,0,0,0,,first");
    ASSERT_EQ(3u,c->ass->Events.size());
    std::vector<int> pasted_ids;
    for(auto const& row:c->ass->Events)pasted_ids.push_back(row.Id);
    EXPECT_EQ(2u,c->selectionController->GetSelectedSet().size());
    EXPECT_EQ(pasted_ids.front(),c->selectionController->GetActiveLine()->Id);
    c->subsController->Undo(); ASSERT_EQ(1u,c->ass->Events.size()); EXPECT_EQ(original,c->ass->Events.front().Id);
    c->subsController->Redo(); ASSERT_EQ(3u,c->ass->Events.size());
    std::vector<int> restored;for(auto const& row:c->ass->Events)restored.push_back(row.Id);
    EXPECT_EQ(pasted_ids,restored); EXPECT_EQ(2u,c->selectionController->GetSelectedSet().size());
    cmd::call("grid/sort/start/selected",c.get());
    EXPECT_EQ("first",c->ass->Events.front().Text.get()); EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    cmd::call("edit/line/delete",c.get()); ASSERT_EQ(1u,c->ass->Events.size()); EXPECT_EQ(original,c->ass->Events.front().Id);
    c->subsController->Undo(); EXPECT_EQ(3u,c->ass->Events.size());
    cmd::call("subtitle/insert/before",c.get()); EXPECT_EQ(4u,c->ass->Events.size());
    c->subsController->Undo(); EXPECT_EQ(3u,c->ass->Events.size());
}
TEST_F(ordinary_edit, binding_and_extradata_survive_undo_redo_and_blocked_public_entry) {
    cmd::PasteSubtitleLines(c.get(),"one\ntwo");
    BindSelectedSubtitles(c.get());
    auto group=GetSubtitleBinding(*c->ass,c->ass->Events.front()); ASSERT_FALSE(group.empty());
    auto extras=c->ass->Extradata.size();
    c->subsController->Undo(); EXPECT_TRUE(GetSubtitleBinding(*c->ass,c->ass->Events.front()).empty());
    c->subsController->Redo(); EXPECT_EQ(group,GetSubtitleBinding(*c->ass,c->ass->Events.front()));
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    UnbindSelectedSubtitles(c.get()); EXPECT_EQ(group,GetSubtitleBinding(*c->ass,c->ass->Events.front()));
    EXPECT_EQ(extras,c->ass->Extradata.size()); comparison.reset();
    UnbindSelectedSubtitles(c.get()); EXPECT_TRUE(GetSubtitleBinding(*c->ass,c->ass->Events.front()).empty());
}
TEST_F(ordinary_edit, nested_command_reuses_lease_and_commit_observers_cannot_edit_or_undo) {
    Input("keep");
    auto outer=c->subtitleAccess->TryWrite(); auto generation=c->subtitleEditGate.Revision().generation;
    cmd::call("edit/clear",c.get());
    EXPECT_EQ(generation+1,c->subtitleEditGate.Revision().generation); // Only Commit advanced it.
    bool visited=false;
    agi::signal::Connection observer=c->ass->AddCommitListener([&](int,const AssDialogue*) {
        visited=true; cmd::call("edit/clear",c.get()); c->subsController->Undo();
    });
    Input("notification"); EXPECT_TRUE(visited); EXPECT_EQ("notification",c->ass->Events.front().Text.get());
    outer.reset(); EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(ordinary_edit, undo_redo_reject_before_history_move_and_selection_notifications_cannot_reenter_history) {
    Input("one"); Input("two");
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    auto description=c->subsController->GetUndoDescription();
    c->subsController->Undo(); EXPECT_EQ("two",c->ass->Events.front().Text.get());
    EXPECT_EQ(description,c->subsController->GetUndoDescription()); comparison.reset();
    c->subsController->Undo(); EXPECT_EQ("one",c->ass->Events.front().Text.get());
    comparison=c->subtitleEditGate.TryBeginComparison(); c->subsController->Redo();
    EXPECT_EQ("one",c->ass->Events.front().Text.get()); comparison.reset();
    bool called=false;
    agi::signal::Connection selected=c->selectionController->AddSelectionListener([&] {
        called=true; c->subsController->Undo();
    });
    c->subsController->Redo(); EXPECT_TRUE(called); EXPECT_EQ("two",c->ass->Events.front().Text.get());
}
TEST_F(ordinary_edit, saved_modified_flag_tracks_ordinary_undo_redo_without_changing_save_policy) {
    Input("saved baseline");
    { auto write=c->subtitleAccess->TryWrite();c->subsController->Save(agi::fs::path("ordinary-saved.ass")); }
    EXPECT_FALSE(c->subsController->IsModified());
    cmd::call("edit/clear",c.get()); EXPECT_TRUE(c->subsController->IsModified());
    c->subsController->Undo(); EXPECT_FALSE(c->subsController->IsModified()); EXPECT_EQ("saved baseline",c->ass->Events.front().Text.get());
    c->subsController->Redo(); EXPECT_TRUE(c->subsController->IsModified());
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    c->subsController->Undo();EXPECT_TRUE(c->subsController->IsModified());EXPECT_EQ("",c->ass->Events.front().Text.get());
}
TEST_F(ordinary_edit, single_replacement_uses_real_engine_and_comparison_blocks_replace_all) {
    Input("alpha alpha");
    SearchReplaceSettings settings{}; settings.find="alpha";settings.replace_with="beta";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::ALL;
    c->search->Configure(settings); c->textSelectionController->SetSelection(0,5);
    EXPECT_TRUE(c->search->ReplaceNext()); EXPECT_EQ("beta alpha",c->ass->Events.front().Text.get());
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    EXPECT_FALSE(c->search->ReplaceAll()); EXPECT_FALSE(c->search->ReplaceNext());
    EXPECT_EQ("beta alpha",c->ass->Events.front().Text.get());
    comparison.reset();
    AcceptMessages messages;
    EXPECT_TRUE(c->search->ReplaceAll()); EXPECT_EQ("beta beta",c->ass->Events.front().Text.get());
    c->subsController->Undo(); EXPECT_EQ("beta alpha",c->ass->Events.front().Text.get());
}
TEST_F(ordinary_edit, canonical_unicode_find_is_readonly_and_returns_original_byte_offsets) {
    std::string original="x e\xCC\x81 {\\i1} e\xCC\x81 y"; Input(original);
    SearchReplaceSettings settings{}; settings.find="\xC3\xA9";settings.replace_with="Z";
    settings.field=SearchReplaceSettings::Field::TEXT;settings.limit_to=SearchReplaceSettings::Limit::ALL;
    settings.skip_tags=true; settings.match_case=true;
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    auto revision=c->subtitleEditGate.Revision(); auto matcher=SearchReplaceEngine::GetMatcher(settings);
    auto match=matcher(&c->ass->Events.front(),0); ASSERT_TRUE(match);
    EXPECT_EQ(2u,match.start); EXPECT_EQ(5u,match.end);
    auto second=matcher(&c->ass->Events.front(),match.end); ASSERT_TRUE(second);
    EXPECT_EQ("e\xCC\x81",original.substr(second.start,second.end-second.start));
    EXPECT_EQ(original,c->ass->Events.front().Text.get()); EXPECT_EQ(revision,c->subtitleEditGate.Revision());
    comparison.reset(); c->search->Configure(settings); c->textSelectionController->SetSelection(2,5);
    EXPECT_TRUE(c->search->ReplaceNext()); EXPECT_EQ("x Z {\\i1} e\xCC\x81 y",c->ass->Events.front().Text.get());
    c->subsController->Undo(); EXPECT_EQ(original,c->ass->Events.front().Text.get());
    settings.use_regex=true;settings.find="(\xC3\xA9)";settings.replace_with="[$1]";
    c->search->Configure(settings); AcceptMessages messages;
    EXPECT_TRUE(c->search->ReplaceAll()); EXPECT_EQ("x [\xC3\xA9] {\\i1} [\xC3\xA9] y",c->ass->Events.front().Text.get());
}
TEST_F(ordinary_edit, real_style_editor_apply_is_gated_and_new_styles_keep_undo_redo) {
    AcceptMessages messages;
    wxArrayString fonts; fonts.Add("Arial");
    DialogStyleEditor dialog(window,c->ass->GetStyle("Default"),c.get(),nullptr,"Copied",fonts);
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    button(&dialog,wxID_APPLY); EXPECT_EQ(1u,c->ass->Styles.size()); EXPECT_EQ(nullptr,c->ass->GetStyle("Copied"));
    comparison.reset(); button(&dialog,wxID_APPLY);
    ASSERT_NE(nullptr,c->ass->GetStyle("Copied")); EXPECT_EQ(2u,c->ass->Styles.size());
    auto style=c->ass->GetStyle("Copied");
    button(&dialog,wxID_APPLY); EXPECT_EQ(style,c->ass->GetStyle("Copied")); // Apply again updates, no duplicate.
    c->subsController->Undo(); EXPECT_EQ(2u,c->ass->Styles.size());
    c->subsController->Undo(); EXPECT_EQ(nullptr,c->ass->GetStyle("Copied"));
    c->subsController->Redo(); EXPECT_NE(nullptr,c->ass->GetStyle("Copied"));
}
TEST_F(ordinary_edit, style_editor_rejects_stale_live_style_after_another_edit) {
    AcceptMessages messages; wxArrayString fonts; fonts.Add("Arial");
    DialogStyleEditor dialog(window,c->ass->GetStyle("Default"),c.get(),nullptr,"",fonts);
    Input("changed elsewhere"); auto revision=c->subsController->GetUndoDescription();
    button(&dialog,wxID_APPLY);
    EXPECT_EQ(revision,c->subsController->GetUndoDescription()); EXPECT_EQ(1u,c->ass->Styles.size());
}
TEST_F(ordinary_edit, script_properties_event_rejects_comparison_and_preserves_undo) {
    auto original=std::string(c->ass->GetScriptInfo("Title"));
    RunModal hook("Script Properties",[&](wxDialog* dialog) {
        wxTextCtrl* title=nullptr;
        for(auto child:descendants(dialog))if(auto text=dynamic_cast<wxTextCtrl*>(child)){title=text;break;}
        ASSERT_NE(nullptr,title);title->ChangeValue("isolated title");button(dialog,wxID_OK);
    });
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    ShowPropertiesDialog(c.get());EXPECT_EQ(original,c->ass->GetScriptInfo("Title"));comparison.reset();
    ShowPropertiesDialog(c.get());EXPECT_EQ("isolated title",c->ass->GetScriptInfo("Title"));
    c->subsController->Undo();EXPECT_EQ(original,c->ass->GetScriptInfo("Title"));
}
TEST_F(ordinary_edit, attachment_delete_event_is_gated_and_undo_restores_content) {
    std::ofstream data("attachment.ttf",std::ios::binary);data << "attachment fixture";data.close();
    { auto write=c->subtitleAccess->TryWrite();c->ass->InsertAttachment(agi::fs::path("attachment.ttf"));c->ass->Commit("attachment",AssFile::COMMIT_ATTACHMENT); }
    auto original=c->ass->Attachments.front().GetEntryData();
    RunModal hook("Attachment List",[&](wxDialog* dialog) {
        wxListView* list=nullptr;wxButton* remove=nullptr;
        for(auto child:descendants(dialog)) {
            if(auto view=dynamic_cast<wxListView*>(child))list=view;
            if(auto b=dynamic_cast<wxButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
        }
        ASSERT_NE(nullptr,list);ASSERT_NE(nullptr,remove);list->Select(0);
        wxCommandEvent event(wxEVT_BUTTON,remove->GetId());event.SetEventObject(remove);remove->GetEventHandler()->ProcessEvent(event);
    });
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    ShowAttachmentsDialog(c.get());EXPECT_EQ(1u,c->ass->Attachments.size());comparison.reset();
    ShowAttachmentsDialog(c.get());EXPECT_TRUE(c->ass->Attachments.empty());
    c->subsController->Undo();ASSERT_EQ(1u,c->ass->Attachments.size());EXPECT_EQ(original,c->ass->Attachments.front().GetEntryData());
}
TEST_F(ordinary_edit, real_style_import_and_delete_preserve_unrelated_data_and_history) {
    class ImportDialogs : public wxModalDialogHook {
    public:
        wxString path;
        int files=0;
        explicit ImportDialogs(wxString path):path(path) { Register(); }
        int Enter(wxDialog* dialog) override {
            if(auto file=dynamic_cast<wxFileDialog*>(dialog)) { ++files; file->SetPath(path); return wxID_OK; }
            if(auto choices=dynamic_cast<wxMultiChoiceDialog*>(dialog)) { wxArrayInt selected;selected.Add(0);selected.Add(1);choices->SetSelections(selected);choices->TransferDataFromWindow(); return wxID_OK; }
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
        auto write=c->subtitleAccess->TryWrite();c->ass->InsertAttachment(agi::fs::path("attachment.ttf"));
        auto extra=c->ass->AddExtradata("opaque","retained"); c->ass->Events.front().ExtradataIds=std::vector<uint32_t>{extra};
        c->ass->Commit("fixture metadata",AssFile::COMMIT_ATTACHMENT|AssFile::COMMIT_EXTRADATA);
    }
    ShowStyleManagerDialog(c.get());
    wxDialog* manager=nullptr;
    for(auto top:wxTopLevelWindows) if(auto d=dynamic_cast<wxDialog*>(top);d && d->GetTitle()=="Styles Manager")manager=d;
    ASSERT_NE(nullptr,manager); manager->Hide();
    auto cleanup=std::unique_ptr<wxDialog,std::function<void(wxDialog*)>>(manager,[](wxDialog* d){d->Close();});
    wxButton *import=nullptr,*remove=nullptr;wxListBox *current=nullptr;wxWindow *down=nullptr,*sort=nullptr;
    for(auto child:descendants(manager)) {
        if(auto b=dynamic_cast<wxButton*>(child); b && b->GetLabel()=="&Import from script...")import=b;
        if(child->GetParent()->GetLabel()=="Current script") {
            if(auto b=dynamic_cast<wxButton*>(child);b && b->GetLabel()=="&Delete")remove=b;
            if(auto list=dynamic_cast<wxListBox*>(child))current=list;
            if(child->GetToolTipText()=="Move style down")down=child;
            if(child->GetToolTipText()=="Sort styles alphabetically")sort=child;
        }
    }
    ASSERT_NE(nullptr,import); ASSERT_NE(nullptr,remove); ASSERT_NE(nullptr,current);
    ASSERT_NE(nullptr,down);ASSERT_NE(nullptr,sort);
    auto path=std::filesystem::absolute("styles.ass").wstring(); ImportDialogs dialogs{wxString(path)};
    auto click=[](wxWindow* control) { wxCommandEvent event(wxEVT_BUTTON,control->GetId());event.SetEventObject(control);control->GetEventHandler()->ProcessEvent(event); };
    auto comparison=c->subtitleEditGate.TryBeginComparison(); ASSERT_TRUE(comparison);
    click(import);EXPECT_EQ(0,dialogs.files);EXPECT_EQ(1u,c->ass->Styles.size());comparison.reset();
    click(import);ASSERT_NE(nullptr,c->ass->GetStyle("Imported"));EXPECT_EQ(72.,c->ass->GetStyle("Imported")->fontsize);
    EXPECT_EQ(70.,c->ass->GetStyle("Default")->fontsize);EXPECT_EQ(1,dialogs.files);
    EXPECT_EQ(1u,c->ass->Attachments.size());EXPECT_EQ("retained",c->ass->Extradata.front().value);
    current->DeselectAll();current->SetStringSelection("Default");click(down);
    EXPECT_EQ("Imported",c->ass->Styles.front().name);c->subsController->Undo();
    EXPECT_EQ("Default",c->ass->Styles.front().name);click(sort);c->subsController->Undo();
    current->DeselectAll();current->SetStringSelection("Imported");
    comparison=c->subtitleEditGate.TryBeginComparison();click(remove);EXPECT_NE(nullptr,c->ass->GetStyle("Imported"));comparison.reset();
    click(remove);EXPECT_EQ(nullptr,c->ass->GetStyle("Imported"));
    c->subsController->Undo();EXPECT_NE(nullptr,c->ass->GetStyle("Imported"));
    c->subsController->Undo();EXPECT_EQ(nullptr,c->ass->GetStyle("Imported"));EXPECT_EQ(48.,c->ass->GetStyle("Default")->fontsize);
    c->subsController->Redo();EXPECT_NE(nullptr,c->ass->GetStyle("Imported"));
    EXPECT_EQ(1u,c->ass->Attachments.size());EXPECT_EQ("retained",c->ass->Extradata.front().value);
}

TEST_F(ordinary_edit, all_migrated_command_operators_reject_comparison_before_any_model_or_history_change) {
    Input("unchanged");
    auto id=c->ass->Events.front().Id;
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
        "time/next",
        "time/prev",
    };
    for(auto name:commands) {
        SCOPED_TRACE(name); (*cmd::get(name))(c.get());
        EXPECT_EQ(1u,c->ass->Events.size()); EXPECT_EQ(id,c->ass->Events.front().Id);
        EXPECT_EQ("unchanged",c->ass->Events.front().Text.get());
        EXPECT_EQ(description,c->subsController->GetUndoDescription());
        EXPECT_EQ(id,c->selectionController->GetActiveLine()->Id);
    }
    cmd::PasteSubtitleLines(c.get(),"blocked\nblocked"); EXPECT_EQ(1u,c->ass->Events.size());
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
        auto& row=c->ass->Events.front();row.Start=1001;row.End=2002;
        row.Text="{\\k50}ab{\\k50}cd";
        c->ass->Commit("audio fixture",AssFile::COMMIT_NEW);
        c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    }
    void TearDown() override {
        c->audioController->SetTimingController(nullptr);
        ordinary_edit::TearDown();
    }
    void Karaoke() {
        SetKaraokeLine(karaoke,&c->ass->Events.front(),true);
        c->audioController->SetTimingController(CreateKaraokeTimingController(c.get(),&karaoke,file_slot));
    }
    AudioDisplay* Display() {
        auto display=new AudioDisplay(window,c->audioController.get(),c.get());
        c->project->LoadAudio("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=441000");
        display->SetSize(0,0,900,160);
        return display;
    }
    void Mouse(AudioDisplay* display,wxEventType type,int ms,bool down=false) {
        wxMouseEvent event(type);event.SetLeftDown(down);
        event.SetPosition({display->RelativeXFromTime(ms),80});event.SetEventObject(display);
        display->GetEventHandler()->ProcessEvent(event);
    }
};
TEST_F(audio_edit, dialogue_manual_drag_cancel_never_writes_or_adds_history) {
    auto description=c->subsController->GetUndoDescription();
    auto token=Timing()->MarkerLifetime();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);ASSERT_EQ(1u,markers.size());
    Timing()->OnMarkerDrag(markers,1103,0);
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->EndDrag(true);
    EXPECT_TRUE(token.expired());
    EXPECT_EQ(1001,Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(description,c->subsController->GetUndoDescription());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, dialogue_manual_commit_and_history_preserve_exact_milliseconds_and_identity) {
    auto id=c->ass->Events.front().Id;
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,c->ass->Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    c->subsController->Redo();EXPECT_EQ(1011,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(id,c->ass->Events.front().Id);
    EXPECT_EQ(id,c->selectionController->GetActiveLine()->Id);
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
    EXPECT_EQ(1101,c->ass->Events.front().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_EQ(1101,c->ass->Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, selection_change_invalidates_markers_and_releases_pending_task) {
    AssDialogue* next;
    {auto write=c->subtitleAccess->TryWrite();next=new AssDialogue;
     next->Start=3001;next->End=4002;c->ass->Events.push_back(*next);c->ass->Commit("audio fixture",AssFile::COMMIT_NEW);}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1001,false,false,5,0);auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1111,0);
    c->selectionController->SetSelectionAndActive({next},next);
    EXPECT_TRUE(token.expired());EXPECT_EQ(3001,Timing()->GetActiveLineRange().begin());
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, multiple_selected_rows_share_pending_lifetime_and_undo) {
    AssDialogue* next;
    {auto write=c->subtitleAccess->TryWrite();next=new AssDialogue;
     next->Start=3001;next->End=4002;c->ass->Events.push_back(*next);c->ass->Commit("audio fixture",AssFile::COMMIT_NEW);
     c->selectionController->SetSelectedSet({&c->ass->Events.front(),next});}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnRightClick(4503,false,0,0);ASSERT_EQ(2u,markers.size());
    Timing()->EndDrag(false);Timing()->Commit();
    for(auto const& row:c->ass->Events)EXPECT_EQ(4503,row.End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(2002,c->ass->Events.front().End.GetMilliseconds());
    EXPECT_EQ(4002,c->ass->Events.back().End.GetMilliseconds());
    EXPECT_EQ(2u,c->selectionController->GetSelectedSet().size());
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
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, karaoke_lead_is_staged_cancelled_then_committed_with_undo) {
    Karaoke();auto text=c->ass->Events.front().Text.get();
    auto lead=OPT_GET("Audio/Lead/IN")->GetInt();
    Timing()->AddLeadIn();EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(text,c->ass->Events.front().Text.get());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->Revert();EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(1001-lead,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,c->ass->Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(text,c->ass->Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ(1001-lead,c->ass->Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, karaoke_drag_and_controller_switch_cancel_pending_cache) {
    Karaoke();auto text=c->ass->Events.front().Text.get();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1500,false,false,10,0);ASSERT_EQ(1u,markers.size());
    auto token=Timing()->MarkerLifetime();Timing()->OnMarkerDrag(markers,1600,0);
    EXPECT_NE(text,karaoke.GetText());EXPECT_EQ(text,c->ass->Events.front().Text.get());
    c->audioController->SetTimingController(CreateDialogueTimingController(c.get()));
    EXPECT_TRUE(token.expired());EXPECT_EQ(text,c->ass->Events.front().Text.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
TEST_F(audio_edit, real_wx_audio_drag_release_and_destroyed_controller_borrow) {
    auto display=Display();
    Mouse(display,wxEVT_LEFT_DOWN,1001,true);EXPECT_TRUE(display->HasCapture());
    Mouse(display,wxEVT_MOTION,1200,true);
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(display,wxEVT_LEFT_UP,1200);
    Timing()->Commit();EXPECT_GT(c->ass->Events.front().Start.GetMilliseconds(),1100);
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
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());c->project->CloseAudio();
}
TEST_F(audio_edit, real_wx_drag_exception_releases_task_and_capture) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    agi::signal::Connection observer=Timing()->AddMarkerMovedListener([] {throw std::runtime_error("marker observer");});
    EXPECT_THROW(Mouse(display,wxEVT_MOTION,1200,true),std::runtime_error);
    EXPECT_FALSE(display->HasCapture());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    observer.Disconnect();c->project->CloseAudio();
}
TEST_F(audio_edit, stale_revision_missing_target_and_document_change_never_write_back) {
    AudioEditSession edit(c.get());ASSERT_TRUE(edit.Begin());
    auto row=c->ass->Events.front(); // independent AssDialogue copy
    {auto scope=c->subtitleAccess->TryWrite();c->ass->Events.front().Start=1002;c->ass->Commit("other",AssFile::COMMIT_DIAG_TIME);}
    EXPECT_FALSE(edit.Begin());
    EXPECT_EQ(1002,c->ass->Events.front().Start.GetMilliseconds());
    edit.Refresh();ASSERT_TRUE(edit.Begin());
    row.Id+=100000;
    EXPECT_THROW(edit.Update({row}),std::invalid_argument);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    edit.Refresh();c->audioController->SetTimingController(nullptr);c->subtitleEditGate.Close();EXPECT_FALSE(edit.Begin());
}
TEST_F(audio_edit, outer_time_command_scope_is_reused_without_spurious_stale_rejection) {
    (*cmd::get("time/lead/in"))(c.get());
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_LT(Timing()->GetActiveLineRange().begin(),1001);
    Timing()->Commit();EXPECT_LT(c->ass->Events.front().Start.GetMilliseconds(),1001);
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
    auto text=c->ass->Events.front().Text.get();
    click();EXPECT_TRUE(cancel->IsEnabled());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(text,c->ass->Events.front().Text.get());
    button(cancel,cancel->GetId());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(text,c->ass->Events.front().Text.get());
    auto token=Timing()->MarkerLifetime();
    click();button(accept,accept->GetId());
    EXPECT_NE(text,c->ass->Events.front().Text.get());EXPECT_TRUE(token.expired());
    auto changed=c->ass->Events.front().Text.get();
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,c->ass->Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(text,c->ass->Events.front().Text.get());
    c->subsController->Redo();EXPECT_EQ(changed,c->ass->Events.front().Text.get());
    auto comparison=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(comparison);
    click();button(accept,accept->GetId());
    EXPECT_EQ(changed,c->ass->Events.front().Text.get());
    comparison.reset();c->project->CloseAudio();
}
TEST_F(audio_edit, audio_source_close_invalidates_active_drag_and_releases_registration) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);auto token=Timing()->MarkerLifetime();
    c->project->CloseAudio();
    EXPECT_TRUE(token.expired());EXPECT_FALSE(display->HasCapture());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
}
TEST_F(audio_edit, linked_neighbor_is_authorized_and_restored_by_undo) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue;
     other->Start=2002;other->End=3003;c->ass->Events.push_back(*other);c->ass->Commit("audio fixture",AssFile::COMMIT_DIAG_ADDREM);}
    OPT_SET("Timing/Link Adjacent Lines")->SetBool(true);
    Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_EQ(2012,c->ass->Events.front().End.GetMilliseconds());
    EXPECT_EQ(2012,c->ass->Events.back().Start.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(2002,c->ass->Events.back().Start.GetMilliseconds());
    EXPECT_EQ(2002,c->ass->Events.front().End.GetMilliseconds());
    auto session=c->subtitleAccess->TryBeginInteraction({c->ass->Events.front().Id});ASSERT_TRUE(session);
    EXPECT_THROW(session->LinkAdjacent(c->ass->Events.front().Id,false,2002,100),std::logic_error);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, real_wx_capture_lost_cancels_pending_drag) {
    auto display=Display();Mouse(display,wxEVT_LEFT_DOWN,1001,true);
    Mouse(display,wxEVT_MOTION,1200,true);display->ReleaseMouse();
    wxMouseCaptureLostEvent lost(display->GetId());
    display->GetEventHandler()->ProcessEvent(lost);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
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
    auto text=c->ass->Events.front().Text.get();
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnLeftClick(1500,false,false,10,0);ASSERT_EQ(1u,markers.size());
    auto token=Timing()->MarkerLifetime();
    Timing()->OnMarkerDrag(markers,1600,0);Timing()->OnMarkerDrag(markers,1700,0);
    EXPECT_FALSE(token.expired());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Timing()->EndDrag(false);EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    EXPECT_NE(text,c->ass->Events.front().Text.get());
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,c->ass->Events.front().End.GetMilliseconds());
    auto changed=c->ass->Events.front().Text.get();
    c->subsController->Undo();EXPECT_EQ(text,c->ass->Events.front().Text.get());
    EXPECT_EQ("audio fixture",c->subsController->GetUndoDescription());
    c->subsController->Redo();EXPECT_EQ(changed,c->ass->Events.front().Text.get());
}
TEST_F(audio_edit, inactive_neighbor_cache_survives_undo_node_replacement) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue;
     other->Start=3001;other->End=4002;c->ass->Events.push_back(*other);c->ass->Commit("audio fixture",AssFile::COMMIT_DIAG_ADDREM);}
    OPT_SET("Audio/Inactive Lines Display Mode")->SetInt(2);
    Timing()->ModifyStart(1);Timing()->Commit();
    c->subsController->Undo();c->subsController->Redo();
    EXPECT_EQ(1011,Timing()->GetActiveLineRange().begin());
    AudioMarkerVector markers;Timing()->GetMarkers(TimeRange(0,5000),markers);
    EXPECT_GE(markers.size(),4u);
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1021,c->ass->Events.front().Start.GetMilliseconds());
}

TEST_F(audio_edit, no_op_navigation_and_boundary_commands_do_not_stale_audio_cache) {
    {auto no_op=c->subtitleAccess->TryWrite();ASSERT_TRUE(no_op);}
    (*cmd::get("time/prev"))(c.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,c->ass->Events.front().Start.GetMilliseconds());
    Karaoke();auto text=c->ass->Events.front().Text.get();
    (*cmd::get("time/prev"))(c.get());
    (*cmd::get("time/start/increase"))(c.get()); // first syllable: no movable left boundary
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    (*cmd::get("time/next"))(c.get());
    (*cmd::get("time/length/increase"))(c.get()); // last syllable: no movable right boundary
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    (*cmd::get("time/prev"))(c.get());
    Timing()->ModifyLength(1,false);Timing()->Commit();
    EXPECT_NE(text,c->ass->Events.front().Text.get());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(audio_edit, stale_pending_audio_task_stays_rejected_until_explicit_rebuild) {
    Timing()->ModifyStart(1);
    {auto foreign=c->subtitleEditGate.TryBeginWrite();ASSERT_TRUE(foreign);}
    Timing()->Commit();Timing()->Commit();
    EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Timing()->Revert();Timing()->ModifyStart(1);Timing()->Commit();
    EXPECT_EQ(1011,c->ass->Events.front().Start.GetMilliseconds());
}

TEST_F(audio_edit, karaoke_one_millisecond_lead_is_not_lost_to_tag_rounding) {
    auto lead=OPT_GET("Audio/Lead/IN")->GetInt();
    OPT_SET("Audio/Lead/IN")->SetInt(1);Karaoke();
    Timing()->AddLeadIn();Timing()->Commit();
    EXPECT_EQ(1000,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(2002,c->ass->Events.front().End.GetMilliseconds());
    c->subsController->Undo();EXPECT_EQ(1001,c->ass->Events.front().Start.GetMilliseconds());
    OPT_SET("Audio/Lead/IN")->SetInt(lead);
}

TEST_F(audio_edit, cancelling_an_untimed_row_cannot_be_committed_later) {
    {auto scope=c->subtitleAccess->TryWrite();c->ass->Events.front().Start=0;c->ass->Events.front().End=0;
     c->ass->Commit("audio fixture",AssFile::COMMIT_DIAG_TIME);}
    ASSERT_TRUE(Timing()->BeginDrag());
    auto markers=Timing()->OnRightClick(1500,false,0,0);ASSERT_FALSE(markers.empty());
    Timing()->EndDrag(true);Timing()->Commit();
    EXPECT_EQ(0,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(0,c->ass->Events.front().End.GetMilliseconds());
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
         auto& row=c->ass->Events.front();row.Start=0;row.End=5000;
         row.Text="{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail";
         c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_FULL);}
        toolbar=new wxToolBar(window,wxID_ANY);
        auto zoom=new wxComboBox(window,wxID_ANY);
        display=new VideoDisplay(toolbar,false,zoom,window,c.get());
        display->SetSize(640,480);
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
    std::string Text() { return c->ass->Events.front().Text.get(); }
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
    Install<VisualToolDrag>();auto text=Text();auto id=c->ass->Events.front().Id;
    auto before=FrameData();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);ASSERT_TRUE(display->HasCapture());
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_MOTION,140,160,true);EXPECT_EQ(text,Text());
    auto preview=FrameData();
    EXPECT_NE(before,preview);
    Mouse(wxEVT_LEFT_UP,140,160);
    EXPECT_NE(text,Text());EXPECT_NE(std::string::npos,Text().find("\\pos(140,160)"));
    EXPECT_NE(std::string::npos,Text().find("\\bord3"));EXPECT_NE(std::string::npos,Text().find("中 文 {\\b1} tail"));
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(id,c->ass->Events.front().Id);
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
    EXPECT_EQ(1u,c->ass->Events.size());
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
    Install<VisualToolRectangle>();auto source=c->ass->Events.front().Id;auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,100,150,true);
    EXPECT_EQ(1u,c->ass->Events.size());EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_UP,100,150);
    ASSERT_EQ(2u,c->ass->Events.size());EXPECT_NE(source,c->ass->Events.front().Id);
    EXPECT_EQ(source,c->ass->Events.back().Id);EXPECT_EQ(text,c->ass->Events.back().Text.get());
    EXPECT_NE(nullptr,c->ass->GetStyle("图形"));EXPECT_EQ("图形",c->ass->Events.front().Style.get());
    c->subsController->Undo();EXPECT_EQ(1u,c->ass->Events.size());EXPECT_EQ(nullptr,c->ass->GetStyle("图形"));
    c->subsController->Redo();EXPECT_EQ(2u,c->ass->Events.size());
}
TEST_F(visual_edit, vector_clip_drag_and_cancel_rebuild_path_without_losing_inverse) {
    {auto write=c->subtitleAccess->TryWrite();c->ass->Events.front().Text="{\\iclip(m 50 50 l 200 50 200 200)\\bord3}字幕";c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_TEXT);}
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
     other->Text="{\\pos(300,300)}other";c->ass->Events.push_back(*other);c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();auto text=Text();auto other_id=other->Id;
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,150,150,true);
    c->selectionController->SetSelectionAndActive({other},other);
    EXPECT_EQ(text,Text());EXPECT_FALSE(display->HasCapture());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Mouse(wxEVT_LEFT_DOWN,300,300,true);Mouse(wxEVT_MOTION,350,350,true);
    (*cmd::get("edit/line/delete"))(c.get());
    Mouse(wxEVT_LEFT_UP,350,350);
    EXPECT_EQ(1u,c->ass->Events.size());EXPECT_NE(other_id,c->ass->Events.front().Id);EXPECT_EQ(text,Text());
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
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue(c->ass->Events.front());
     row->Text="{\\pos(300,300)\\blur2}other";c->ass->Events.push_back(*row);
     first_id=c->ass->Events.front().Id;second_id=row->Id;
     auto extra=c->ass->AddExtradata("unknown-test","opaque value");
     c->ass->Events.front().Actor="actor";c->ass->Events.front().Layer=7;c->ass->Events.front().ExtradataIds=std::vector<uint32_t>{extra};
     c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);
     c->selectionController->SetSelectedSet({&c->ass->Events.front(),row});BindSelectedSubtitles(c.get());
     group=GetSubtitleBinding(*c->ass,c->ass->Events.front());}
    auto extras=c->ass->Extradata;Install<VisualToolDrag>();
    Drag(100,100,130,140);
    EXPECT_EQ(first_id,c->ass->Events.front().Id);EXPECT_EQ(second_id,c->ass->Events.back().Id);
    EXPECT_EQ(7,c->ass->Events.front().Layer);EXPECT_EQ("actor",c->ass->Events.front().Actor.get());
    EXPECT_EQ(group,GetSubtitleBinding(*c->ass,c->ass->Events.front()));
    EXPECT_NE(std::string::npos,c->ass->Events.back().Text.get().find("\\pos(330,340)"));
    ASSERT_EQ(extras.size(),c->ass->Extradata.size());
    for(size_t i=0;i<extras.size();++i){EXPECT_EQ(extras[i].key,c->ass->Extradata[i].key);EXPECT_EQ(extras[i].value,c->ass->Extradata[i].value);}
}
TEST_F(visual_edit, selecting_another_visible_line_with_subtitle_sync_keeps_feature_alive) {
    {auto write=c->subtitleAccess->TryWrite();auto row=new AssDialogue;
     row->Start=1000;row->End=5000;row->Text="{\\pos(300,300)}other";c->ass->Events.push_back(*row);c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    c->videoController->JumpToFrame(50);OPT_SET("Video/Subtitle Sync")->SetBool(true);
    Install<VisualToolDrag>();auto first=Text();
    Drag(300,300,340,350);
    EXPECT_EQ(first,Text());EXPECT_NE(std::string::npos,c->ass->Events.back().Text.get().find("\\pos(340,350)"));
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
    EXPECT_EQ(1u,c->ass->Events.size());EXPECT_EQ(text,Text());EXPECT_EQ(nullptr,c->ass->GetStyle("图形"));
    tool->SetSubTool(static_cast<int>(DrawingShape::Polygon));
    Mouse(wxEVT_LEFT_DOWN,20,20,true);Mouse(wxEVT_LEFT_UP,20,20);
    Mouse(wxEVT_LEFT_DOWN,100,20,true);Mouse(wxEVT_LEFT_UP,100,20);
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_LEFT_UP,100,100);
    EXPECT_FALSE(c->subtitleEditGate.TryBeginComparison());
    wxKeyEvent enter(wxEVT_CHAR_HOOK);enter.m_keyCode=WXK_RETURN;display->GetEventHandler()->ProcessEvent(enter);
    ASSERT_EQ(2u,c->ass->Events.size());auto style=c->ass->GetStyle("图形");ASSERT_NE(nullptr,style);auto data=style->GetEntryData();
    tool->SetSubTool(static_cast<int>(DrawingShape::Ellipse));Drag(200,200,240,260);
    EXPECT_EQ(3u,c->ass->Events.size());EXPECT_EQ(data,c->ass->GetStyle("图形")->GetEntryData());
}
TEST_F(visual_edit, commit_observer_exception_ends_task_and_preserves_normal_undo) {
    Install<VisualToolDrag>();auto text=Text();
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    agi::signal::Connection observer=c->ass->AddCommitListener([]{throw std::runtime_error("observer");});
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
    auto session=c->subtitleAccess->TryBeginInteraction({c->ass->Events.front().Id});ASSERT_TRUE(session);
    AssDialogue drawing;drawing.Id=c->ass->Events.front().Id;AssStyle style;style.name="must-not-be-added";
    auto count=c->ass->Styles.size();
    EXPECT_THROW(session->InsertBefore(drawing.Id,drawing,&style),std::invalid_argument);
    EXPECT_EQ(1u,c->ass->Events.size());EXPECT_EQ(count,c->ass->Styles.size());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}

TEST_F(visual_edit, outside_video_rectangle_click_does_not_leave_a_write_task) {
    Install<VisualToolRectangle>();Mouse(wxEVT_LEFT_DOWN,-10,-10,true);Mouse(wxEVT_LEFT_UP,-10,-10);
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());EXPECT_EQ(1u,c->ass->Events.size());
}
TEST_F(visual_edit, cross_double_click_preserves_move_origin_and_unrelated_tags) {
    {auto write=c->subtitleAccess->TryWrite();c->ass->Events.front().Text="{\\move(100,100,200,200,10,900)\\org(80,80)\\fax0.2\\i1}字幕";c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_TEXT);}
    Install<VisualToolCross>();Mouse(wxEVT_LEFT_DCLICK,150,160,true);Mouse(wxEVT_LEFT_UP,150,160);
    EXPECT_NE(std::string::npos,Text().find("\\move(150,160,250,260,10,900)"));
    EXPECT_NE(std::string::npos,Text().find("\\org(130,140)"));EXPECT_NE(std::string::npos,Text().find("\\fax0.2"));
}
TEST_F(visual_edit, replacing_document_content_invalidates_old_geometry_and_target_ids) {
    Install<VisualToolDrag>();auto previous=c->ass->Events.front().Id;
    Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    {auto write=c->subtitleAccess->TryWrite();AssFile replacement;replacement.LoadDefault();
     replacement.SetScriptInfo("PlayResX","640");replacement.SetScriptInfo("PlayResY","480");
     replacement.Events.front().Text="{\\pos(300,300)}new document";c->ass->swap(replacement);
     c->ass->Commit("new document",AssFile::COMMIT_NEW);
     auto active=&c->ass->Events.front();c->selectionController->SetSelectionAndActive({active},active);}
    Mouse(wxEVT_LEFT_UP,160,170);
    EXPECT_NE(previous,c->ass->Events.front().Id);EXPECT_EQ("{\\pos(300,300)}new document",Text());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    Drag(300,300,350,350);EXPECT_NE(std::string::npos,Text().find("\\pos(350,350)"));
}
TEST_F(visual_edit, feature_hit_order_follows_document_order_after_sort) {
    {auto write=c->subtitleAccess->TryWrite();auto other=new AssDialogue(c->ass->Events.front());
     c->ass->Events.push_back(*other);c->ass->Events.reverse();c->ass->Commit("visual fixture",AssFile::COMMIT_ORDER);}
    auto expected=c->ass->Events.back().Id;auto untouched=c->ass->Events.front().Text.get();
    Install<VisualToolDrag>();Drag(100,100,160,170);
    EXPECT_EQ(expected,c->selectionController->GetActiveLine()->Id);
    EXPECT_EQ(untouched,c->ass->Events.front().Text.get());EXPECT_NE(untouched,c->ass->Events.back().Text.get());
}
TEST_F(visual_edit, missing_target_without_commit_cannot_partially_write_other_rows) {
    AssDialogue* other;
    {auto write=c->subtitleAccess->TryWrite();other=new AssDialogue;other->Text="untouched";
     c->ass->Events.push_back(*other);c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);Mouse(wxEVT_MOTION,160,170,true);
    // Deliberately simulate a not-yet-migrated C1-C caller which removes a row without Commit.
    {auto write=c->subtitleAccess->TryWrite();c->ass->Events.erase_and_dispose(c->ass->Events.begin(),[](auto row){delete row;});}
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,160,170),std::out_of_range);
    EXPECT_EQ("untouched",Text());EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
    c->selectionController->SetSelectionAndActive({other},other);
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
    AssDialogueBase valid=c->ass->Events.front();valid.Text="{\\pos(500,400)}different";
    auto missing=valid;missing.Id+=100000;
    c->project->VideoProvider()->PreviewSubtitles({valid,missing});
    EXPECT_EQ(before,FrameData());EXPECT_EQ(text,Text());
    c->project->VideoProvider()->PreviewSubtitles({valid,valid});EXPECT_EQ(before,FrameData());
}
TEST_F(visual_edit, ten_thousand_rows_keep_mouse_motion_local_to_edited_targets) {
    {auto write=c->subtitleAccess->TryWrite();for(int i=1;i<10000;++i){auto row=new AssDialogue;
     row->Start=10000;row->End=15000;row->Text="inactive subtitle";c->ass->Events.push_back(*row);}
     c->ass->Commit("visual fixture",AssFile::COMMIT_DIAG_ADDREM);}
    Install<VisualToolDrag>();Mouse(wxEVT_LEFT_DOWN,100,100,true);
    auto start=std::chrono::steady_clock::now();
    for(int i=1;i<=100;++i)Mouse(wxEVT_MOTION,100+i,100+i,true);
    auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("B2 10000 rows, 100 mouse moves: %.3f ms UI dispatch\n",milliseconds);
    EXPECT_LT(milliseconds,10000);EXPECT_EQ("{\\pos(100,100)\\bord3\\i1}中 文 {\\b1} tail",Text());
    Mouse(wxEVT_LEFT_UP,200,200);EXPECT_EQ(10000u,c->ass->Events.size());
    EXPECT_NE(std::string::npos,Text().find("\\pos(200,200)"));
    EXPECT_EQ("inactive subtitle",c->ass->Events.back().Text.get());
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
    ASSERT_EQ(2u,c->ass->Events.size());auto shape=c->ass->Events.front().Text.get();
    Install<VisualToolDrag>();
    auto save=agi::fs::path((std::filesystem::current_path()/"visual-result.ass").wstring());
    {auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile reopened;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&reopened,save,c->project->Timecodes(),"utf-8");
    ASSERT_EQ(2u,reopened.Events.size());EXPECT_EQ(shape,reopened.Events.front().Text.get());
    EXPECT_NE(nullptr,reopened.GetStyle("图形"));
    auto saved=c->ass->Events.front().Text.get();
    Mouse(wxEVT_LEFT_DOWN,30,40,true);Mouse(wxEVT_MOTION,60,70,true);
    {auto write=c->subtitleAccess->TryWrite();c->subsController->Save(save);}
    AssFile pending;SubtitleFormat::GetReader(save,"utf-8")->ReadFile(&pending,save,c->project->Timecodes(),"utf-8");
    EXPECT_EQ(saved,pending.Events.front().Text.get());
    wxKeyEvent esc(wxEVT_CHAR_HOOK);esc.m_keyCode=WXK_ESCAPE;display->GetEventHandler()->ProcessEvent(esc);
    EXPECT_EQ(saved,c->ass->Events.front().Text.get());
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
    {auto legacy=c->subtitleAccess->TryWrite();c->ass->Events.front().Layer=7;c->ass->Events.front().Start=1;}
    EXPECT_THROW(Mouse(wxEVT_LEFT_UP,140,150),std::logic_error);
    EXPECT_EQ(text,Text());EXPECT_EQ(7,c->ass->Events.front().Layer);
    EXPECT_EQ(1,c->ass->Events.front().Start.GetMilliseconds());
    EXPECT_TRUE(c->subtitleEditGate.TryBeginComparison());
}
}
