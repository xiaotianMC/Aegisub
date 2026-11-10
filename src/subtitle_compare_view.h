// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
// C2-C diff editor: the model behind the side-by-side comparison window
// (dialog_compare.cpp), without any wx dependency so it can be tested directly.
//
// The window shows a base version on the left and a modified version on the
// right, one comparison item per aligned row. One side is the open project (the
// target); the other is the file read from disk. "Accept" keeps the right
// side's content, "Reject" keeps the left side's. Decisions are staged here and
// only Choices() reaches the Apply Engine, so browsing and deciding never write
// to the project; the window publishes them once, as one undoable step.
#include "subtitle_apply.h"

#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

class AssFile;

namespace subtitle_compare_view {
namespace model = agi::ass::compare;

enum class Side { Left, Right };
inline Side Other(Side s) { return s == Side::Left ? Side::Right : Side::Left; }

/// How an entry looks going from the left side to the right side.
enum class Change { Unchanged, Inserted, Removed, Modified };
enum class Decision { Pending, Accepted, Rejected, Partial };

/// Fields in display order with stable English identifiers (translated by the UI).
std::vector<model::Field> const& FieldOrder();
char const* FieldName(model::Field);

/// What kind of difference an entry is, for filtering and batch decisions.
/// The first four are modified lines whose (shown) changes all fall in one
/// field group; Multiple spans several groups.
enum class Category { Time, Text, StyleActor, OtherFields, Multiple, Inserted, Removed };
std::vector<Category> const& CategoryOrder();
/// Stable English identifier (translated by the UI).
char const* CategoryName(Category);
/// The field group of a single-group category (0 for the others).
model::Fields CategoryFields(Category);

/// What the window shows: fields left out of the comparison view, and
/// optionally a single category. Hidden differences stay pending.
struct Filter {
    model::Fields ignored = 0;
    std::optional<Category> category;
    bool Active() const { return ignored || category; }
};

/// Byte range of a UTF-8 string.
struct Span { size_t begin = 0, end = 0; };
struct TextDiff { std::vector<Span> left, right; };
/// Changed parts of two texts, by token: an override block, a \N-style escape,
/// an ASCII word, a whitespace run, or one other character (so CJK text diffs
/// by character). Very long unmatched middles are marked as a whole.
TextDiff DiffText(std::string const& left, std::string const& right);

/// One subtitle line as displayed on one side.
struct Line {
    size_t number = 0;                  ///< 1-based position in its document
    int start = 0, end = 0;             ///< times in milliseconds
    std::array<std::string, 12> values; ///< display value of each field, by bit index
    bool comment = false;
    std::string const& Value(model::Field field) const;
};

struct Entry {
    size_t item = 0;                    ///< index into the comparison items
    Change change = Change::Unchanged;
    model::Ambiguity ambiguity = model::Ambiguity::None;
    std::optional<Line> left, right;
    model::Fields changed = 0;          ///< Modified: changed comparison fields
    TextDiff text;                      ///< Modified with a Text change
    bool IsDiff() const { return change != Change::Unchanged; }
    std::optional<Line> const& Get(Side s) const { return s == Side::Left ? left : right; }
};

/// A row of the aligned two-pane layout: one entry, or a fold hiding
/// @p hidden unchanged entries starting at @p entry.
struct LayoutRow {
    size_t entry = 0;
    size_t hidden = 0;
    bool IsFold() const { return hidden > 0; }
};

class State {
    struct Choice {
        bool decided = false;
        model::Fields take = 0; ///< fields where the file's value wins (All for whole lines)
        bool operator==(Choice const& o) const { return decided == o.decided && take == o.take; }
    };
    struct Step { size_t entry; Choice before, after; };

    std::vector<Entry> entries;
    std::vector<size_t> diffs;          ///< entry indices of the differences
    std::vector<Choice> choices;        ///< by entry
    std::vector<bool> checked;          ///< by entry
    std::vector<std::vector<Step>> undo, redo;
    Side project = Side::Right;

    model::Fields Whole(size_t entry) const;
    Side FileSide() const { return Other(project); }
    void Commit(std::vector<Step> steps);
    Choice Target(size_t entry, Decision decision, model::Fields scope) const;
public:
    /// @param project which side shows the open project (the other shows the file)
    State(AssFile const& current, AssFile const& baseline, model::Fields comparison_fields,
        std::vector<model::Item> const& items, Side project = Side::Right);

    std::vector<Entry> const& Entries() const { return entries; }
    /// Entry indices of all differences, in display order.
    std::vector<size_t> const& Diffs() const { return diffs; }
    /// Position of @p entry in Diffs(), if it is a difference.
    std::optional<size_t> DiffIndex(size_t entry) const;

    Side ProjectSide() const { return project; }
    /// Swap which side the project is shown on. Decisions keep their effect on
    /// the project, so an accepted change becomes a rejected one and vice versa.
    void SetProjectSide(Side side);

    Decision GetDecision(size_t entry) const;
    /// Which side's value of @p field the result keeps; unset while pending.
    /// Whole-line entries ignore @p field.
    std::optional<Side> Winner(size_t entry, model::Field field = model::Text) const;
    size_t Count(Decision decision) const;
    /// The decision @p decision would change for any of @p entries (non-differences ignored).
    size_t Changing(std::vector<size_t> const& entries, Decision decision, model::Fields scope = model::All) const;

    /// Accepted, Rejected or Pending (reset) for every difference in @p entries,
    /// as one undo step. Returns false (and records nothing) if nothing changed.
    /// For modified lines, Accepted and Rejected touch only the fields in
    /// @p scope (the others keep their choice, or the project's value); Pending
    /// always resets the whole line.
    bool Decide(std::vector<size_t> const& entries, Decision decision, model::Fields scope = model::All);
    /// Modified entries only: keep @p winner's value of one changed field. A pending
    /// entry becomes decided with every other field left as the project has it.
    bool SetField(size_t entry, model::Field field, Side winner);
    bool CanUndo() const { return !undo.empty(); }
    bool CanRedo() const { return !redo.empty(); }
    bool Undo();
    bool Redo();

    bool Checked(size_t entry) const { return checked.at(entry); }
    void Check(size_t entry, bool value);
    void CheckAll(bool value);
    std::vector<size_t> CheckedEntries() const;

    /// What to give the Apply Engine: the decided entries whose result differs
    /// from the project. Pending entries leave the project as it is.
    std::vector<subtitle_apply::Choice> Choices() const;

    /// Changed fields still shown when @p ignored are left out (modified lines only).
    model::Fields Shown(size_t entry, model::Fields ignored = 0) const;
    /// The category of a difference under @p ignored; unset if it is not a
    /// difference, or only ignored fields changed.
    std::optional<Category> CategoryOf(size_t entry, model::Fields ignored = 0) const;
    /// Whether the window shows @p entry as a difference under @p filter.
    bool Visible(size_t entry, Filter const& filter) const;
    /// Entry indices of the differences shown under @p filter, in display order.
    std::vector<size_t> VisibleDiffs(Filter const& filter) const;

    /// Aligned rows. Unless @p show_all, runs of entries that are not shown as
    /// differences (under @p filter) keep @p context entries next to each
    /// difference and fold the rest (folds of a single entry are not made). A
    /// fold whose first hidden entry is in @p expanded is shown in full.
    std::vector<LayoutRow> Layout(bool show_all, std::set<size_t> const& expanded, size_t context = 3, Filter const& filter = {}) const;
};
}
