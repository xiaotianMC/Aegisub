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
//
// Aegisub Project http://www.aegisub.org/

#include "visual_tool.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "ass_style.h"
#include "async_video_provider.h"
#include "project.h"
#include <boost/scope_exit.hpp>
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "video_controller.h"
#include "video_display.h"
#include "visual_tool_clip.h"
#include "visual_tool_drag.h"
#include "visual_tool_vector_clip.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/format.h>
#include <libaegisub/of_type_adaptor.h>
#include <libaegisub/string.h>

#include <algorithm>
#include <tuple>

namespace {
auto row_fields(AssDialogueBase const& r) {
	return std::make_tuple(r.Id,r.Row,r.Comment,r.Layer,r.Margin,r.Start.GetMilliseconds(),
		r.End.GetMilliseconds(),r.Style,r.Actor,r.Effect,r.ExtradataIds,r.Text);
}
}

VisualToolBase::VisualToolBase(VideoDisplay *parent, agi::Context *context)
: c(context)
, parent(parent)
, frame_number(c->videoController->GetFrameN())
, highlight_color_primary_opt(OPT_GET("Colour/Visual Tools/Highlight Primary"))
, highlight_color_secondary_opt(OPT_GET("Colour/Visual Tools/Highlight Secondary"))
, line_color_primary_opt(OPT_GET("Colour/Visual Tools/Lines Primary"))
, line_color_secondary_opt(OPT_GET("Colour/Visual Tools/Lines Secondary"))
, shaded_area_alpha_opt(OPT_GET("Colour/Visual Tools/Shaded Area Alpha"))
, file_changed_connection(c->subtitleAccess->Read().AddCommitListener(&VisualToolBase::OnCommit, this))
{
	SetResolutions();
	RefreshRows();
	active_line = GetActiveDialogueLine();
	connections.push_back(c->selectionController->AddActiveLineListener(&VisualToolBase::OnActiveLineChanged, this));
	connections.push_back(c->videoController->AddSeekListener(&VisualToolBase::OnSeek, this));
	parent->Bind(wxEVT_MOUSE_CAPTURE_LOST, &VisualToolBase::OnMouseCaptureLost, this);
	parent->Bind(wxEVT_CHAR_HOOK, &VisualToolBase::OnKeyDown, this);
	connections.push_back(c->selectionController->AddSelectionListener(&VisualToolBase::OnSelectionChanged, this));
}

VisualToolBase::~VisualToolBase() {
	parent->Unbind(wxEVT_CHAR_HOOK, &VisualToolBase::OnKeyDown, this);
	parent->Unbind(wxEVT_MOUSE_CAPTURE_LOST, &VisualToolBase::OnMouseCaptureLost, this);
	if ((holding || dragging) && parent->HasCapture()) parent->ReleaseMouse();
	try { EndEdit(false); } catch (...) { }
}

void VisualToolBase::SetResolutions() {
	int script_w, script_h, layout_w, layout_h;
	c->subtitleAccess->Read().GetResolution(script_w, script_h);
	c->subtitleAccess->Read().GetEffectiveLayoutResolution(c, layout_w, layout_h);
	script_res = Vector2D(script_w, script_h);
	layout_res = Vector2D(layout_w, layout_h);
}

void VisualToolBase::ReadSelection() {
	selected_ids.clear();
	auto const& selected = c->selectionController->ReadSelectedSet();
	for (auto const& row : c->subtitleAccess->Read().Events)
		if (std::find(selected.begin(), selected.end(), &row) != selected.end()) selected_ids.push_back(row.Id);
	std::sort(selected_ids.begin(), selected_ids.end());
}

