// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace agi::ass::compare {
using Fields = uint32_t;
enum Field : Fields {
    Text = 1 << 0, Start = 1 << 1, End = 1 << 2, Layer = 1 << 3,
    Style = 1 << 4, Actor = 1 << 5, MarginL = 1 << 6, MarginR = 1 << 7,
    MarginV = 1 << 8, Effect = 1 << 9, Comment = 1 << 10, Extra = 1 << 11,
    All = (1 << 12) - 1
};
using Extras = std::map<std::string, std::string>;
struct Row {
    std::string text, style, actor, effect;
    int start_ms = 0, end_ms = 0, layer = 0;
    std::array<int, 3> margins{};
    bool comment = false;
    Extras extra;
};
enum class Format { Ass, Ssa, Srt, MicroDvd, Ttxt, Txt, Unknown };
Fields Capabilities(Format format);
struct Document {
    std::vector<Row> rows;
    Fields fields = All;
};
enum class Kind { Unchanged, Added, Deleted, Modified };
enum class MatchReason { None, EqualSequence, ExactFields, UniqueTime, UniqueText };
enum class Ambiguity { None, Duplicate, Crossing, ConflictingEvidence, NoReliableMatch };
struct Item {
    std::optional<size_t> baseline, current;
    Kind kind = Kind::Unchanged;
    Fields changed = 0;
    MatchReason reason = MatchReason::None;
    Ambiguity ambiguity = Ambiguity::None;
    // Boundary in the ORIGINAL current sequence [0, current.rows.size()].
    size_t insertion_boundary = 0;
};
using Cancel = std::function<bool()>;
class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("Subtitle comparison cancelled") { }
};
void CheckCancelled(Cancel const& cancel);
Fields ChangedFields(Row const& baseline, Row const& current, Fields fields);
std::vector<Item> Compare(Document const& baseline, Document const& current, Cancel const& cancel = {});

struct ExtraEntry {
    uint32_t id;
    std::string key, value;
};
enum class ExtraIssueKind { DuplicateId, MissingId, DuplicateKey, UnknownDependency, BindingPolicyRequired };
struct ExtraIssue {
    ExtraIssueKind kind;
    std::optional<uint32_t> id;
    std::string key;
};
struct ExtraAnalysis {
    std::vector<Extras> rows;
    std::vector<ExtraIssue> issues;
    bool valid = true;
};
ExtraAnalysis ResolveExtras(std::vector<ExtraEntry> const& entries,
    std::vector<std::vector<uint32_t>> const& references, Cancel const& cancel = {});
// A numeric mapping plan only: this does NOT authorize importing opaque values
// or choose a policy for binding groups. Neither input is modified.
struct ExtraMapping {
    std::map<uint32_t, uint32_t> ids;
    std::vector<ExtraEntry> additions;
    uint32_t next_id = 0;
};
ExtraMapping PlanExtraMapping(std::vector<ExtraEntry> const& source,
    std::vector<ExtraEntry> const& destination, uint32_t next_id, Cancel const& cancel = {});
}
