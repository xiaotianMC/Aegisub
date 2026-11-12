// Copyright (c) 2005, Rodrigo Braz Monteiro, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file frame_main.cpp
/// @brief Main window creation and control management
/// @ingroup main_ui

#include "frame_main.h"

#include "include/aegisub/context.h"
#include "include/aegisub/menu.h"
#include "include/aegisub/toolbar.h"
#include "include/aegisub/hotkey.h"

#include "ass_file.h"
#include "subtitle_project_access.h"
#include "async_video_provider.h"
#include "audio_controller.h"
#include "audio_box.h"
#include "base_grid.h"
#include "compat.h"
#include "command/command.h"
#include "dialog_detached_video.h"
#include "dialog_manager.h"
#include "libresrc/libresrc.h"
#include "main.h"
#include "options.h"
#include "project.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "status_info.h"
#include "script_panel.h"
#include "style_panel.h"
#include "utils.h"
#include "version.h"
#include "video_box.h"
#include "theme.h"
#include "workspace_tabs.h"
#include "video_controller.h"
#include "video_display.h"
#include "selection_controller.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/log.h>

#include <wx/dnd.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/statusbr.h>
#include <wx/sysopt.h>
#include <wx/toolbar.h>

enum {
	ID_APP_TIMER_STATUSCLEAR = 12002
};

#ifdef WITH_STARTUPLOG
#define StartupLog(a) MessageBox(0, a, "Aegisub startup log", 0)
#else
#define StartupLog(a) LOG_I("frame_main/init") << a
#endif

/// Handle files drag and dropped onto Aegisub
class AegisubFileDropTarget final : public wxFileDropTarget {
	agi::Context *context;
public:
	AegisubFileDropTarget(agi::Context *context) : context(context) { }
	bool OnDropFiles(wxCoord, wxCoord, wxArrayString const& filenames) override {
		std::vector<agi::fs::path> files;
		for (wxString const& fn : filenames)
			files.push_back(from_wx(fn));
		agi::dispatch::Main().Async([=, this] { context->project->LoadList(files, true); });
		return true;
	}
};