void VisualToolBase::RefreshRows() {
	int previous = active_line ? active_line->Id : 0;
	active_line = nullptr;
	ordered_rows.clear();
	rows.clear();
	for (auto const& row : c->subtitleAccess->Read().Events) {
		auto [it, inserted] = rows.emplace(row.Id, std::make_unique<AssDialogue>(static_cast<AssDialogueBase const&>(row)));
		if (!inserted) throw std::logic_error("Duplicate visual row identity");
		ordered_rows.push_back(it->second.get());
	}
	ReadSelection();
	auto selected = c->selectionController->ReadActiveLine();
	int active = selected ? previous : 0;
	for (auto const& row : c->subtitleAccess->Read().Events)
		if (&row == selected) { active = row.Id; break; }
	active_line = Snapshot(active);
	if (!IsDisplayed(active_line)) active_line = nullptr;
	cache_revision = c->subtitleEditGate.Capture();
	if (deferred_seek) { deferred_seek = false; OnFrameChanged(); }
}

AssDialogue* VisualToolBase::Snapshot(int id) const {
	auto it = rows.find(id);
	return it == rows.end() ? nullptr : it->second.get();
}
std::vector<AssDialogue*> VisualToolBase::SelectedLines() const {
	std::vector<AssDialogue*> result;
	for (auto id : selected_ids) if (auto row = Snapshot(id)) result.push_back(row);
	return result;
}

bool VisualToolBase::BeginEdit(int include_id) {
	if (interaction) {
		if (EditCurrent()) {
			auto expanded = targets;
			expanded.insert(expanded.end(), selected_ids.begin(), selected_ids.end());
			if (include_id) expanded.push_back(include_id);
			std::sort(expanded.begin(), expanded.end());
			expanded.erase(std::unique(expanded.begin(), expanded.end()), expanded.end());
			if (expanded != targets) {
				auto baseline = originals;
				for (auto id : expanded) if (!baseline.contains(id)) {
					auto row = Snapshot(id);
					if (!row) throw std::logic_error("Missing visual target snapshot");
					baseline.emplace(id, *row);
				}
				auto next = c->subtitleAccess->TryBeginInteraction(expanded);
				if (!next) return false;
				interaction = std::move(next); targets = std::move(expanded);
				originals = std::move(baseline);
			}
			return true;
		}
		Cancel(); return false;
	}
	if (!cache_revision || !cache_revision->Current()) {
		// A no-op C1-A command also advances the generation. Rebind an idle
		// cache only after exact data equality, never an in-progress gesture.
		bool equal = cache_revision && cache_revision->Revision().document == c->subtitleEditGate.Revision().document
			&& rows.size() == c->subtitleAccess->Read().Events.size();
		for (auto const& row : c->subtitleAccess->Read().Events) {
			auto copy = Snapshot(row.Id);
			if (!copy || row_fields(row) != row_fields(*copy)) { equal = false; break; }
		}
		if (!equal) { Cancel(); return false; }
		cache_revision = c->subtitleEditGate.Capture();
	}
	targets = selected_ids;
	if (active_line) targets.push_back(active_line->Id);
	if (include_id) targets.push_back(include_id);
	std::sort(targets.begin(), targets.end());
	targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
	if (targets.empty()) return false;
	std::map<int, AssDialogueBase> baseline;
	for (auto id : targets) {
		auto row = Snapshot(id);
		if (!row) throw std::logic_error("Missing visual target snapshot");
		baseline.emplace(id, *row);
	}
	interaction = c->subtitleAccess->TryBeginInteraction(targets);
	if (interaction) originals = std::move(baseline);
	return interaction.has_value();
}
bool VisualToolBase::EditCurrent() const { return interaction && interaction->Current(); }

void VisualToolBase::ValidateOriginals() const {
	if (!EditCurrent()) throw std::logic_error("Stale visual interaction");
	size_t found = 0;
	for (auto const& row : c->subtitleAccess->Read().Events) {
		auto it = originals.find(row.Id);
		if (it == originals.end()) continue;
		if (row_fields(row) != row_fields(it->second)) throw std::logic_error("Visual target changed without notification");
		++found;
	}
	if (found != originals.size()) throw std::out_of_range("Visual target removed");
}

