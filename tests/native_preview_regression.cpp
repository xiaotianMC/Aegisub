// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Runs against the native application objects, without injecting mouse/keyboard input.
#include "main.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "base_grid.h"
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
            require(line->Text.get().find("l 180 0 180 60 0 60") != std::string::npos, "width did not scale the path while dialog was open");
            require(line->Text.get().find("\\fscx") == std::string::npos && line->Text.get().find("\\fscy") == std::string::npos,
                "resizing still left fscx/fscy tags");
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

        // Double-clicking a drawing row remains an entry point to the shape panel.
        auto grid = c.subsGrid;
        require(grid, "subtitle grid missing");
        grid->SetClientSize(wxSize(600, 400));
        wxSizeEvent grid_resize(grid->GetClientSize());
        grid->ProcessWindowEvent(grid_resize);
        wxYield();
        bool dialog_opened = false;
        wxTheApp->CallAfter([&] {
            for (auto window : wxTopLevelWindows) {
                auto dialog = dynamic_cast<wxDialog*>(window);
                if (dialog && dialog->IsModal()) {
                    dialog_opened = true;
                    dialog->EndModal(wxID_CANCEL);
                }
            }
        });
        auto grid_click = [&](wxEventType type) {
            wxMouseEvent event(type);
            event.SetPosition({200, 30});
            event.SetEventObject(grid);
            if (type == wxEVT_LEFT_DOWN)
                event.SetLeftDown(true);
            grid->ProcessWindowEvent(event);
        };
        grid_click(wxEVT_LEFT_DOWN);
        grid_click(wxEVT_LEFT_UP);
        grid_click(wxEVT_LEFT_DCLICK);
        wxYield();
        require(dialog_opened, "double click on a drawing row did not open the shape panel");

        c.project->LoadVideo("?dummy:24:120:640:360:48:160:240:");
        auto display = c.videoDisplay;
        cmd::call("video/tool/cross", &c);
        display->SetWindowZoom(5);
        wxYield();
        // The default tool no longer hijacks the left button just because the
        // preview is cropped; panning is an explicit sidebar mode.
        mouse(*display, wxEVT_LEFT_DOWN, {100, 70}, true);
        require(!display->HasCapture(), "default tool still captured the left button");
        mouse(*display, wxEVT_LEFT_UP, {100, 70}, false);

        cmd::call("video/tool/pan", &c);
        require(display->IsPanMode(), "pan command did not enable pan mode");
        const auto unchanged = line->Text;
        mouse(*display, wxEVT_LEFT_DOWN, {100, 70}, true);
        const auto anchor = display->GetMousePosition();
        require(display->HasCapture(), "pan mode did not capture the left button");
        mouse(*display, wxEVT_MOTION, {60, 45}, true);
        require(display->IsContentZoomActive(), "pan mode did not pan the video");
        require(display->HasCapture(), "visual tool released panning capture");
        require((display->GetMousePosition() - anchor).Len() < .01, "panning moved the video in the wrong direction");
        mouse(*display, wxEVT_LEFT_UP, {60, 45}, false);
        require(!display->HasCapture(), "panning did not release capture");
        require(display->IsPanMode(), "releasing the button left pan mode");
        require(line->Text == unchanged, "panning changed subtitle position");

        mouse(*display, wxEVT_LEFT_DOWN, {60, 45}, true);
        display->ReleaseMouse();
        wxMouseCaptureLostEvent lost(display->GetId());
        display->ProcessWindowEvent(lost);
        mouse(*display, wxEVT_MOTION, {80, 60}, true);
        require(!display->HasCapture(), "lost capture resumed a stale drag");
        mouse(*display, wxEVT_LEFT_UP, {80, 60}, false);

        // Leaving pan mode restores the original fitted size and centering.
        cmd::call("video/tool/pan", &c);
        require(!display->IsPanMode(), "pan command did not disable pan mode");
        require(!display->IsContentZoomActive(), "leaving pan mode did not restore the original size");

        // Middle-button panning remains available in every tool.
        cmd::call("video/tool/rectangle", &c);
        require(!display->IsPanMode(), "selecting another tool kept pan mode");
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
        wxSizeEvent resize(display->GetClientSize());
        display->ProcessWindowEvent(resize);
        mouse(*display, wxEVT_LEFT_DOWN, {100, 70}, true);
        require(!display->HasCapture(), "fitting video still intercepted the left button");
        mouse(*display, wxEVT_LEFT_UP, {100, 70}, false);
        std::puts("PASS: live synchronization, cancel, invalid input, single undo, explicit pan mode, capture release and size restore");
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        result = 1;
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
