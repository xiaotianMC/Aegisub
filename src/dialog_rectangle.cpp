// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "dialog_rectangle.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_override.h"
#include "ass_style.h"
#include "colour_button.h"
#include "include/aegisub/context.h"
#include "shape_drawing.h"
#include "selection_controller.h"

#include <libaegisub/format.h>
#include "utils.h"

#include <algorithm>
#include <array>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace {
struct Drawing {
    std::vector<std::unique_ptr<AssDialogueBlock>> blocks;
    AssDialogueBlockDrawing *drawing = nullptr;
    AssDialogueBlockOverride *prefix = nullptr;
    double x = 0, y = 0, width = 0, height = 0, scale_x = 100, scale_y = 100;
    agi::Color color;
};

std::optional<Drawing> ReadDrawing(const agi::Context *c, const AssDialogue *line) {
    if (!line) return std::nullopt;
    Drawing rect;
    rect.blocks = line->ParseTags();
    auto style = c->ass->GetStyle(line->Style);
    if (!style) return std::nullopt;
    rect.color = style->primary;
    rect.scale_x = style->scalex;
    rect.scale_y = style->scaley;
    int alpha = rect.color.a, alignment = style->alignment, positions = 0;
    for (auto const& block : rect.blocks) {
        if (block->GetType() == AssBlockType::PLAIN && !block->GetText().empty())
            return std::nullopt;
        if (block->GetType() == AssBlockType::DRAWING) {
            if (rect.drawing) return std::nullopt;
            rect.drawing = static_cast<AssDialogueBlockDrawing*>(block.get());
            auto bounds = ParseDrawingBounds(block->GetText());
            if (!bounds || rect.drawing->Scale < 1 || rect.drawing->Scale > 30 || !rect.prefix)
                return std::nullopt;
            double unit = std::ldexp(1.0, rect.drawing->Scale - 1);
            rect.width = bounds->Width() / unit * rect.scale_x / 100;
            rect.height = bounds->Height() / unit * rect.scale_y / 100;
            rect.color.a = alpha;
        }
        if (block->GetType() != AssBlockType::OVERRIDE || rect.drawing) continue;
        rect.prefix = static_cast<AssDialogueBlockOverride*>(block.get());
        for (auto const& tag : rect.prefix->Tags) {
            auto const& n = tag.Name;
            // Animated position, transforms, and style resets need a richer editor.
            if (n == "\\move" || n == "\\t" || n == "\\r") return std::nullopt;
            if (n == "\\pos") {
                if (tag.Params.size() != 2 || tag.Params[0].omitted || tag.Params[1].omitted)
                    return std::nullopt;
                rect.x = tag.Params[0].Get<double>();
                rect.y = tag.Params[1].Get<double>();
                ++positions;
            }
            else if (!tag.Params.empty() && !tag.Params[0].omitted) {
                if (n == "\\an") alignment = tag.Params[0].Get<int>();
                else if (n == "\\a") alignment = AssStyle::SsaToAss(tag.Params[0].Get<int>());
                else if (n == "\\fscx") rect.scale_x = tag.Params[0].Get<double>();
                else if (n == "\\fscy") rect.scale_y = tag.Params[0].Get<double>();
                else if (n == "\\c" || n == "\\1c") rect.color = tag.Params[0].Get<agi::Color>();
                else if (n == "\\alpha" || n == "\\1a") alpha = tag.Params[0].Get<int>();
            }
        }
    }
    if (positions > 1 || alignment < 1 || alignment > 9) return std::nullopt;
    if (!positions) {
        auto margins = line->Margin;
        for (size_t i = 0; i < margins.size(); ++i)
            if (!margins[i]) margins[i] = style->Margin[i];
        int script_width, script_height;
        c->ass->GetResolution(script_width, script_height);
        int horizontal = (alignment - 1) % 3, vertical = (alignment - 1) / 3;
        rect.x = horizontal == 0 ? margins[0] : horizontal == 2 ? script_width - margins[1]
            : (script_width + margins[0] - margins[1]) / 2;
        rect.y = vertical == 0 ? script_height - margins[2] : vertical == 2 ? margins[2]
            : script_height / 2;
    }
    if (!rect.drawing || !std::isfinite(rect.x) || !std::isfinite(rect.y)
        || std::abs(rect.x) > 1000000 || std::abs(rect.y) > 1000000
        || !std::isfinite(rect.scale_x) || !std::isfinite(rect.scale_y)
        || rect.scale_x < 0.001 || rect.scale_y < 0.001 || rect.scale_x > 1000000 || rect.scale_y > 1000000
        || !std::isfinite(rect.width) || !std::isfinite(rect.height)
        || rect.width < 0.001 || rect.height < 0.001 || rect.width > 1000000 || rect.height > 1000000)
        return std::nullopt;
    return rect;
}
}

bool CanEditDrawing(const agi::Context *c) {
    return ReadDrawing(c, c->selectionController->GetActiveLine()).has_value();
}

