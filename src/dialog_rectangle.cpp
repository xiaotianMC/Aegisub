// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "dialog_rectangle.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "ass_override.h"
#include "ass_style.h"
#include "include/aegisub/context.h"
#include "shape_drawing.h"
#include "selection_controller.h"
#include "subtitle_dialog_edit.h"
#include "subtitle_row_edit.h"
#include "frame_main.h"
#include "style_panel.h"

#include <libaegisub/format.h>
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <wx/intl.h>

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
    AssStyle const* style=nullptr;
    for(auto const& entry:c->subtitleAccess->Read().Styles)if(boost::iequals(entry.name,line->Style.get())){style=&entry;break;}
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
        c->subtitleAccess->Read().GetResolution(script_width, script_height);
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
    auto id=SubtitleDialogEdit::ActiveID(c);
    for(auto const& row:c->subtitleAccess->Read().Events)if(row.Id==id)return ReadDrawing(c,&row).has_value();
    return false;
}

void EditDrawing(agi::Context *c) {
    if (!CanEditDrawing(c) || !c->frame) return;
    if (auto panel = c->frame->GetStylePanel()) panel->EditShape();
}

std::optional<std::pair<double, double>> ShapeSize(const agi::Context *c) {
    auto active = c->selectionController->ReadActiveLine();
    if (!active) return std::nullopt;
    auto rect = ReadDrawing(c, active);
    if (!rect) return std::nullopt;
    return std::make_pair(rect->width, rect->height);
}

int ResizeShape(agi::Context *c, std::optional<double> width, std::optional<double> height, int amend_id) {
    auto scope = c->subtitleAccess->TryWrite();
    if (!scope) return -1;
    SubtitleRowEdit staged(c);
    AssDialogue *row = nullptr;
    for (auto r : staged.rows)
        if (r->Id == staged.active) row = r;
    if (!row) return amend_id;
    auto parsed = ReadDrawing(c, row);
    if (!parsed) return amend_id;
    auto& rect = *parsed;
    auto valid = [](std::optional<double> v) { return v && std::isfinite(*v) && *v >= 0.001 && *v <= 1000000; };
    bool resize_x = valid(width) && std::abs(*width - rect.width) >= 0.00051;
    bool resize_y = valid(height) && std::abs(*height - rect.height) >= 0.00051;
    if (!resize_x && !resize_y) return amend_id;

    // Scale the drawing's points rather than add \fscx/\fscy: scale tags
    // are baked into the path, so the size is exact and they can go
    double base_x = 100, base_y = 100;
    if (auto style = c->subtitleAccess->Read().GetStyle(row->Style.get())) {
        base_x = style->scalex;
        base_y = style->scaley;
    }
    double scale_x = (resize_x ? *width : rect.width) * rect.scale_x / (rect.width * base_x);
    double scale_y = (resize_y ? *height : rect.height) * rect.scale_y / (rect.height * base_y);
    auto path = ScaleDrawingPath(rect.drawing->text, scale_x, scale_y);
    if (path.empty()) return amend_id;
    rect.drawing->text = std::move(path);
    for (auto const& block : rect.blocks) {
        if (block.get() == rect.drawing) break;
        if (block->GetType() != AssBlockType::OVERRIDE) continue;
        std::erase_if(static_cast<AssDialogueBlockOverride*>(block.get())->Tags, [](auto const& tag) {
            return tag.Name == "\\fscx" || tag.Name == "\\fscy";
        });
    }
    row->UpdateText(rect.blocks);
    auto edit = c->subtitleAccess->Edit(scope->Lease());
    edit.UpdateRows(staged.Values());
    return edit.Commit(_("edit shape"), AssFile::COMMIT_DIAG_TEXT, amend_id, staged.rows.size() == 1 ? row->Id : 0);
}
