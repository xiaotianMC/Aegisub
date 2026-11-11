// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

/// @file dialog_progress.cpp
/// @brief Progress-bar dialog box for displaying during long operations
/// @ingroup utility
///

#include "dialog_progress.h"
#include "theme.h"

#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "utils.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/util_osx.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef _MSC_VER
#include <shobjidl.h>
#endif

using agi::dispatch::Main;

namespace {
	void set_taskbar_progress([[maybe_unused]] int progress) {
#ifdef _MSC_VER
		ITaskbarList3 *taskbar;
		auto hr = ::CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
			__uuidof(ITaskbarList3), (LPVOID *)&taskbar);
		if (FAILED(hr)) return;

		hr = taskbar->HrInit();
		if (FAILED(hr)) {
			taskbar->Release();
			return;
		}

		auto hwnd = wxTheApp->GetTopWindow()->GetHWND();
		if (progress == 0 || progress == 100)
			taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
		else if (progress == -1)
			taskbar->SetProgressState(hwnd, TBPF_INDETERMINATE);
		else
			taskbar->SetProgressValue(hwnd, progress, 100);

		taskbar->Release();
#endif
	}

	/// About how long @p seconds is, as minutes and seconds
	wxString Duration(int seconds) {
		if (seconds < 60) return wxString::Format(_("%d s"), std::max(1, seconds));
		return wxString::Format(_("%d min %d s"), seconds / 60, seconds % 60);
	}

	/// A sweep across the bar while not known how far
	constexpr double sweep_seconds = 1.6;
	/// How quickly the bar glides to a new value: most of the way in this
	constexpr double glide_seconds = 0.18;
}

ProgressBar::ProgressBar(wxWindow *parent)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
, timer(this)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(GetBestSize());
	Bind(wxEVT_PAINT, &ProgressBar::OnPaint, this);
	Bind(wxEVT_TIMER, &ProgressBar::OnTimer, this);
	theme_changed = theme::AddChangeListener([this] { Refresh(); });
}

wxSize ProgressBar::DoGetBestClientSize() const {
	return FromDIP(wxSize(360, 6));
}

void ProgressBar::SetValue(double share) {
	share = std::clamp(share, 0.0, 1.0);
	bool was_sweeping = indeterminate;
	indeterminate = false;
	if (share < target || was_sweeping) shown = share;
	target = share;
	Animate();
	Refresh();
}

void ProgressBar::SetIndeterminate() {
	if (indeterminate) return;
	indeterminate = true;
	sweep_start = std::chrono::steady_clock::now();
	Animate();
	Refresh();
}

void ProgressBar::Animate() {
	bool moving = indeterminate || std::abs(target - shown) > 0.0005;
	if (moving && !timer.IsRunning()) {
		last_tick = std::chrono::steady_clock::now();
		timer.Start(16);
	}
	else if (!moving)
		timer.Stop();
}

void ProgressBar::OnTimer(wxTimerEvent &) {
	auto now = std::chrono::steady_clock::now();
	double dt = std::chrono::duration<double>(now - last_tick).count();
	last_tick = now;
	if (!indeterminate) {
		shown += (target - shown) * (1 - std::exp(-dt / glide_seconds * 3));
		if (std::abs(target - shown) <= 0.0005) shown = target;
	}
	Animate();
	Refresh();
}

void ProgressBar::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto p = flat::Colours(this);
	wxSize size = GetClientSize();
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(p.surface));
	dc.DrawRectangle(wxPoint(0, 0), size);

	int h = std::min(size.y, FromDIP(6));
	wxRect track(0, (size.y - h) / 2, size.x, h);
	double radius = h / 2.0;
	dc.SetBrush(wxBrush(theme::Mix(p.surface, p.text, theme::IsDark() ? 0.16 : 0.1)));
	dc.DrawRoundedRectangle(track, radius);

	dc.SetBrush(wxBrush(p.accent));
	if (indeterminate) {
		// A piece a third of the track long, easing in and out as it goes
		// from wholly off its left end to wholly off its right
		double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - sweep_start).count();
		double phase = std::fmod(t / sweep_seconds, 1.0);
		double eased = phase < 0.5 ? 2 * phase * phase : 1 - 2 * (1 - phase) * (1 - phase);
		int piece = track.width / 3;
		int x = int(-piece + (track.width + piece) * eased);
		int left = std::max(0, x), right = std::min(track.width, x + piece);
		if (right - left > 0) {
			dc.SetClippingRegion(track);
			dc.DrawRoundedRectangle(wxRect(left, track.y, right - left, h), radius);
			dc.DestroyClippingRegion();
		}
		return;
	}
	if (shown <= 0) return;
	// Never narrower than its rounded ends
	int width = std::max(h, int(std::lround(track.width * shown)));
	dc.DrawRoundedRectangle(wxRect(track.x, track.y, width, h), radius);
}

