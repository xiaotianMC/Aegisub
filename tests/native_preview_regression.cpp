// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Runs against the native application objects, without injecting mouse/keyboard input.
#include "main.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "command/command.h"
#include "dialog_rectangle.h"
#include "include/aegisub/context.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subs_edit_ctrl.h"
#include "video_display.h"
#include "visual_tool.h"

#include <cstdio>
#include <stdexcept>
#include <wx/dialog.h>
#include <wx/spinctrl.h>

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T> static T *find_child(wxWindow *parent) {
    if (auto child = dynamic_cast<T*>(parent)) return child;
    for (auto child : parent->GetChildren())
        if (auto result = find_child<T>(child)) return result;
    return nullptr;
}

static void edit(agi::Context& c, const std::function<void(wxDialog&, wxSpinCtrlDouble&)>& action) {
    std::exception_ptr failure;
    wxTheApp->CallAfter([&] {
        wxDialog *dialog = nullptr;
        for (auto window : wxTopLevelWindows)
            if (auto candidate = dynamic_cast<wxDialog*>(window); candidate && candidate->IsModal())
                dialog = candidate;
        try {
            require(dialog, "shape dialog did not open");
            auto width = find_child<wxSpinCtrlDouble>(dialog);
            require(width, "shape width field missing");
            action(*dialog, *width);
        }
        catch (...) {
            failure = std::current_exception();
            if (dialog) dialog->EndModal(wxID_CANCEL);
        }
    });
    EditDrawing(&c);
    if (failure) std::rethrow_exception(failure);
}

static void set_width(wxSpinCtrlDouble& field, const wxString& value) {
    field.SetValue(value);
    wxCommandEvent changed(wxEVT_TEXT, field.GetId());
    changed.SetEventObject(&field);
    field.ProcessWindowEvent(changed);
}

static void mouse(VideoDisplay& display, wxEventType type, wxPoint position, bool left) {
    wxMouseEvent event(type);
    event.SetPosition(position);
    event.SetLeftDown(left);
    event.SetEventObject(&display);
    display.ProcessWindowEvent(event);
}

int main(int argc, char **argv) {
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    int result = 0;
    try {
        auto& c = wxGetApp().NewProjectContext();
        c.project->LoadSubtitles("../tests/data/shape-editor-test.ass", "UTF-8", false);
        auto line = c.selectionController->GetActiveLine();
        const auto original = line->Text;
        auto editor = find_child<SubsTextEditCtrl>(c.parent);
        require(editor, "native subtitle editor missing");
        auto synced = [&] {
            require(std::string(editor->GetTextRaw().data()) == line->Text.get(), "subtitle editor did not synchronize");
        };
        edit(c, [&](wxDialog& dialog, wxSpinCtrlDouble& width) {
            set_width(width, "180");
            require(line->Text.get().find("\\fscx150") != std::string::npos, "width did not update while dialog was open");
            synced();
            require(!c.subsController->IsModified(), "preview changed saved state");
            require(c.subsController->GetUndoDescription().empty(), "preview added undo history");
            set_width(width, "120");
            require(line->Text == original, "returning to original width changed text");
            set_width(width, "180");
            synced();
            dialog.EndModal(wxID_CANCEL);
        });
        require(line->Text == original, "cancel did not restore text");
        synced();
        require(!c.subsController->IsModified(), "cancel changed saved state");
        edit(c, [&](wxDialog& dialog, wxSpinCtrlDouble& width) {
            set_width(width, "180");
            const auto valid = line->Text;
            set_width(width, "-1");
            require(line->Text == valid, "invalid width changed subtitle");
            set_width(width, "240");
            synced();
            dialog.EndModal(wxID_OK);
        });
        require(c.subsController->IsModified(), "confirmation did not create an edit");
        c.subsController->Undo();
        line = c.selectionController->GetActiveLine();
        require(line->Text == original, "one undo did not restore the whole edit");
        synced();
        require(!c.subsController->IsModified(), "undo did not restore saved state");

        c.project->LoadVideo("?dummy:24:120:640:360:48:160:240:");
        auto display = c.videoDisplay;
        cmd::call("video/tool/cross", &c);
        display->SetWindowZoom(5);
        wxYield();
        mouse(*display, wxEVT_LEFT_DOWN, {100, 70}, true);
        require(display->HasCapture(), "large window zoom did not start panning");
        mouse(*display, wxEVT_LEFT_UP, {100, 70}, false);
        display->SetWindowZoom(.5);
        display->SetMinClientSize(wxSize(32, 32));
        display->SetClientSize(wxSize(200, 120));
        wxSizeEvent resize(display->GetClientSize());
        display->ProcessWindowEvent(resize);
        const auto unchanged = line->Text;
        mouse(*display, wxEVT_LEFT_DOWN, {100, 70}, true);
        const auto anchor = display->GetMousePosition();
        require(display->HasCapture(), "cropped video did not start panning");
        mouse(*display, wxEVT_MOTION, {60, 45}, true);
        require(display->IsContentZoomActive(), "video did not pan");
        require(display->HasCapture(), "visual tool released panning capture");
        require((display->GetMousePosition() - anchor).Len() < .01, "panning moved the video in the wrong direction");
        mouse(*display, wxEVT_LEFT_UP, {60, 45}, false);
        require(!display->HasCapture(), "panning did not release capture");
        require(line->Text == unchanged, "panning changed subtitle position");
        mouse(*display, wxEVT_LEFT_DOWN, {60, 45}, true);
        display->ReleaseMouse();
        wxMouseCaptureLostEvent lost(display->GetId());
        display->ProcessWindowEvent(lost);
        mouse(*display, wxEVT_MOTION, {80, 60}, true);
        require(!display->HasCapture(), "lost capture resumed a stale drag");
        mouse(*display, wxEVT_LEFT_UP, {80, 60}, false);
        cmd::call("video/tool/rectangle", &c);
        wxMouseEvent middle_down(wxEVT_MIDDLE_DOWN);
        middle_down.SetPosition({100, 70});
        middle_down.SetMiddleDown(true);
        display->ProcessWindowEvent(middle_down);
        wxMouseEvent middle_drag(wxEVT_MOTION);
        middle_drag.SetPosition({80, 60});
        middle_drag.SetMiddleDown(true);
        display->ProcessWindowEvent(middle_drag);
        require(display->HasCapture(), "drawing tool released middle-button pan capture");
        wxMouseEvent middle_up(wxEVT_MIDDLE_UP);
        middle_up.SetPosition({80, 60});
        display->ProcessWindowEvent(middle_up);
        require(!display->HasCapture(), "middle-button pan did not release capture");
        cmd::call("video/tool/cross", &c);
        display->ResetContentZoom();
        display->SetClientSize(wxSize(320, 180));
        display->ProcessWindowEvent(resize);
        mouse(*display, wxEVT_LEFT_DOWN, {100, 70}, true);
        require(!display->HasCapture(), "fitting video still intercepted the left button");
        mouse(*display, wxEVT_LEFT_UP, {100, 70}, false);
        std::puts("PASS: live synchronization, cancel, invalid input, single undo, cropped-video panning, capture release and fitting video");
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        result = 1;
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
