// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
// C2 Apply Engine planner: turns a set of chosen comparison items into a
// complete replacement of the current document's styles, events and extradata.
// Pure: reads two independent models and returns new nodes; nothing it is
// given is modified. Publication belongs to SubsController::ApplyComparison.
// See docs/subtitle-apply-engine.md for the confirmed policies.
#include "ass_file.h"
#include <libaegisub/ass/version_compare.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class AssDialogue;
class AssStyle;

namespace subtitle_apply {
namespace model = agi::ass::compare;

/// One comparison item the user chose to apply. For Modified items, @p fields
/// limits which of the item's changed fields are taken from the baseline;
/// Added and Deleted items always apply as a whole.
struct Choice {
    size_t item = 0;
    model::Fields fields = model::All;
};

enum class Problem {
    UnknownItem,      ///< choice index out of range
    DuplicateChoice,  ///< the same item chosen twice
    NothingToApply,   ///< Unchanged item, or a Modified choice selecting no changed field
    PartialBinding,   ///< a binding group would end up neither intact nor exactly the baseline group
    StyleConflict,    ///< a needed style is defined differently in current and baseline
    StyleAmbiguous,   ///< a needed style name is defined more than once
    InvalidExtradata, ///< baseline or current extradata cannot be resolved
};
struct Refusal {
    Problem problem;
    std::optional<size_t> item; ///< item index, when the refusal concerns one item
    std::string detail;         ///< style name or binding token, when relevant
};

struct Report {
    std::vector<Refusal> refusals;
    size_t modified = 0, added = 0, deleted = 0;
    /// Baseline style definitions copied into the document
    std::vector<std::string> imported_styles;
    /// Style names used by applied rows that neither side defines (kept, as ASS allows)
    std::vector<std::string> missing_styles;
    /// Extradata keys imported with applied rows (opaque values, remapped IDs)
    std::vector<std::string> imported_keys;
    bool Ok() const { return refusals.empty(); }
};

/// A complete plan. When Ok(), styles and events give the document's final
/// lists in order: each pointer is either a node of the @p current model passed
/// to Prepare (kept unchanged) or a node owned by @p pool.
struct Plan {
    Report report;
    std::vector<std::unique_ptr<AssEntry>> pool;
    std::vector<AssStyle const*> styles;
    std::vector<AssDialogue const*> events;
    /// Entries to append to the current extradata, in increasing ID order
    std::vector<ExtradataEntry> extradata_additions;
    uint32_t next_extradata_id = 0;
    int commit_type = 0;
    /// IDs of the rows the plan modified or added, in final document order
    std::vector<int> applied_ids;
    bool Modifies() const { return report.Ok() && (report.modified || report.added || report.deleted); }
};

/// @param current  the document the items were compared against (projected with All)
/// @param baseline the baseline document
/// @param fields   the comparison's field capabilities (baseline fields)
/// @param items    Compare(baseline, current) for exactly these two documents
/// Refusals are collected (not thrown); the plan then carries no result.
/// Throws std::invalid_argument if @p items do not describe @p current/@p baseline.
Plan Prepare(AssFile const& current, AssFile const& baseline, model::Fields fields,
    std::vector<model::Item> const& items, std::vector<Choice> const& choices,
    model::Cancel const& cancel = {});
}