void VisualToolBase::EndEdit(bool apply) {
	// End registration even if rendering or a normal Commit observer throws.
	BOOST_SCOPE_EXIT_ALL(this) { interaction.reset(); dirty.clear(); targets.clear(); originals.clear(); edit_message.clear(); };
	auto provider = c->project->VideoProvider();
	if (!apply || !EditCurrent()) {
		if (previewing && provider) provider->LoadSubtitles(&c->subtitleAccess->Read());
		previewing = false;
		return;
	}
	std::vector<AssDialogueBase> updates;
	ValidateOriginals();
	for (auto const& row : c->subtitleAccess->Read().Events) if (dirty.contains(row.Id)) {
		auto update = AssDialogueBase(row);
		update.Text = Snapshot(row.Id)->Text;
		updates.push_back(std::move(update));
	}
	if (!updates.empty()) {
		file_changed_connection.Block();
		BOOST_SCOPE_EXIT_ALL(this) { file_changed_connection.Unblock(); };
		interaction->UpdateRows(updates);
		interaction->Commit(edit_message.empty() ? _("visual typesetting") : edit_message, AssFile::COMMIT_DIAG_TEXT);
	}
	previewing = false;
}

void VisualToolBase::Cancel() {
	holding = dragging = false;
	if (parent->HasCapture()) parent->ReleaseMouse();
	EndEdit(false);
	RefreshRows();
	OnFileChanged();
}
void VisualToolBase::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE && (interaction || holding || dragging)) { Cancel(); parent->Render(); }
	else event.Skip();
}
void VisualToolBase::OnSelectionChanged() {
	if (changing_selection) { ReadSelection(); return; }
	Cancel();
}
void VisualToolBase::OnCommit(int) {
	// VideoController already queued the new live subtitles before this listener.
	previewing = false;
	Cancel();
	SetResolutions();
	OnCoordinateSystemsChanged();
}
void VisualToolBase::OnSeek(int new_frame) {
	if (frame_number == new_frame) return;
	if (changing_selection) { frame_number = new_frame; deferred_seek = true; return; }
	Cancel();
	frame_number = new_frame;
	active_line = GetActiveDialogueLine();
	OnFrameChanged();
	OnLineChanged();
}
void VisualToolBase::OnMouseCaptureLost(wxMouseCaptureLostEvent& event) { Cancel(); event.Skip(); }
void VisualToolBase::OnActiveLineChanged() {
	if (!changing_selection) Cancel();
	active_line = GetActiveDialogueLine();
	OnLineChanged();
	parent->Render();
}

bool VisualToolBase::IsDisplayed(AssDialogue *line) const {
	int frame = c->videoController->GetFrameN();
	return line && !line->Comment
		&& c->videoController->FrameAtTime(line->Start, agi::vfr::START) <= frame
		&& c->videoController->FrameAtTime(line->End, agi::vfr::END) >= frame;
}
void VisualToolBase::Commit(wxString message) {
	if (!EditCurrent()) throw std::logic_error("Stale visual interaction");
	if (!message.empty()) edit_message = message;
	std::vector<AssDialogueBase> preview;
	for (auto id : dirty) if (auto row = Snapshot(id)) preview.push_back(*row);
	if (!preview.empty()) if (auto provider = c->project->VideoProvider()) {
		provider->PreviewSubtitles(std::move(preview));
		previewing = true;
	}
}
AssDialogue* VisualToolBase::GetActiveDialogueLine() {
	auto selected = c->selectionController->ReadActiveLine();
	for (auto const& row : c->subtitleAccess->Read().Events)
		if (&row == selected) { auto copy = Snapshot(row.Id); return IsDisplayed(copy) ? copy : nullptr; }
	return selected && active_line && IsDisplayed(active_line) ? active_line : nullptr;
}

