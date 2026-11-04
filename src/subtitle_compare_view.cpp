// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_compare_view.h"

#include "ass_dialogue.h"
#include "ass_file.h"

#include <libaegisub/ass/time.h>
#include <stdexcept>

namespace subtitle_compare_view {
namespace {
// Milliseconds are significant in comparisons (a 1 ms change is a change).
std::string time(int ms) { return agi::Time(ms).GetAssFormatted(true); }

std::string extras(AssFile const& file, AssDialogue const& line) {
    std::string out;
    for (auto const& e : file.GetExtradata(line.ExtradataIds.get())) {
        if (!out.empty()) out += "; ";
        out += e.key + "=" + e.value;
    }
    return out;
}

std::string value(AssFile const& file, AssDialogue const& line, model::Field field) {
    switch (field) {
        case model::Text: return line.Text.get();
        case model::Start: return time(line.Start.GetMilliseconds());
        case model::End: return time(line.End.GetMilliseconds());
        case model::Layer: return std::to_string(line.Layer);
        case model::Style: return line.Style.get();
        case model::Actor: return line.Actor.get();
        case model::MarginL: return std::to_string(line.Margin[0]);
        case model::MarginR: return std::to_string(line.Margin[1]);
        case model::MarginV: return std::to_string(line.Margin[2]);
        case model::Effect: return line.Effect.get();
        case model::Comment: return line.Comment ? "yes" : "no";
        case model::Extra: return extras(file, line);
        default: return {};
    }
}
}

std::vector<model::Field> const& FieldOrder() {
    static std::vector<model::Field> const order{model::Text, model::Start, model::End, model::Style,
        model::Actor, model::Effect, model::Layer, model::MarginL, model::MarginR, model::MarginV,
        model::Comment, model::Extra};
    return order;
}

char const* FieldName(model::Field field) {
    switch (field) {
        case model::Text: return "Text";
        case model::Start: return "Start";
        case model::End: return "End";
        case model::Layer: return "Layer";
        case model::Style: return "Style";
        case model::Actor: return "Actor";
        case model::MarginL: return "Left margin";
        case model::MarginR: return "Right margin";
        case model::MarginV: return "Vertical margin";
        case model::Effect: return "Effect";
        case model::Comment: return "Comment";
        case model::Extra: return "Extra data";
        default: return "";
    }
}

State::State(AssFile const& current, AssFile const& baseline, model::Fields comparison_fields,
        std::vector<model::Item> const& items) {
    std::vector<AssDialogue const*> cur, base;
    for (auto const& line : current.Events) cur.push_back(&line);
    for (auto const& line : baseline.Events) base.push_back(&line);
    for (size_t i = 0; i < items.size(); ++i) {
        auto const& item = items[i];
        if (item.kind == model::Kind::Unchanged) continue;
        if ((item.current && *item.current >= cur.size()) || (item.baseline && *item.baseline >= base.size()))
            throw std::invalid_argument("Comparison items do not describe these documents");
        Row row;
        row.item = i; row.kind = item.kind; row.ambiguity = item.ambiguity;
        AssDialogue const* c = item.current ? cur[*item.current] : nullptr;
        AssDialogue const* b = item.baseline ? base[*item.baseline] : nullptr;
        if (c) { row.current_row = *item.current + 1; row.current_text = c->Text.get(); }
        if (b) { row.baseline_row = *item.baseline + 1; row.baseline_text = b->Text.get(); }
        auto shown = b ? b : c;
        row.time = time(shown->Start.GetMilliseconds()) + " - " + time(shown->End.GetMilliseconds());
        model::Fields f = 0;
        if (item.kind == model::Kind::Modified) {
            f = item.changed & comparison_fields;
            for (auto field : FieldOrder())
                if (f & field) row.diffs.push_back({field, value(current, *c, field), value(baseline, *b, field)});
        }
        rows.push_back(std::move(row));
        checked.push_back(false);
        fields.push_back(f);
    }
}

size_t State::Count(std::optional<model::Kind> kind) const {
    if (!kind) return rows.size();
    size_t n = 0;
    for (auto const& row : rows) n += row.kind == *kind;
    return n;
}

void State::Check(size_t row, bool value) {
    checked.at(row) = value;
    if (value && rows[row].kind == model::Kind::Modified && !fields[row])
        for (auto const& d : rows[row].diffs) fields[row] |= d.field;
}

void State::CheckAll(bool value, std::optional<model::Kind> kind) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (!kind || rows[i].kind == *kind) Check(i, value);
}

size_t State::CheckedCount() const {
    size_t n = 0;
    for (bool c : checked) n += c;
    return n;
}

void State::SetField(size_t row, model::Field field, bool value) {
    if (rows.at(row).kind != model::Kind::Modified) throw std::logic_error("Only modified rows have fields to choose");
    bool known = false;
    for (auto const& d : rows[row].diffs) known |= d.field == field;
    if (!known) throw std::invalid_argument("Field did not change in this row");
    if (value) {
        // An unchecked row shows no field as chosen: choosing one starts from it alone.
        if (!checked[row]) fields[row] = 0;
        fields[row] |= field; checked[row] = true;
    }
    else {
        fields[row] &= ~model::Fields(field);
        if (!fields[row]) checked[row] = false;
    }
}

std::vector<subtitle_apply::Choice> State::Choices() const {
    std::vector<subtitle_apply::Choice> out;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!checked[i]) continue;
        out.push_back({rows[i].item, rows[i].kind == model::Kind::Modified ? fields[i] : model::All});
    }
    return out;
}
}
