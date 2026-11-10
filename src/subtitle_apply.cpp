// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_apply.h"

#include "ass_dialogue.h"
#include "ass_style.h"
#include "subtitle_compare.h"

#include <boost/algorithm/string/predicate.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace subtitle_apply {
namespace {
constexpr auto binding_key = "aegisub.enhanced.subtitle-binding";

struct ExtraIndex {
    std::map<uint32_t, ExtradataEntry const*> by_id;
    explicit ExtraIndex(AssFile const& file) {
        for (auto const& e : file.Extradata) by_id.emplace(e.id, &e);
    }
    // nullopt: a reference does not resolve
    std::optional<std::vector<ExtradataEntry const*>> Resolve(AssDialogueBase const& line) const {
        std::vector<ExtradataEntry const*> out;
        for (auto id : line.ExtradataIds.get()) {
            auto it = by_id.find(id);
            if (it == by_id.end()) return std::nullopt;
            out.push_back(it->second);
        }
        return out;
    }
};

std::optional<std::string> binding_of(std::vector<ExtradataEntry const*> const& extras) {
    for (auto e : extras) if (e->key == binding_key) return e->value;
    return std::nullopt;
}

void copy_fields(AssDialogueBase& to, AssDialogueBase const& from, model::Fields f) {
    if (f & model::Text) to.Text = from.Text;
    if (f & model::Start) to.Start = from.Start;
    if (f & model::End) to.End = from.End;
    if (f & model::Layer) to.Layer = from.Layer;
    if (f & model::Style) to.Style = from.Style;
    if (f & model::Actor) to.Actor = from.Actor;
    if (f & model::MarginL) to.Margin[0] = from.Margin[0];
    if (f & model::MarginR) to.Margin[1] = from.Margin[1];
    if (f & model::MarginV) to.Margin[2] = from.Margin[2];
    if (f & model::Effect) to.Effect = from.Effect;
    if (f & model::Comment) to.Comment = from.Comment;
}

void add_unique(std::vector<std::string>& list, std::string const& value) {
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

// \r style names in a line's override blocks (same rule as AnalyzeDependencies)
void reset_style_names(AssDialogueBlockOverride const& block, std::vector<std::string>& names) {
    for (auto const& tag : block.Tags) {
        if (tag.IsValid() && tag.Name == "\\r" && !tag.Params.empty() && !tag.Params[0].omitted) {
            auto name = tag.Params[0].Get<std::string>();
            if (!name.empty()) add_unique(names, name);
        }
        for (auto const& param : tag.Params)
            if (!param.omitted && param.GetType() == VariableDataType::BLOCK)
                reset_style_names(*param.Get<AssDialogueBlockOverride*>(), names);
    }
}
}

Plan Prepare(AssFile const& current, AssFile const& baseline, model::Fields fields,
    std::vector<model::Item> const& items, std::vector<Choice> const& choices, model::Cancel const& cancel) {
    Plan plan;
    auto& report = plan.report;
    std::vector<AssDialogue const*> cur, base;
    for (auto const& line : current.Events) cur.push_back(&line);
    for (auto const& line : baseline.Events) base.push_back(&line);
    fields &= model::All;

    // ---- items must describe these two documents ----
    for (auto const& item : items) {
        bool ok = true;
        switch (item.kind) {
            case model::Kind::Added: ok = item.baseline && !item.current && item.insertion_boundary <= cur.size(); break;
            case model::Kind::Deleted: ok = !item.baseline && item.current; break;
            default: ok = item.baseline && item.current; break;
        }
        if (!ok || (item.baseline && *item.baseline >= base.size()) || (item.current && *item.current >= cur.size()))
            throw std::invalid_argument("Comparison items do not describe these documents");
    }

    // ---- choices ----
    struct Chosen { model::Item const* item; size_t index; model::Fields fields; };
    std::vector<Chosen> chosen;
    std::set<size_t> seen;
    for (auto const& choice : choices) {
        model::CheckCancelled(cancel);
        if (choice.item >= items.size()) { report.refusals.push_back({Problem::UnknownItem, choice.item, {}}); continue; }
        if (!seen.insert(choice.item).second) { report.refusals.push_back({Problem::DuplicateChoice, choice.item, {}}); continue; }
        auto const& item = items[choice.item];
        model::Fields f = 0;
        if (item.kind == model::Kind::Modified) f = choice.fields & item.changed & fields;
        if (item.kind == model::Kind::Unchanged || (item.kind == model::Kind::Modified && !f)) {
            report.refusals.push_back({Problem::NothingToApply, choice.item, {}});
            continue;
        }
        chosen.push_back({&item, choice.item, f});
    }

    // ---- extradata of both sides must resolve ----
    ExtraIndex cur_extra(current), base_extra(baseline);
    std::vector<std::vector<ExtradataEntry const*>> cur_rows, base_rows;
    auto resolve_all = [&](std::vector<AssDialogue const*> const& lines, ExtraIndex const& index,
                           std::vector<std::vector<ExtradataEntry const*>>& out) {
        for (auto line : lines) {
            model::CheckCancelled(cancel);
            auto r = index.Resolve(*line);
            if (!r) return false;
            out.push_back(std::move(*r));
        }
        return true;
    };
    if (!resolve_all(cur, cur_extra, cur_rows) || !resolve_all(base, base_extra, base_rows)) {
        report.refusals.push_back({Problem::InvalidExtradata, std::nullopt, {}});
        return plan;
    }

    // ---- per-row outcome ----
    // For every current row: deleted, modified (fields) or kept. For every
    // added baseline row: inserted at a boundary in baseline order.
    std::vector<bool> deleted(cur.size());
    std::vector<model::Fields> modified(cur.size());
    std::vector<std::optional<size_t>> modified_from(cur.size());
    std::vector<std::vector<size_t>> additions(cur.size() + 1); // boundary -> baseline rows
    std::vector<std::optional<size_t>> matched(cur.size());     // current row -> baseline row
    for (auto const& item : items)
        if (item.baseline && item.current) matched[*item.current] = *item.baseline;
    for (auto const& c : chosen) {
        auto const& item = *c.item;
        if (item.kind == model::Kind::Deleted) { deleted[*item.current] = true; ++report.deleted; }
        else if (item.kind == model::Kind::Added) { additions[item.insertion_boundary].push_back(*item.baseline); ++report.added; }
        else { modified[*item.current] = c.fields; modified_from[*item.current] = *item.baseline; ++report.modified; }
    }

    // Within one gap, additions follow the baseline order whatever the choice order.
    for (auto& gap : additions) std::sort(gap.begin(), gap.end());

    // Final rows, described before any node is built.
    struct Final {
        std::optional<size_t> current;  // origin in current, if any
        std::optional<size_t> baseline; // corresponding baseline row, if any
        model::Fields fields = 0;       // fields taken from baseline (Added: every supported field)
        bool extras_from_baseline = false;
    };
    std::vector<Final> finals;
    for (size_t b = 0; b <= cur.size(); ++b) {
        for (auto row : additions[b]) finals.push_back({std::nullopt, row, fields, (fields & model::Extra) != 0});
        if (b == cur.size() || deleted[b]) continue;
        finals.push_back({b, matched[b], modified[b], (modified[b] & model::Extra) != 0});
    }
    static std::vector<ExtradataEntry const*> const no_extras;
    auto final_extras = [&](Final const& f) -> std::vector<ExtradataEntry const*> const& {
        if (f.extras_from_baseline) return base_rows[*f.baseline];
        return f.current ? cur_rows[*f.current] : no_extras; // added without Extra support
    };

    // ---- binding groups: never leave one partial ----
    {
        std::map<std::string, std::set<size_t>> current_groups, baseline_groups;
        for (size_t i = 0; i < cur.size(); ++i) if (auto t = binding_of(cur_rows[i])) current_groups[*t].insert(i);
        for (size_t i = 0; i < base.size(); ++i) if (auto t = binding_of(base_rows[i])) baseline_groups[*t].insert(i);
        std::set<std::string> touched;
        std::vector<bool> survives(cur.size());
        for (auto const& f : finals) {
            auto now = binding_of(final_extras(f));
            if (f.current) {
                survives[*f.current] = true;
                auto before = binding_of(cur_rows[*f.current]);
                if (before != now) { if (before) touched.insert(*before); if (now) touched.insert(*now); }
            }
            else if (now) touched.insert(*now);
        }
        for (size_t i = 0; i < cur.size(); ++i)
            if (!survives[i]) if (auto t = binding_of(cur_rows[i])) touched.insert(*t);
        for (auto const& token : touched) {
            std::set<size_t> from_current, from_baseline;
            bool all_current = true, all_baseline = true;
            size_t members = 0;
            for (auto const& f : finals) {
                if (binding_of(final_extras(f)) != token) continue;
                ++members;
                if (f.current && binding_of(cur_rows[*f.current]) == token) from_current.insert(*f.current); else all_current = false;
                if (f.baseline && binding_of(base_rows[*f.baseline]) == token) from_baseline.insert(*f.baseline); else all_baseline = false;
            }
            bool intact = members == 0
                || (all_current && from_current == current_groups[token])
                || (all_baseline && from_baseline.size() == members && from_baseline == baseline_groups[token]);
            if (!intact) report.refusals.push_back({Problem::PartialBinding, std::nullopt, token});
        }
    }

    // ---- styles needed by applied rows ----
    std::vector<std::string> needed;
    for (auto const& f : finals) {
        if (!f.baseline || !f.fields) continue;
        auto const& line = *base[*f.baseline];
        if (f.fields & model::Style) add_unique(needed, line.Style.get());
        if (f.fields & model::Text)
            for (auto const& block : line.ParseTags())
                if (block->GetType() == AssBlockType::OVERRIDE)
                    reset_style_names(static_cast<AssDialogueBlockOverride const&>(*block), needed);
    }
    std::vector<AssStyle const*> imported;
    for (auto const& name : needed) {
        model::CheckCancelled(cancel);
        std::vector<AssStyle const*> from, to;
        for (auto const& s : baseline.Styles) if (boost::iequals(s.name, name)) from.push_back(&s);
        for (auto const& s : current.Styles) if (boost::iequals(s.name, name)) to.push_back(&s);
        // Same states and lookup rule as subtitle_compare::AnalyzeDependencies.
        if (from.size() > 1 || to.size() > 1) report.refusals.push_back({Problem::StyleAmbiguous, std::nullopt, name});
        else if (!to.empty()) {
            if (!from.empty() && !subtitle_compare::SameStyle(*from[0], *to[0]))
                report.refusals.push_back({Problem::StyleConflict, std::nullopt, name});
        }
        else if (from.empty()) add_unique(report.missing_styles, name);
        else if (std::find(imported.begin(), imported.end(), from[0]) == imported.end()) imported.push_back(from[0]);
    }
    if (!report.Ok()) return plan;

    // ---- extradata remap for rows whose extras come from the baseline ----
    std::vector<model::ExtraEntry> source, destination;
    {
        std::set<uint32_t> ids;
        for (auto const& f : finals)
            if (f.extras_from_baseline)
                for (auto e : base_rows[*f.baseline]) if (ids.insert(e->id).second) source.push_back({e->id, e->key, e->value});
        for (auto const& e : current.Extradata) destination.push_back({e.id, e.key, e.value});
    }
    auto mapping = model::PlanExtraMapping(source, destination, current.next_extradata_id, cancel);
    for (auto const& a : mapping.additions) plan.extradata_additions.push_back({a.id, a.key, a.value});
    plan.next_extradata_id = mapping.next_id;
    for (auto const& e : source)
        if (e.key != binding_key) add_unique(report.imported_keys, e.key);
    std::sort(report.imported_keys.begin(), report.imported_keys.end());

    // ---- build ----
    for (auto const& s : current.Styles) plan.styles.push_back(&s);
    for (auto s : imported) {
        auto copy = std::make_unique<AssStyle>(*s);
        plan.styles.push_back(copy.get());
        report.imported_styles.push_back(copy->name);
        plan.pool.push_back(std::move(copy));
    }
    for (auto const& f : finals) {
        model::CheckCancelled(cancel);
        if (f.current && !f.fields) { plan.events.push_back(cur[*f.current]); continue; }
        std::unique_ptr<AssDialogue> line;
        if (f.current) line = std::make_unique<AssDialogue>(static_cast<AssDialogueBase const&>(*cur[*f.current])); // keeps the row ID
        else line = std::make_unique<AssDialogue>(); // new row ID; unsupported fields keep defaults
        copy_fields(*line, *base[*f.baseline], f.fields);
        if (f.extras_from_baseline) {
            std::vector<uint32_t> ids;
            for (auto id : base[*f.baseline]->ExtradataIds.get()) ids.push_back(mapping.ids.at(id));
            line->ExtradataIds = ids;
        }
        else if (!f.current) line->ExtradataIds = std::vector<uint32_t>{};
        plan.applied_ids.push_back(line->Id);
        plan.events.push_back(line.get());
        plan.pool.push_back(std::move(line));
    }

    if (report.added || report.deleted) plan.commit_type |= AssFile::COMMIT_DIAG_ADDREM;
    if (report.modified) plan.commit_type |= AssFile::COMMIT_DIAG_FULL;
    if (!imported.empty()) plan.commit_type |= AssFile::COMMIT_STYLES;
    if (!plan.extradata_additions.empty()) plan.commit_type |= AssFile::COMMIT_EXTRADATA;
    return plan;
}
}