void VisualToolBase::SelectLine(int id) {
	if (!EditCurrent()) throw std::logic_error("Unauthorized visual selection");
	changing_selection = true;
	BOOST_SCOPE_EXIT_ALL(this) { changing_selection = false; };
	c->selectionController->SetActiveID(id);
}
void VisualToolBase::SelectFeature(int id, bool clear) {
	if (!EditCurrent()) throw std::logic_error("Unauthorized visual selection");
	changing_selection = true;
	BOOST_SCOPE_EXIT_ALL(this) { changing_selection = false; };
	auto selected = clear ? std::set<int>{} : c->selectionController->SelectedIDs();
    selected.insert(id);
    c->selectionController->SetSelectedIDs(std::move(selected));
}
void VisualToolBase::DeselectFeature(int id) {
	if (!EditCurrent()) throw std::logic_error("Unauthorized visual selection");
	auto selected = c->selectionController->SelectedIDs();
	if (selected.size() <= 1) return;
	selected.erase(id);
	changing_selection = true;
	BOOST_SCOPE_EXIT_ALL(this) { changing_selection = false; };
	auto active = c->selectionController->ActiveID();
    if (!selected.count(active)) active = *selected.begin();
    c->selectionController->SetSelectionAndActiveIDs(std::move(selected), active);
}
int VisualToolBase::InsertDrawing(AssDialogueBase const& drawing, AssStyle const* style) {
	if (!EditCurrent() || !active_line) throw std::logic_error("Stale drawing interaction");
	ValidateOriginals();
	auto id = interaction->InsertBefore(active_line->Id, drawing, style);
	file_changed_connection.Block();
	BOOST_SCOPE_EXIT_ALL(this) { file_changed_connection.Unblock(); };
	interaction->Commit(_("draw shape"), AssFile::COMMIT_DIAG_ADDREM | (style ? AssFile::COMMIT_STYLES : 0));
	BOOST_SCOPE_EXIT_ALL(this) { interaction.reset(); };
	c->selectionController->SetSelectionAndActiveIDs({id}, id);
	return id;
}

void VisualToolBase::SetCanvasSize(int w, int h) {
	canvas_size = Vector2D(w, h);
}

void VisualToolBase::SetDisplayArea(int x, int y, int w, int h) {
	if (x == video_pos.X() && y == video_pos.Y() && w == video_size.X() && h == video_size.Y()) return;

	video_pos = Vector2D(x, y);
	video_size = Vector2D(w, h);

	Cancel();
	OnCoordinateSystemsChanged();
}

Vector2D VisualToolBase::ToScriptCoords(Vector2D point) const {
	return (point - video_pos) * script_res / video_size;
}

Vector2D VisualToolBase::FromScriptCoords(Vector2D point) const {
	return (point * video_size / script_res) + video_pos;
}

template<class FeatureType>
VisualTool<FeatureType>::VisualTool(VideoDisplay *parent, agi::Context *context)
: VisualToolBase(parent, context)
{
}

template<class FeatureType>
void VisualTool<FeatureType>::OnMouseEvent(wxMouseEvent &event) {
	try { MouseEvent(event); }
	catch (...) { Cancel(); throw; }
}

