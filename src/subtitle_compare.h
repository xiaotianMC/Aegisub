// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include "ass_file.h"
#include <libaegisub/ass/version_compare.h>
#include <libaegisub/vfr.h>
#include <memory>

class SubtitleFormat;
namespace subtitle_compare {
namespace model = agi::ass::compare;
struct Projection {
    model::Document document;
    std::vector<model::ExtraIssue> extra_issues;
};
// Current in-memory documents should use All, regardless of their last save extension.
Projection ProjectDocument(AssFile const& file, model::Fields fields = model::All,
    model::Cancel const& cancel = {});
enum class StyleState { Existing, Importable, Conflict, Missing, Ambiguous };
struct StyleDependency { std::string name; StyleState state; };
struct BindingAnalysis {
    std::string token;
    size_t baseline_members = 0, selected_members = 0, current_members = 0;
};
struct Dependencies {
    std::vector<StyleDependency> styles;
    std::vector<model::ExtraIssue> extra_issues;
    std::vector<BindingAnalysis> bindings;
};
// Analysis only. Importable is NOT approval to apply a binding/opaque value.
Dependencies AnalyzeDependencies(AssFile const& baseline, AssFile const& current,
    std::vector<size_t> const& selected_baseline_rows, model::Cancel const& cancel = {});
enum class LoadState { Completed, Cancelled, NeedsEncoding, Failed };
struct Baseline {
    LoadState state = LoadState::Failed;
    std::unique_ptr<AssFile> file;
    model::Fields fields = 0;
    std::string error;
};
// Select a reader using existing GetReader on the host thread. Readers which
// require FPS/text/track choices must also run there; this API creates no UI.
Baseline ReadBaseline(SubtitleFormat const& reader, agi::fs::path const& path,
    agi::vfr::Framerate const& fps, std::string encoding = {}, model::Cancel const& cancel = {});
}