class DialogProgressSink final : public agi::ProgressSink {
	DialogProgress *dialog;
	std::atomic<bool> cancelled{false};
	int progress = 0;

public:
	DialogProgressSink(DialogProgress *dialog) : dialog(dialog) { }

	void SetTitle(std::string const& title) override {
		Main().Async([=, this]{ dialog->title->SetLabelText(to_wx(title)); });
	}

	void SetMessage(std::string const& msg) override {
		Main().Async([=, this]{ dialog->text->SetLabelText(to_wx(msg)); });
	}

	void SetProgress(int64_t cur, int64_t max) override {
		int new_progress = mid<int>(0, double(cur) / max * 300, 300);
		if (new_progress != progress) {
			progress = new_progress;
			Main().Async([=, this]{ dialog->SetProgress(new_progress); });
		}
	}

	void Log(std::string const& str) override {
		Main().Async([=, this]{ dialog->pending_log += to_wx(str); });
	}

	bool IsCancelled() override {
		return cancelled;
	}

	void Cancel() {
		cancelled = true;
	}

	void SetIndeterminate() override {
		Main().Async([this]{ dialog->SetIndeterminate(); });
	}
};

DialogProgress::DialogProgress(wxWindow *parent, wxString const& title_text, wxString const& message)
: wxDialog(parent, -1, title_text, wxDefaultPosition, wxDefaultSize, theme::Border(wxBORDER_RAISED))
{
	title = new wxStaticText(this, -1, title_text, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
	text = new wxStaticText(this, -1, message, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_MIDDLE);
	bar = new ProgressBar(this);
	percent = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
	remaining = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
	cancel_button = new FlatButton(this, _("Cancel"), wxID_CANCEL);
	cancel_button->SetLeastSize(wxSize(FromDIP(84), FromDIP(30)));
	log_output = new wxTextCtrl(this, -1, "", wxDefaultPosition, DipSize(600, 240), wxTE_MULTILINE | wxTE_READONLY);

	// The title a little larger and bold; the rest smaller and dim
	wxFont title_font = title->GetFont();
	title_font.SetFractionalPointSize(title_font.GetFractionalPointSize() * 1.2);
	title_font.SetWeight(wxFONTWEIGHT_SEMIBOLD);
	title->SetFont(title_font);
	wxFont progress_font = percent->GetFont();
	progress_font.SetFractionalPointSize(progress_font.GetFractionalPointSize() * 0.92);
	percent->SetFont(progress_font);
	remaining->SetFont(progress_font);

	theme::UseSurface(this);
	auto dim = flat::Colours(this).dim;
	text->SetForegroundColour(dim);
	percent->SetForegroundColour(dim);
	remaining->SetForegroundColour(dim);
	// Room for the longest that is written there
	percent->SetMinSize(wxSize(GetTextExtent("100%").x + FromDIP(8), -1));
	int pad = FromDIP(20);

	auto numbers = new wxBoxSizer(wxHORIZONTAL);
	numbers->Add(percent, wxSizerFlags().CenterVertical());
	numbers->Add(remaining, wxSizerFlags(1).CenterVertical());

	auto buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->AddStretchSpacer();
	buttons->Add(cancel_button);

	wxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(title, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, pad));
	sizer->Add(text, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(4)).Border(wxLEFT | wxRIGHT, pad));
	sizer->AddSpacer(FromDIP(16));
	sizer->Add(bar, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, pad));
	sizer->AddSpacer(FromDIP(6));
	sizer->Add(numbers, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, pad));
	sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, pad));
	sizer->Add(log_output, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, pad));
	sizer->Hide(log_output);

	SetSizerAndFit(sizer);
	CenterOnParent();

	// Escape presses Cancel, as it did the system's button
	flat::UseFlatButtonKeys(this);
	Bind(wxEVT_SHOW, &DialogProgress::OnShow, this);
}