template<class FeatureType>
void VisualTool<FeatureType>::MouseEvent(wxMouseEvent &event) {
	if ((dragging || holding) && (!EditCurrent() || !parent->HasCapture())) { Cancel(); return; }
	bool left_click = event.LeftDown();
	bool left_double = event.LeftDClick();
	shift_down = event.ShiftDown();
	ctrl_down = event.CmdDown();
	alt_down = event.AltDown();

	mouse_pos = event.GetPosition();

	if (event.Leaving()) {
		mouse_pos = Vector2D();
		parent->Render();
		return;
	}

	if (!dragging) {
		int max_layer = INT_MIN;
		active_feature = nullptr;
		for (auto& feature : features) {
			if (feature.IsMouseOver(mouse_pos) && feature.layer >= max_layer) {
				active_feature = &feature;
				max_layer = feature.layer;
			}
		}
	}

	if (dragging) {
		// continue drag
		if (event.LeftIsDown()) {
			for (auto sel : sel_features)
				sel->UpdateDrag(mouse_pos - drag_start, shift_down);
			for (auto sel : sel_features)
				UpdateDrag(sel);
			Commit();
		}
		// end drag
		else {
			if (!event.LeftUp()) { Cancel(); return; }
			dragging = false;

			// mouse didn't move, fiddle with selection
			if (active_feature && !active_feature->HasMoved()) {
				// Don't deselect stuff that was selected in this click's mousedown event
				if (!sel_changed) {
					if (ctrl_down)
						RemoveSelection(active_feature);
					else
						SetSelection(active_feature, true);
				}
			}

			active_feature = nullptr;
			parent->ReleaseMouse();
			parent->SetFocus();
		}
	}
	else if (holding) {
		if (!event.LeftIsDown()) {
			if (!event.LeftUp()) { Cancel(); return; }
			holding = false;

			parent->ReleaseMouse();
			parent->SetFocus();
		}

		UpdateHold();
		Commit();

	}
	else if (left_click) {
		if (!BeginEdit(active_feature ? active_feature->line_id : 0)) return;
		drag_start = mouse_pos;

		// start drag
		if (active_feature) {
			if (!sel_features.count(active_feature)) {
				sel_changed = true;
				SetSelection(active_feature, !ctrl_down);
			}
			else
				sel_changed = false;

			if (active_feature->line_id) SelectLine(active_feature->line_id);
			if (!BeginEdit(active_feature->line_id)) return;

			if (InitializeDrag(active_feature)) {
				for (auto sel : sel_features) sel->StartDrag();
				dragging = true;
				parent->CaptureMouse();
			}
		}
		// start hold
		else {
			if (!alt_down && features.size() > 1) {
				sel_features.clear();
				if (active_line) SelectFeature(active_line->Id, true);
			}
			if (active_line && InitializeHold()) {
				holding = true;
				parent->CaptureMouse();
			}
		}
	}

	if (active_line && left_double) {
		if (!BeginEdit()) return;
		OnDoubleClick();
	}

	if (!holding && !dragging && HasEdit()) { EndEdit(true); RefreshRows(); }
	parent->Render();

}

template<class FeatureType>
void VisualTool<FeatureType>::DrawAllFeatures() {
	wxColour grid_color = to_wx(line_color_secondary_opt->GetColor());
	gl.SetLineColour(grid_color, 1.0f, 1);
	wxColour base_fill = to_wx(highlight_color_primary_opt->GetColor());
	wxColour active_fill = to_wx(highlight_color_secondary_opt->GetColor());
	wxColour alt_fill = to_wx(line_color_primary_opt->GetColor());
	for (auto& feature : features) {
		wxColour fill = base_fill;
		if (&feature == active_feature)
			fill = active_fill;
		else if (sel_features.count(&feature))
			fill = alt_fill;
		gl.SetFillColour(fill, 0.3f);
		feature.Draw(gl);
	}
}

template<class FeatureType>
void VisualTool<FeatureType>::SetSelection(FeatureType *feat, bool clear) {
	if (clear) sel_features.clear();
	if (sel_features.insert(feat).second && feat->line_id) SelectFeature(feat->line_id, clear);
}
template<class FeatureType>
void VisualTool<FeatureType>::RemoveSelection(FeatureType *feat) {
	if (!sel_features.erase(feat) || !feat->line_id) return;
	for (auto sel : sel_features) if (sel->line_id == feat->line_id) return;
	DeselectFeature(feat->line_id);
}

//////// PARSERS

typedef const std::vector<AssOverrideParameter> * param_vec;

// Find a tag's parameters in a line or return nullptr if it's not found
static param_vec find_tag(std::vector<std::unique_ptr<AssDialogueBlock>>& blocks, std::string const& tag_name) {
	for (auto ovr : blocks | agi::of_type<AssDialogueBlockOverride>()) {
		for (auto const& tag : ovr->Tags) {
			if (tag.Name == tag_name)
				return &tag.Params;
		}
	}

	return nullptr;
}

