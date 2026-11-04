// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
// C2-B comparison UI: display rows and choice state for a comparison, without
// any wx dependency so it can be tested directly. The dialog (dialog_compare.cpp)
// only renders this and forwards user actions.
#include "subtitle_apply.h"

#include <optional>
#include <string>
#include <vector>

class AssFile;

namespace subtitle_compare_view {
namespace model = agi::ass::compare;

struct FieldDiff {
    model::Field field;
    std::string current, baseline; ///< display values (UTF-8)
};

/// One displayed change (Unchanged items are not listed).
struct Row {
    size_t item = 0;                       ///< index into the comparison items
    model::Kind kind = model::Kind::Unchanged;
    model::Ambiguity ambiguity = model::Ambiguity::None;
    std::optional<size_t> current_row, baseline_row; ///< 1-based line numbers
    std::string time;                      ///< "start - end" of the baseline side if any, else current
    std::string current_text, baseline_text;
    std::vector<FieldDiff> diffs;          ///< Modified only, in field order
};

/// Fields in display order with stable English identifiers (translated by the UI).
std::vector<model::Field> const& FieldOrder();
char const* FieldName(model::Field);

class State {
    std::vector<Row> rows;
    std::vector<bool> checked;
    std::vector<model::Fields> fields;
public:
    State(AssFile const& current, AssFile const& baseline, model::Fields comparison_fields,
        std::vector<model::Item> const& items);
    std::vector<Row> const& Rows() const { return rows; }
    size_t Count(std::optional<model::Kind> kind = std::nullopt) const;

    bool Checked(size_t row) const { return checked.at(row); }
    /// Checking a Modified row whose field set is empty restores all its changed fields.
    void Check(size_t row, bool value);
    /// Rows matching @p kind (all rows if unset).
    void CheckAll(bool value, std::optional<model::Kind> kind = std::nullopt);
    size_t CheckedCount() const;

    /// Modified rows only: the changed fields to take from the baseline.
    model::Fields Fields(size_t row) const { return fields.at(row); }
    /// Clearing the last field unchecks the row. Setting one checks the row; on an
    /// unchecked row (which shows no field as chosen) it becomes the only field.
    void SetField(size_t row, model::Field field, bool value);

    std::vector<subtitle_apply::Choice> Choices() const;
};
}
