// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_compare.h"

#include "ass_dialogue.h"
#include "ass_style.h"
#include "subtitle_format.h"
#include <libaegisub/charset.h>
#include <boost/algorithm/string/predicate.hpp>
#include <algorithm>
#include <set>
#include <tuple>

namespace subtitle_compare {
namespace {
std::vector<model::ExtraEntry> entries(AssFile const& file) {
    std::vector<model::ExtraEntry> result;
    for (auto const& e : file.Extradata) result.push_back({e.id, e.key, e.value});
    return result;
}
model::ExtraAnalysis extras(AssFile const& file, model::Cancel const& cancel) {
    std::vector<std::vector<uint32_t>> refs;
    for (auto const& line : file.Events) { model::CheckCancelled(cancel); refs.push_back(line.ExtradataIds.get()); }
    return model::ResolveExtras(entries(file), refs, cancel);
}
bool same_style(AssStyle const& a, AssStyle const& b) {
    auto values = [](AssStyle const& s) {
        return std::tie(s.font, s.fontsize, s.primary, s.secondary, s.outline, s.shadow,
            s.bold, s.italic, s.underline, s.strikeout, s.scalex, s.scaley, s.spacing,
            s.angle, s.borderstyle, s.outline_w, s.shadow_w, s.alignment, s.Margin, s.encoding);
    };
    return values(a) == values(b);
}
void reset_styles(AssDialogueBlockOverride const& block, std::set<std::string>& names, model::Cancel const& cancel) {
    for (auto const& tag : block.Tags) {
        model::CheckCancelled(cancel);
        if (tag.IsValid() && tag.Name == "\\r" && !tag.Params.empty() && !tag.Params[0].omitted) {
            auto name = tag.Params[0].Get<std::string>();
            if (!name.empty()) names.insert(std::move(name));
        }
        for (auto const& param : tag.Params)
            if (!param.omitted && param.GetType() == VariableDataType::BLOCK)
                reset_styles(*param.Get<AssDialogueBlockOverride*>(), names, cancel);
    }
}
}

Projection ProjectDocument(AssFile const& file, model::Fields fields, model::Cancel const& cancel) {
    Projection out; out.document.fields = fields;
    auto extra = extras(file, cancel);
    if (!extra.valid) throw std::invalid_argument("Invalid extradata IDs or duplicate per-line keys");
    out.extra_issues = std::move(extra.issues);
    size_t i = 0;
    for (auto const& line : file.Events) {
        model::CheckCancelled(cancel); model::Row row;
        row.text = line.Text.get(); row.style = line.Style.get(); row.actor = line.Actor.get(); row.effect = line.Effect.get();
        row.start_ms = line.Start.GetMilliseconds(); row.end_ms = line.End.GetMilliseconds();
        row.layer = line.Layer; row.margins = line.Margin; row.comment = line.Comment;
        row.extra = std::move(extra.rows[i++]); out.document.rows.push_back(std::move(row));
    }
    return out;
}

Dependencies AnalyzeDependencies(AssFile const& baseline, AssFile const& current,
    std::vector<size_t> const& selected_baseline_rows, model::Cancel const& cancel) {
    Dependencies result;
    auto base_extra = extras(baseline, cancel), current_extra = extras(current, cancel);
    if (!base_extra.valid || !current_extra.valid) throw std::invalid_argument("Cannot analyze invalid extradata");
    std::set<size_t> selected(selected_baseline_rows.begin(), selected_baseline_rows.end());
    if (!selected.empty() && *selected.rbegin() >= base_extra.rows.size()) throw std::out_of_range("Baseline row index");
    std::set<std::string> names;
    std::map<std::string, BindingAnalysis> bindings;
    auto binding_key = "aegisub.enhanced.subtitle-binding";
    for (auto const& row : base_extra.rows) {
        auto it = row.find(binding_key);
        if (it != row.end()) { auto& b = bindings[it->second]; b.token = it->second; ++b.baseline_members; }
    }
    for (auto const& row : current_extra.rows) {
        auto it = row.find(binding_key);
        if (it != row.end() && bindings.count(it->second)) ++bindings.at(it->second).current_members;
    }
    size_t i = 0;
    for (auto const& line : baseline.Events) {
        model::CheckCancelled(cancel);
        if (!selected.count(i++)) continue;
        names.insert(line.Style.get());
        for (auto const& block : line.ParseTags())
            if (block->GetType() == AssBlockType::OVERRIDE)
                reset_styles(static_cast<AssDialogueBlockOverride const&>(*block), names, cancel);
        auto binding = base_extra.rows[i-1].find(binding_key);
        if (binding != base_extra.rows[i-1].end()) ++bindings.at(binding->second).selected_members;
        for (auto const& e : base_extra.rows[i-1]) result.extra_issues.push_back({
            e.first == binding_key ? model::ExtraIssueKind::BindingPolicyRequired : model::ExtraIssueKind::UnknownDependency, {}, e.first});
    }
    for (auto const& [token, b] : bindings) if (b.selected_members) result.bindings.push_back(b);
    for (auto const& name : names) {
        model::CheckCancelled(cancel);
        std::vector<AssStyle const*> from, to;
        for (auto const& s : baseline.Styles) if (boost::iequals(s.name, name)) from.push_back(&s);
        for (auto const& s : current.Styles) if (boost::iequals(s.name, name)) to.push_back(&s);
        StyleState state;
        if (from.size() > 1 || to.size() > 1) state = StyleState::Ambiguous;
        else if (!to.empty()) state = from.empty() || same_style(*from[0], *to[0]) ? StyleState::Existing : StyleState::Conflict;
        else state = from.empty() ? StyleState::Missing : StyleState::Importable;
        result.styles.push_back({name, state});
    }
    return result;
}

Baseline ReadBaseline(SubtitleFormat const& reader, agi::fs::path const& path,
    agi::vfr::Framerate const& fps, std::string encoding, model::Cancel const& cancel) {
    Baseline result;
    try {
        model::CheckCancelled(cancel);
        if (encoding.empty()) encoding = agi::charset::Detect(path);
        if (encoding.empty()) { result.state = LoadState::NeedsEncoding; return result; }
        if (!reader.CanReadFile(path, encoding.c_str())) throw std::invalid_argument("Selected reader does not support baseline file");
        auto extension = path.extension().string();
        model::Format format = model::Format::Unknown;
        if (boost::iequals(extension, ".ass")) format = model::Format::Ass;
        else if (boost::iequals(extension, ".ssa")) format = model::Format::Ssa;
        else if (boost::iequals(extension, ".srt")) format = model::Format::Srt;
        else if (boost::iequals(extension, ".sub")) format = model::Format::MicroDvd;
        else if (boost::iequals(extension, ".ttxt")) format = model::Format::Ttxt;
        else if (boost::iequals(extension, ".txt")) format = model::Format::Txt;
        result.fields = model::Capabilities(format);
        auto file = std::make_unique<AssFile>();
        reader.ReadFile(file.get(), path, fps, encoding.c_str(), {true, cancel, &result.fields});
        model::CheckCancelled(cancel);
        if (!result.fields) throw std::invalid_argument("Reader did not report comparable source fields");
        // Validate before publishing even an independently loaded baseline.
        ProjectDocument(*file, result.fields, cancel);
        result.file = std::move(file); result.state = LoadState::Completed;
    }
    catch (model::Cancelled const&) { result.state = LoadState::Cancelled; }
    catch (agi::UserCancelException const&) { result.state = LoadState::Cancelled; }
    catch (agi::Exception const& e) { result.error = e.GetMessage(); }
    catch (std::exception const& e) { result.error = e.what(); }
    return result;
}
}