// Get a Vector2D from the given tag parameters, or Vector2D::Bad() if they are not valid
static Vector2D vec_or_bad(param_vec tag, size_t x_idx, size_t y_idx) {
	if (!tag ||
		tag->size() <= x_idx || tag->size() <= y_idx ||
		(*tag)[x_idx].omitted || (*tag)[y_idx].omitted)
	{
		return Vector2D();
	}
	return Vector2D((*tag)[x_idx].Get<float>(), (*tag)[y_idx].Get<float>());
}

Vector2D VisualToolBase::GetLinePosition(AssDialogue *diag) {
	auto blocks = diag->ParseTags();

	if (Vector2D ret = vec_or_bad(find_tag(blocks, "\\pos"), 0, 1)) return ret;
	if (Vector2D ret = vec_or_bad(find_tag(blocks, "\\move"), 0, 1)) return ret;

	// Get default position
	auto margin = diag->Margin;
	int align = 2;

	if (AssStyle const* style = c->subtitleAccess->Read().GetStyle(diag->Style)) {
		align = style->alignment;
		for (int i = 0; i < 3; i++) {
			if (margin[i] == 0)
				margin[i] = style->Margin[i];
		}
	}

	param_vec align_tag;
	int ovr_align = 0;
	if ((align_tag = find_tag(blocks, "\\an")))
		ovr_align = (*align_tag)[0].Get<int>(ovr_align);
	else if ((align_tag = find_tag(blocks, "\\a")))
		ovr_align = AssStyle::SsaToAss((*align_tag)[0].Get<int>(2));

	if (ovr_align > 0 && ovr_align <= 9)
		align = ovr_align;

	// Alignment type
	int hor = (align - 1) % 3;
	int vert = (align - 1) / 3;

	// Calculate positions
	int x, y;
	if (hor == 0)
		x = margin[0];
	else if (hor == 1)
		x = (script_res.X() + margin[0] - margin[1]) / 2;
	else
		x = script_res.X() - margin[1];

	if (vert == 0)
		y = script_res.Y() - margin[2];
	else if (vert == 1)
		y = script_res.Y() / 2;
	else
		y = margin[2];

	return Vector2D(x, y);
}

Vector2D VisualToolBase::GetLineOrigin(AssDialogue *diag) {
	auto blocks = diag->ParseTags();
	return vec_or_bad(find_tag(blocks, "\\org"), 0, 1);
}

bool VisualToolBase::GetLineMove(AssDialogue *diag, Vector2D &p1, Vector2D &p2, int &t1, int &t2) {
	auto blocks = diag->ParseTags();

	param_vec tag = find_tag(blocks, "\\move");
	if (!tag)
		return false;

	p1 = vec_or_bad(tag, 0, 1);
	p2 = vec_or_bad(tag, 2, 3);
	// VSFilter actually defaults to -1, but it uses <= 0 to check for default and 0 seems less bug-prone
	t1 = (*tag)[4].Get<int>(0);
	t2 = (*tag)[5].Get<int>(0);

	return p1 && p2;
}

void VisualToolBase::GetLineRotation(AssDialogue *diag, float &rx, float &ry, float &rz) {
	rx = ry = rz = 0.f;

	if (AssStyle const* style = c->subtitleAccess->Read().GetStyle(diag->Style))
		rz = style->angle;

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\frx"))
		rx = tag->front().Get(rx);
	if (param_vec tag = find_tag(blocks, "\\fry"))
		ry = tag->front().Get(ry);
	if (param_vec tag = find_tag(blocks, "\\frz"))
		rz = tag->front().Get(rz);
	else if ((tag = find_tag(blocks, "\\fr")))
		rz = tag->front().Get(rz);
}

void VisualToolBase::GetLineShear(AssDialogue *diag, float& fax, float& fay) {
	fax = fay = 0.f;

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\fax"))
		fax = tag->front().Get(fax);
	if (param_vec tag = find_tag(blocks, "\\fay"))
		fay = tag->front().Get(fay);
}