FrameMain::FrameMain()
: wxFrame(nullptr, -1, "")
, context(std::make_unique<agi::Context>())
{
	StartupLog("Entering FrameMain constructor");

	SetSize(FromDIP(wxSize(920, 700)));

#ifdef __WXGTK__
	// XXX HACK XXX
	// We need to set LC_ALL to "" here for input methods to work reliably.
	setlocale(LC_ALL, "");

	// However LC_NUMERIC must be "C", otherwise some parsing fails.
	setlocale(LC_NUMERIC, "C");
#endif

	StartupLog("Initializing context controls");
	BindConnection(context->subtitleAccess->Read().AddCommitListener("frame_main.AddCommitListener", [this](int, AssDialogue const*, SubtitleModelRebindTicket ticket) { UpdateTitle(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)));
    BindConnection(context->subtitleAccess->AddMetadataListener(&FrameMain::UpdateTitle, this));
	BindConnection(context->subsController->AddFileOpenListener(&FrameMain::OnSubtitlesOpen, this));
	BindConnection(context->subsController->AddFileSaveListener(&FrameMain::UpdateTitle, this));
	BindConnection(context->project->AddAudioProviderListener(&FrameMain::OnAudioOpen, this));
	BindConnection(context->project->AddVideoProviderListener(&FrameMain::OnVideoOpen, this));

	StartupLog("Initializing context frames");
	context->parent = this;
	context->frame = this;

	StartupLog("Apply saved Maximized state");
	if (OPT_GET("App/Maximized")->GetBool()) Maximize(true);

	StartupLog("Initialize toolbar");
	wxSystemOptions::SetOption("msw.remap", 0);
	BindConnection(OPT_SUB("App/Show Toolbar", &FrameMain::EnableToolBar, this));
	EnableToolBar(*OPT_GET("App/Show Toolbar"));

	StartupLog("Initialize menu bar");
	menu::GetMenuBar("main", this, (wxID_HIGHEST + 1) + 10000, context.get());

	StartupLog("Create status bar");
	// Menu help, timed messages, the current line, the file's state.
	CreateStatusBar(4);
	{
		int widths[] = {-3, -2, -3, -1};
		GetStatusBar()->SetStatusWidths(4, widths);
	}

	StartupLog("Set icon");
#ifdef _WIN32
	SetIcon(wxICON(wxicon));
#else
	wxIcon icon;
	icon.CopyFromBitmap(GETIMAGE(wxicon));
	SetIcon(icon);
#endif

	StartupLog("Create views and inner main window controls");
	InitContents();
	BindConnection(OPT_SUB("Video/Detached/Enabled", &FrameMain::OnVideoDetach, this));

	StartupLog("Set up drag/drop target");
	SetDropTarget(new AegisubFileDropTarget(context.get()));
	Bind(wxEVT_SYS_COLOUR_CHANGED, [](wxSysColourChangedEvent& event) { theme::SystemColoursChanged(); event.Skip(); });

	StartupLog("Load default file");
	context->project->CloseSubtitles();

	StartupLog("Display main window");
	AddFullScreenButton(this);
	Show();
	SetDisplayMode(1, 1);

	StartupLog("Leaving FrameMain constructor");
}

FrameMain::~FrameMain () {
    context->project->BeginHostShutdown();
	context->project->CloseAudio();
	context->project->CloseVideo();

	DestroyChildren();
}
bool FrameMain::Destroy() {
    if (context && context->subsController && context->subsController->IsInstalling()) {
        context->subsController->CancelModelInstallation();
        return false; // Also cover direct deferred Destroy, not just close events.
    }
    return wxFrame::Destroy();
}

void FrameMain::EnableToolBar(agi::OptionValue const& opt) {
	if (opt.GetBool()) {
		if (!GetToolBar()) {
			toolbar::AttachToolbar(this, "main", context.get(), "Default");
			GetToolBar()->Realize();
		}
	}
	else if (wxToolBar *old_tb = GetToolBar()) {
		SetToolBar(nullptr);
		delete old_tb;
		Layout();
	}
}

void FrameMain::InitContents() {
	StartupLog("Create background panel");
	// The editor is the first tab; a comparison opens beside it.
	auto tabs = new WorkspaceTabs(this, context.get());
	context->workspaceTabs = tabs;
	auto Panel = new wxPanel(tabs->Book(), -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN);

	StartupLog("Create subtitles grid");
	context->subsGrid = new BaseGrid(Panel, context.get());

	StartupLog("Create video box");
	videoBox = new VideoBox(Panel, false, context.get());

	StartupLog("Create audio box");
	context->audioBox = audioBox = new AudioBox(Panel, context.get());

	StartupLog("Create subtitle editing box");
	auto EditBox = editBox = new SubsEditBox(Panel, context.get());

	StartupLog("Arrange main sizers");
	// Blocks on a backdrop, the same gap everywhere and no borders
	// (docs/theme.md). The audio box's resize strip is the gap below it.
	theme::UseBackdrop(Panel);
	const int gap = theme::BlockGap(Panel);
	ToolsSizer = new wxBoxSizer(wxVERTICAL);
	ToolsSizer->Add(audioBox, 0, wxEXPAND);
	ToolsSizer->Add(EditBox, 1, wxEXPAND);
	TopSizer = new wxBoxSizer(wxHORIZONTAL);
	TopSizer->Add(videoBox, 0, wxEXPAND, 0);
	TopSizer->Add(ToolsSizer, 1, wxEXPAND | wxLEFT, gap);
	// The visual tools sit in the middle between the video and the audio
	videoBox->SetToolbarGap(gap);
	MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(TopSizer, 0, wxEXPAND | wxLEFT | wxTOP, gap);
	MainSizer->Add(context->subsGrid, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, gap);
	// The style panel takes the whole height at the right, or the script
	// panel in its place (docs/style-panel.md); the gap before it is its
	// resize strip
	stylePanel = new StylePanel(Panel, context.get());
	scriptPanel = new ScriptPanel(Panel, context.get());
	auto edge = panelEdge = new StylePanelEdge(Panel, stylePanel, scriptPanel);
	OuterSizer = new wxBoxSizer(wxHORIZONTAL);
	OuterSizer->Add(MainSizer, 1, wxEXPAND);
	OuterSizer->Add(edge, 0, wxEXPAND);
	OuterSizer->Add(stylePanel, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, gap);
	OuterSizer->Add(scriptPanel, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, gap);
	Panel->SetSizer(OuterSizer);
	edge->Update();
	tabs->SetEditor(Panel);
	// The comparison's tabs at the bottom left, in the status bar
	if (GetStatusBar()) tabs->MoveStripTo(GetStatusBar());
	BindConnection(tabs->AddChangeListener(&FrameMain::UpdateStatus, this));
	BindConnection(context->selectionController->AddActiveLineListener("frame_main.AddActiveLineListener", [this](SubtitleModelRebindTicket ticket) { UpdateStatus(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)));
	BindConnection(context->selectionController->AddSelectionListener("frame_main.AddSelectionListener", [this](SubtitleModelRebindTicket ticket) { UpdateStatus(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)));

	StartupLog("Perform layout");
	Layout();
	StartupLog("Leaving InitContents");
}

void FrameMain::SetDisplayMode(int video, int audio) {
	if (!IsShownOnScreen()) return;

	bool sv = false, sa = false;

	if (video == -1) sv = showVideo;
	else if (video)  sv = context->project->VideoProvider() && !context->dialog->Get<DialogDetachedVideo>();

	if (audio == -1) sa = showAudio;
	else if (audio)  sa = !!context->project->AudioProvider();

	// See if anything changed
	if (sv == showVideo && sa == showAudio) return;

	showVideo = sv;
	showAudio = sa;

	bool didFreeze = !IsFrozen();
	if (didFreeze) Freeze();

	context->videoController->Stop();

	TopSizer->Show(videoBox, showVideo, true);
	ToolsSizer->Show(audioBox, showAudio, true);

	MainSizer->Layout();
	Layout();

	if (didFreeze) Thaw();
}

void FrameMain::UpdateStatus() {
	if (!GetStatusBar() || GetStatusBar()->GetFieldsCount() < 4) return;
	SetStatusText(status_info::Line(context.get()), 2);
	SetStatusText(status_info::File(context.get()), 3);
}

void FrameMain::UpdateTitle() {
	UpdateStatus();
	wxString newTitle;
	if (context->subsController->IsModified()) newTitle << "* ";
	newTitle << context->subsController->Filename().filename().wstring();

#ifndef __WXMAC__
	newTitle << " - Aegisub " << GetAegisubLongVersionString();
#endif

#if defined(__WXMAC__)
	// On Mac, set the mark in the close button
	OSXSetModified(context->subsController->IsModified());
#endif

	if (GetTitle() != newTitle) SetTitle(newTitle);
}

void FrameMain::OnVideoOpen(AsyncVideoProvider *provider) {
	if (!provider) {
		SetDisplayMode(0, -1);
		return;
	}

	Freeze();
	int vidx = provider->GetWidth(), vidy = provider->GetHeight();

	// Set zoom level based on video resolution and window size
	double zoom = context->videoDisplay->GetWindowZoom();
	wxSize windowSize = GetSize();
	if (vidx*3*zoom > windowSize.GetX()*4 || vidy*4*zoom > windowSize.GetY()*6)
		context->videoDisplay->SetWindowZoom(zoom * .25);
	else if (vidx*3*zoom > windowSize.GetX()*2 || vidy*4*zoom > windowSize.GetY()*3)
		context->videoDisplay->SetWindowZoom(zoom * .5);

	SetDisplayMode(1,-1);

	if (OPT_GET("Video/Detached/Enabled")->GetBool() && !context->dialog->Get<DialogDetachedVideo>())
		cmd::call("video/detach", context.get());
	Thaw();
}

void FrameMain::OnVideoDetach(agi::OptionValue const& opt) {
	if (opt.GetBool())
		SetDisplayMode(0, -1);
	else if (context->project->VideoProvider())
		SetDisplayMode(1, -1);
}

void FrameMain::StatusTimeout(wxString text,int ms) {
	SetStatusText(text,1);
	StatusClear.SetOwner(this, ID_APP_TIMER_STATUSCLEAR);
	StatusClear.Start(ms,true);
}

BEGIN_EVENT_TABLE(FrameMain, wxFrame)
	EVT_TIMER(ID_APP_TIMER_STATUSCLEAR, FrameMain::OnStatusClear)
	EVT_CLOSE(FrameMain::OnCloseWindow)
	EVT_CHAR_HOOK(FrameMain::OnKeyDown)
	EVT_MOUSEWHEEL(FrameMain::OnMouseWheel)
END_EVENT_TABLE()

void FrameMain::OnCloseWindow(wxCloseEvent &event) {
    if (context->subsController->DeferHostClose(event)) return;
	wxEventBlocker blocker(this, wxEVT_CLOSE_WINDOW);

	context->videoController->Stop();
	context->audioController->Stop();

	// An open comparison ends first: it holds the subtitles.
	if (context->workspaceTabs && !context->workspaceTabs->CloseComparison(event.CanVeto())) {
		event.Veto();
		return;
	}

	// Ask user if he wants to save first
	if (context->subsController->TryToClose(event.CanVeto()) == wxCANCEL) {
		event.Veto();
		return;
	}

	context->dialog.reset();

	// Store maximization state
	OPT_SET("App/Maximized")->SetBool(IsMaximized());

	Destroy();
}

void FrameMain::OnStatusClear(wxTimerEvent &) {
	SetStatusText("",1);
}

void FrameMain::OnAudioOpen(agi::AudioProvider *provider) {
	if (provider)
		SetDisplayMode(-1, 1);
	else
		SetDisplayMode(-1, 0);
}

void FrameMain::OnSubtitlesOpen() {
	UpdateTitle();
	SetDisplayMode(1, 1);
}

void FrameMain::OnKeyDown(wxKeyEvent &event) {
	hotkey::check("Main Frame", context.get(), event);
}

void FrameMain::OnMouseWheel(wxMouseEvent &evt) {
	ForwardMouseWheelEvent(this, evt);
}

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>

WXLRESULT FrameMain::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
	// Alt + wheel zooms the audio: releasing Alt afterwards must not select
	// the menu bar, or the next wheel turns still count as Alt + wheel.
	if (message == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_KEYMENU && lParam == 0 && TakeAltUsedForWheel())
		return 0;
	return wxFrame::MSWWindowProc(message, wParam, lParam);
}
#endif