void DialogProgress::Run(std::function<void(agi::ProgressSink*)> task) {
	DialogProgressSink ps(this);
	this->ps = &ps;

	auto current_title = from_wx(title->GetLabelText());
	agi::dispatch::Background().Async([=, this]{
		agi::osx::AppNapDisabler app_nap_disabler(current_title);
		try {
			task(this->ps);
		}
		catch (agi::Exception const& e) {
			this->ps->Log(e.GetMessage());
		}

		Main().Async([this]{
			Unbind(wxEVT_IDLE, &DialogProgress::OnIdle, this);

			// Unbind the cancel handler so that the default behavior happens (i.e. the
			// dialog is closed) as there's no longer a task to cancel
			Unbind(wxEVT_BUTTON, &DialogProgress::OnCancel, this, wxID_CANCEL);

			// If it ran to completion and there is debug output, leave the window open
			// so the user can read the debug output and switch the cancel button to a
			// close button
			bool cancelled = this->ps->IsCancelled();
			if (cancelled || (log_output->IsEmpty() && !pending_log))
				EndModal(!cancelled);
			else {
				if (!pending_log.empty()) {
					wxIdleEvent evt;
					OnIdle(evt);
				}
				cancel_button->SetLabel(_("Close"));
				bar->SetValue(1);
				percent->SetLabelText("100%");
				remaining->SetLabelText("");
			}
			set_taskbar_progress(0);
		});
	});

	if (!ShowModal())
		throw agi::UserCancelException("Cancelled by user");
}

void DialogProgress::OnShow(wxShowEvent& evt) {
	if (!evt.IsShown()) return;

	// Restore the cancel button in case it was previously switched to a close
	// button
	Bind(wxEVT_BUTTON, &DialogProgress::OnCancel, this, wxID_CANCEL);
	Bind(wxEVT_IDLE, &DialogProgress::OnIdle, this);
	cancel_button->SetLabel(_("Cancel"));
	cancel_button->Enable();
	progress = 0;
	progress_start = std::chrono::steady_clock::now();
	bar->SetValue(0);
	percent->SetLabelText("");
	remaining->SetLabelText("");

	wxSizer *sizer = GetSizer();
	if (sizer->IsShown(log_output)) {
		sizer->Hide(log_output);
		Layout();
		sizer->Fit(this);
		log_output->Clear();
	}
}

void DialogProgress::OnIdle(wxIdleEvent&) {
	if (!pending_log) return;

	if (log_output->IsEmpty()) {
		wxSizer *sizer = GetSizer();
		sizer->Show(log_output);
		Layout();
		sizer->Fit(this);
		CenterOnParent();
	}

	*log_output << pending_log;
	log_output->SetInsertionPointEnd();
	pending_log.clear();
}

void DialogProgress::OnCancel(wxCommandEvent &) {
	ps->Cancel();
	cancel_button->Enable(false);
	cancel_button->SetLabel(_("Cancelling..."));
}

void DialogProgress::SetProgress(int target) {
	using namespace std::chrono;
	auto now = steady_clock::now();
	// Starting over (a task's next stage): the time left is counted afresh
	if (target < progress || bar->IsIndeterminate()) progress_start = now;
	progress = target;
	bar->SetValue(target / 300.0);
	set_taskbar_progress(target / 3);
	percent->SetLabelText(wxString::Format("%d%%", target / 3));

	// How long the rest takes, at the pace so far, once there is enough of
	// it to go by
	double share = target / 300.0;
	double seconds = duration<double>(now - progress_start).count();
	if (share >= 0.03 && share < 1 && seconds >= 2)
		remaining->SetLabelText(wxString::Format(_("About %s left"), Duration(int(std::lround(seconds * (1 - share) / share)))));
	else
		remaining->SetLabelText("");
}

void DialogProgress::SetIndeterminate() {
	bar->SetIndeterminate();
	percent->SetLabelText("");
	remaining->SetLabelText("");
	set_taskbar_progress(-1);
}