void VisualToolBase::GetLineScale(AssDialogue *diag, Vector2D &scale) {
	float x = 100.f, y = 100.f;

	if (AssStyle const* style = c->subtitleAccess->Read().GetStyle(diag->Style)) {
		x = style->scalex;
		y = style->scaley;
	}

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\fscx"))
		x = tag->front().Get(x);
	if (param_vec tag = find_tag(blocks, "\\fscy"))
		y = tag->front().Get(y);

	scale = Vector2D(x, y);
}

void VisualToolBase::GetLineClip(AssDialogue *diag, Vector2D &p1, Vector2D &p2, bool &inverse) {
	inverse = false;

	auto blocks = diag->ParseTags();
	param_vec tag = find_tag(blocks, "\\iclip");
	if (tag)
		inverse = true;
	else
		tag = find_tag(blocks, "\\clip");

	if (tag && tag->size() == 4) {
		p1 = vec_or_bad(tag, 0, 1);
		p2 = vec_or_bad(tag, 2, 3);
	}
	else {
		p1 = Vector2D(0, 0);
		p2 = script_res - 1;
	}
}

std::string VisualToolBase::GetLineVectorClip(AssDialogue *diag, int &scale, bool &inverse) {
	auto blocks = diag->ParseTags();

	scale = 1;
	inverse = false;

	param_vec tag = find_tag(blocks, "\\iclip");
	if (tag)
		inverse = true;
	else
		tag = find_tag(blocks, "\\clip");

	if (tag && tag->size() == 4) {
		return agi::format("m %.2f %.2f l %.2f %.2f %.2f %.2f %.2f %.2f"
			, (*tag)[0].Get<double>(), (*tag)[1].Get<double>()
			, (*tag)[2].Get<double>(), (*tag)[1].Get<double>()
			, (*tag)[2].Get<double>(), (*tag)[3].Get<double>()
			, (*tag)[0].Get<double>(), (*tag)[3].Get<double>());
	}
	if (tag) {
		scale = std::max((*tag)[0].Get(scale), 1);
		return (*tag)[1].Get<std::string>("");
	}

	return "";
}

void VisualToolBase::SetSelectedOverride(std::string const& tag, std::string const& value) {
	for (auto line : SelectedLines())
		SetOverride(line, tag, value);
}

void VisualToolBase::SetOverride(AssDialogue* line, std::string const& tag, std::string const& value) {
	if (!EditCurrent() || !line || Snapshot(line->Id) != line || !std::binary_search(targets.begin(), targets.end(), line->Id))
		throw std::logic_error("Unauthorized visual snapshot");
	auto original = line->Text;

	std::string removeTag;
	if (tag == "\\1c") removeTag = "\\c";
	else if (tag == "\\frz") removeTag = "\\fr";
	else if (tag == "\\pos") removeTag = "\\move";
	else if (tag == "\\move") removeTag = "\\pos";
	else if (tag == "\\clip") removeTag = "\\iclip";
	else if (tag == "\\iclip") removeTag = "\\clip";

	// Get block at start
	auto blocks = line->ParseTags();
	AssDialogueBlock *block = blocks.front().get();

	if (block->GetType() == AssBlockType::OVERRIDE) {
		auto ovr = static_cast<AssDialogueBlockOverride*>(block);
		// Remove old of same
		for (size_t i = 0; i < ovr->Tags.size(); i++) {
			std::string const& name = ovr->Tags[i].Name;
			if (tag == name || removeTag == name) {
				ovr->Tags.erase(ovr->Tags.begin() + i);
				i--;
			}
		}
		ovr->AddTag(tag + value);

		line->UpdateText(blocks);
	}
	else
		line->Text = agi::Str("{", tag, value, "}", line->Text.get());
	if (line->Text != original) dirty.insert(line->Id);
}

// If only export worked
template class VisualTool<VisualDraggableFeature>;
template class VisualTool<ClipCorner>;
template class VisualTool<VisualToolDragDraggableFeature>;
template class VisualTool<VisualToolVectorClipDraggableFeature>;
