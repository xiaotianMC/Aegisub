// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "dialog_scroll_effect.h"

#include "flat_controls.h"
#include "flat_dialog.h"
#include "format.h"
#include "scroll_effect.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

wxString ScrollSpeedText(ScrollEffect const& effect, int duration_ms) {
	double px_s = 1000. / EffectiveDelay(effect.delay);
	int travel = static_cast<int>(std::lround(ScrollShift(effect, duration_ms)));
	return fmt_tl("%s px/s, %d px by the line's end",
		px_s >= 10 ? wxString::Format("%.0f", px_s) : wxString::Format("%.1f", px_s), travel);
}

std::optional<std::string> ShowScrollEffectDialog(wxWindow *parent, std::string const& current, int duration_ms, int script_w, int script_h) {
	wxDialog d(parent, -1, _("Scroll Effect"));
	theme::UseSurface(&d);
	int gap = d.FromDIP(10), row = d.FromDIP(6);

	auto const parsed = ParseScrollEffect(current);
	ScrollEffect e = parsed.value_or(DefaultScrollEffect(ScrollEffect::Kind::ScrollUp, duration_ms, script_w, script_h));
	// A banner chosen anew crosses the screen by the line's end
	bool banner_delay_set = parsed && !parsed->IsScroll();
	bool scroll_delay_set = parsed && parsed->IsScroll();

	auto kind = new FlatSwitch(&d, {wxGETTEXT_IN_CONTEXT("scroll effect", "&None"), _("&Banner"), _("Scroll &up"), _("Scroll &down")});
	kind->SetSelection(!parsed ? 0 : parsed->kind == ScrollEffect::Kind::Banner ? 1 : parsed->kind == ScrollEffect::Kind::ScrollUp ? 2 : 3);
	auto direction = new FlatSwitch(&d, {_("Right to &left"), _("Left to &right")});
	direction->SetSelection(e.left_to_right ? 1 : 0);

	auto top = flat::NumberField::Create(&d, wxGETTEXT_IN_CONTEXT("scroll effect", "Top"), e.y1, 0, script_h);
	auto bottom = flat::NumberField::Create(&d, wxGETTEXT_IN_CONTEXT("scroll effect", "Bottom"), e.y2, 0, script_h);
	auto delay = flat::NumberField::Create(&d, _("Delay"), e.delay, 0, 1000000, 1, 0, 64);
	delay->SetToolTip(_("Milliseconds the text takes to move one pixel of the script's resolution"));
	auto speed = flat::DimLabel(&d, wxString());
	auto fade_check = new FlatCheck(&d, _("&Fade at the edges (VSFilter only)"));
	fade_check->SetValue(e.fade.has_value());
	auto fade = flat::NumberField::Create(&d, wxGETTEXT_IN_CONTEXT("scroll effect", "Fade"), e.fade.value_or(0), 0, std::max(script_w, script_h));

	auto replacing = parsed || current.empty() ? nullptr
		: flat::DimLabel(&d, _("The Effect field holds something else, which this replaces"));

	auto update = [&] {
		int k = kind->GetSelection();
		bool banner = k == 1, scroll = k >= 2;
		if (banner && !banner_delay_set) {
			delay->SetValue(DelayForShift(duration_ms, std::max(script_w, 1)));
			banner_delay_set = true;
		}
		if (scroll && !scroll_delay_set) {
			delay->SetValue(DelayForShift(duration_ms, std::max(std::abs(bottom->GetInt() - top->GetInt()), 1)));
			scroll_delay_set = true;
		}
		direction->Enable(banner);
		top->Enable(scroll);
		bottom->Enable(scroll);
		delay->Enable(k != 0);
		fade_check->Enable(k != 0);
		fade->Enable(k != 0 && fade_check->GetValue());
		ScrollEffect shown;
		shown.delay = delay->GetInt();
		speed->SetLabel(k == 0 ? wxString() : ScrollSpeedText(shown, duration_ms));
		d.Layout();
	};
	kind->on_choose = [&](int) { update(); };
	delay->on_change = [&](double) { update(); };
	fade_check->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent&) { update(); });

	auto grid = new wxFlexGridSizer(2, row, gap);
	grid->AddGrowableCol(1);
	auto add = [&](wxString const& label, wxSizer *content) {
		grid->Add(flat::DimLabel(&d, label), wxSizerFlags().CenterVertical());
		grid->Add(content, wxSizerFlags().Expand());
	};
	auto line = [&](std::initializer_list<wxWindow*> windows) {
		auto sizer = new wxBoxSizer(wxHORIZONTAL);
		bool first = true;
		for (auto w : windows) {
			sizer->Add(w, wxSizerFlags().CenterVertical().Border(wxLEFT, first ? 0 : row));
			first = false;
		}
		return sizer;
	};
	add(_("Effect"), line({kind}));
	add(_("Direction"), line({direction}));
	add(_("Area"), line({top->Frame(), bottom->Frame()}));
	add(_("Speed"), line({delay->Frame(), speed}));
	grid->AddSpacer(0);
	grid->Add(line({fade_check, fade->Frame()}));

	auto main = new wxBoxSizer(wxVERTICAL);
	main->Add(grid, wxSizerFlags().Expand().Border(wxALL, gap));
	if (replacing) main->Add(replacing, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap));
	main->Add(new flat::DialogButtons(&d, wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap));
	update();
	d.SetSizerAndFit(main);
	d.CenterOnParent();

	if (d.ShowModal() != wxID_OK) return std::nullopt;

	int k = kind->GetSelection();
	if (k == 0) return parsed ? std::string() : current;
	e.kind = k == 1 ? ScrollEffect::Kind::Banner : k == 2 ? ScrollEffect::Kind::ScrollUp : ScrollEffect::Kind::ScrollDown;
	e.delay = delay->GetInt();
	e.left_to_right = k == 1 && direction->GetSelection() == 1;
	e.y1 = top->GetInt();
	e.y2 = bottom->GetInt();
	e.fade = fade_check->GetValue() ? std::optional<int>(fade->GetInt()) : std::nullopt;
	// Unchanged, the field is left as it was written
	if (parsed && *parsed == e) return current;
	return FormatScrollEffect(e);
}
