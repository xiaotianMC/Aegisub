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

#include <wx/app.h>
#include <wx/dcclient.h>
#include <wx/graphics.h>

#ifdef __WXMSW__
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

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
		// Whatever share comes next is shown, even the one shown before
		progress = -1;
		Main().Async([this]{ dialog->SetIndeterminate(); });
	}
};

DialogProgress::DialogProgress(wxWindow *parent, wxString const& title_text, wxString const& message)
{
	// A card with round corners: where the system composites windows, the
	// window sees through about it and the card casts a shadow; elsewhere
	// it is the window, with a thin line round it (and on Windows 11 the
	// system's own round corners and shadow)
	// Asked of a window made already: this one has to know before it is
#ifdef __WXMSW__
	// wxMSW says yes, but for a top-level window wxBG_STYLE_TRANSPARENT only
	// leaves the background unerased: the room for the shadow would show
	// whatever was on the screen when the window opened, inside the system's
	// round border drawn about the whole window. The window is the card here.
	see_through = false;
#else
	wxWindow *made = parent ? parent : wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
	see_through = made && made->IsTransparentBackgroundSupported();
#endif
	SetBackgroundStyle(see_through ? wxBG_STYLE_TRANSPARENT : wxBG_STYLE_PAINT);
	Create(parent, -1, title_text, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
#ifdef __WXMSW__
	// DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND: ignored before Windows 11
	DWORD round = 2;
	DwmSetWindowAttribute(GetHWND(), 33, &round, sizeof(round));
#endif
	if (see_through) shadow = FromDIP(18);
	Bind(wxEVT_PAINT, &DialogProgress::OnPaint, this);

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

	wxSizer *sizer = content = new wxBoxSizer(wxVERTICAL);
	sizer->Add(title, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, pad));
	sizer->Add(text, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(4)).Border(wxLEFT | wxRIGHT, pad));
	sizer->AddSpacer(FromDIP(16));
	sizer->Add(bar, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, pad));
	sizer->AddSpacer(FromDIP(6));
	sizer->Add(numbers, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, pad));
	sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, pad));
	sizer->Add(log_output, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, pad));
	sizer->Hide(log_output);

	auto outer = new wxBoxSizer(wxVERTICAL);
	outer->Add(sizer, wxSizerFlags(1).Expand().Border(wxALL, shadow));
	SetSizerAndFit(outer);
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

	if (content->IsShown(log_output)) {
		content->Hide(log_output);
		Refit();
		log_output->Clear();
	}
}

void DialogProgress::Refit() {
	Layout();
	GetSizer()->Fit(this);
	Refresh();
}

void DialogProgress::OnPaint(wxPaintEvent&) {
	wxPaintDC dc(this);
	auto p = flat::Colours(this);
	bool const dark = theme::IsDark();
	wxColour const edge = theme::Mix(p.surface, p.text, dark ? 0.16 : 0.12);
	wxRect card(GetClientSize());
	card.Deflate(shadow);
	int const thick = std::max(1, FromDIP(1));
	std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
	if (!gc) {
		dc.SetPen(wxPen(edge, thick));
		dc.SetBrush(wxBrush(p.surface));
		dc.DrawRectangle(card);
		return;
	}
	gc->SetPen(wxNullGraphicsPen);
	// Square where the window is the card: the system rounds it, if anything
	double const radius = see_through ? FromDIP(12) : 0;

	if (see_through) {
		// Soft, and a little below the card: rings each adding a little
		// dark, closer together near the card, so it is deepest there and
		// fades out a long way
		int const steps = 24;
		double const drop = FromDIP(5);
		int const alpha = std::max(1, static_cast<int>(std::lround(255 * (dark ? 0.45 : 0.13) / steps)));
		gc->SetBrush(wxBrush(wxColour(0, 0, 0, alpha)));
		for (int i = steps; i > 0; --i) {
			double const t = static_cast<double>(i) / steps;
			double const grow = shadow * std::pow(t, 1.7);
			gc->DrawRoundedRectangle(card.x - grow, card.y - grow + drop * (1 - t) + FromDIP(2),
				card.width + 2 * grow, card.height + 2 * grow, radius + grow);
		}
	}

	gc->SetBrush(wxBrush(p.surface));
	gc->DrawRoundedRectangle(card.x, card.y, card.width, card.height, radius);
	// A hairline round it, all inside, so the card stands off a background
	// of its own colour
	auto ring = gc->CreatePath();
	ring.AddRoundedRectangle(card.x, card.y, card.width, card.height, radius);
	ring.AddRoundedRectangle(card.x + thick, card.y + thick, card.width - 2 * thick, card.height - 2 * thick, std::max(0., radius - thick));
	gc->SetBrush(wxBrush(edge));
	gc->FillPath(ring, wxODDEVEN_RULE);
}

void DialogProgress::OnIdle(wxIdleEvent&) {
	if (!pending_log) return;

	if (log_output->IsEmpty()) {
		content->Show(log_output);
		Refit();
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
