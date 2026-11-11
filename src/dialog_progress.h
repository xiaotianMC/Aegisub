// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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

/// @file dialog_progress.h
/// @see dialog_progress.cpp
/// @ingroup utility
///

#pragma once

#include <chrono>
#include <wx/dialog.h>
#include <wx/timer.h>

#include <libaegisub/background_runner.h>
#include <libaegisub/signal.h>

class DialogProgressSink;
class FlatButton;
class wxStaticText;
class wxTextCtrl;

/// The progress dialog's bar: a thin rounded track the accent colour fills,
/// gliding to each new value instead of jumping there; while how far the
/// task has got is not known, a short piece of it sweeps across instead.
class ProgressBar final : public wxWindow {
	wxTimer timer;
	double shown = 0, target = 0;
	bool indeterminate = false;
	std::chrono::steady_clock::time_point last_tick, sweep_start;
	agi::signal::Connection theme_changed;

	void OnPaint(wxPaintEvent &);
	void OnTimer(wxTimerEvent &);
	void Animate();

public:
	explicit ProgressBar(wxWindow *parent);

	/// How far the task has got, 0 to 1. Moving back (a new stage of the
	/// task) is shown at once.
	void SetValue(double share);
	double GetValue() const { return target; }
	/// The share drawn now, as it glides to the value
	double GetShown() const { return shown; }
	/// Not known how far the task has got, until a value is set
	void SetIndeterminate();
	bool IsIndeterminate() const { return indeterminate; }

	wxSize DoGetBestClientSize() const override;
	bool AcceptsFocus() const override { return false; }
};

/// @class DialogProgress
/// @brief Progress-bar dialog box for displaying during long operations
class DialogProgress final : public wxDialog, public agi::BackgroundRunner {
	friend class DialogProgressSink;
	DialogProgressSink *ps;

	wxStaticText *title;
	wxStaticText *text;
	ProgressBar *bar;
	/// How far, in per cent, and about how long is left, under the bar
	wxStaticText *percent;
	wxStaticText *remaining;
	FlatButton *cancel_button;
	wxTextCtrl *log_output;
	/// What is laid out on the card, inside the room left for its shadow
	wxSizer *content = nullptr;
	/// The window's corners see through to what is behind it, and a soft
	/// shadow falls about the card, where the system can show that
	bool see_through = false;
	/// How far the shadow reaches out from the card, 0 without one
	int shadow = 0;

	wxString pending_log;
	/// Of 300; the time the task was last seen at its start, to tell how
	/// long the rest will take
	int progress = 0;
	std::chrono::steady_clock::time_point progress_start;

	void OnShow(wxShowEvent&);
	void OnCancel(wxCommandEvent &);
	void OnIdle(wxIdleEvent&);
	void OnPaint(wxPaintEvent&);
	/// Lay the content out again and fit the window round it
	void Refit();

	void SetProgress(int target);
	void SetIndeterminate();

public:
	/// Constructor
	/// @param parent Parent window of the dialog
	/// @param title Initial title of the dialog
	/// @param message Initial message of the dialog
	DialogProgress(wxWindow *parent, wxString const& title="", wxString const& message="");

	/// BackgroundWorker implementation
	void Run(std::function<void(agi::ProgressSink *)> task) override;
};