void EditDrawing(agi::Context *c) {
    auto line = c->selectionController->GetActiveLine();
    auto parsed = ReadDrawing(c, line);
    if (!parsed) return;
    auto& rect = *parsed;
    const AssDialogue original(*line);
    wxDialog dialog(c->parent, -1, _("Edit Shape"));
    auto layout = new wxBoxSizer(wxVERTICAL);
    auto fields = new wxFlexGridSizer(2, dialog.FromDIP(8), dialog.FromDIP(12));
    fields->AddGrowableCol(1);
    auto number = [&](wxString const& label, double value, double min, double max, int digits) {
        fields->Add(new wxStaticText(&dialog, -1, label), 0, wxALIGN_CENTER_VERTICAL);
        auto control = new wxSpinCtrlDouble(&dialog, -1, "", wxDefaultPosition,
            dialog.FromDIP(wxSize(170, -1)), wxSP_ARROW_KEYS, min, max, value, 1);
        control->SetDigits(digits);
        fields->Add(control, 1, wxEXPAND);
        return control;
    };
    auto width = number(_("Shape width"), rect.width, std::max(0.001, rect.width * 0.001 / rect.scale_x),
        std::min(1000000.0, rect.width * 1000000 / rect.scale_x), 3);
    auto height = number(_("Shape height"), rect.height, std::max(0.001, rect.height * 0.001 / rect.scale_y),
        std::min(1000000.0, rect.height * 1000000 / rect.scale_y), 3);
    auto x = number(_("Position X"), rect.x, -1000000, 1000000, 3);
    auto y = number(_("Position Y"), rect.y, -1000000, 1000000, 3);
    fields->Add(new wxStaticText(&dialog, -1, _("Fill color")), 0, wxALIGN_CENTER_VERTICAL);
    auto color = new ColourButton(&dialog, dialog.FromDIP(wxSize(100, 20)), false, rect.color);
    fields->Add(color, 1, wxEXPAND);
    auto transparency = number(_("Transparency (%)"), rect.color.a * 100.0 / 255, 0, 100, 1);
    layout->Add(fields, 0, wxALL | wxEXPAND, dialog.FromDIP(12));
    auto hint = new wxStaticText(&dialog, -1,
        _("Size uses the drawing control-point bounds before rotation. Position is the alignment anchor. Transparency: 0% opaque, 100% invisible."));
    hint->Wrap(dialog.FromDIP(400));
    layout->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
    layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0,
        wxALL | wxALIGN_RIGHT, dialog.FromDIP(12));
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    const std::array controls{width, height, x, y, transparency};
    auto update = [&](bool show_error) {
        std::array<double, 5> values;
        for (size_t i = 0; i < controls.size(); ++i) {
            auto ctrl = controls[i];
            if (!ctrl->GetTextValue().ToDouble(&values[i]) || !std::isfinite(values[i])
                || values[i] < ctrl->GetMin() || values[i] > ctrl->GetMax()) {
                if (show_error) {
                    wxMessageBox(_("Enter a number within the allowed range."), _("Edit Shape"), wxOK | wxICON_WARNING, &dialog);
                    ctrl->SetFocus();
                }
                return false;
            }
        }

        // Rebuild from the original so returning a field to its initial value
        // also removes its preview tag without accumulating rounding errors.
        auto edited = ReadDrawing(c, &original);
        auto& rect = *edited;
        auto new_color = color->GetColor();
        new_color.a = std::lround(values[4] * 255 / 100);
        bool resize_x = std::abs(values[0] - rect.width) >= 0.00051;
        bool resize_y = std::abs(values[1] - rect.height) >= 0.00051;
        bool position = std::abs(values[2] - rect.x) >= 0.00051 || std::abs(values[3] - rect.y) >= 0.00051;
        bool fill = new_color.r != rect.color.r || new_color.g != rect.color.g || new_color.b != rect.color.b;
        bool alpha = new_color.a != rect.color.a;
        AssDialogue result(original);
        if (resize_x || resize_y || position || fill || alpha) {
            // Preserve unrelated override tags, comments, timing, style and layer.
            for (auto const& block : rect.blocks) {
                if (block.get() == rect.drawing) break;
                if (block->GetType() != AssBlockType::OVERRIDE) continue;
                auto over = static_cast<AssDialogueBlockOverride*>(block.get());
                std::erase_if(over->Tags, [&](auto const& tag) {
                    return (position && tag.Name == "\\pos") || (resize_x && tag.Name == "\\fscx")
                        || (resize_y && tag.Name == "\\fscy") || (fill && (tag.Name == "\\c" || tag.Name == "\\1c"))
                        || (alpha && tag.Name == "\\1a");
                });
            }
            if (position) rect.prefix->AddTag("\\pos(" + float_to_string(values[2]) + ","
                + float_to_string(values[3]) + ")");
            if (resize_x) rect.prefix->AddTag("\\fscx" + float_to_string(rect.scale_x * values[0] / rect.width, 9));
            if (resize_y) rect.prefix->AddTag("\\fscy" + float_to_string(rect.scale_y * values[1] / rect.height, 9));
            if (fill) rect.prefix->AddTag("\\1c" + new_color.GetAssOverrideFormatted());
            if (alpha) rect.prefix->AddTag(agi::format("\\1a&H%02X&", (int)new_color.a));
            result.UpdateText(rect.blocks);
        }
        if (line->Text != result.Text) {
            line->Text = result.Text;
            // An empty description refreshes all views without adding undo history.
            c->ass->Commit("", AssFile::COMMIT_DIAG_TEXT, -1, line);
        }
        return true;
    };
    for (auto ctrl : controls) {
        ctrl->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { update(false); });
        ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [&](wxSpinDoubleEvent&) { update(false); });
    }
    color->Bind(EVT_COLOR, [&](ValueEvent<agi::Color>&) { update(false); });
    dialog.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        if (update(true)) dialog.EndModal(wxID_OK);
    }, wxID_OK);
    if (dialog.ShowModal() != wxID_OK) {
        if (line->Text != original.Text) {
            line->Text = original.Text;
            c->ass->Commit("", AssFile::COMMIT_DIAG_TEXT, -1, line);
        }
        return;
    }
    if (line->Text != original.Text)
        c->ass->Commit(_("edit shape"), AssFile::COMMIT_DIAG_TEXT, -1, line);
}
