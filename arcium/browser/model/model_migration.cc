// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_migration.h"

#include <iterator>
#include <utility>

#include "arcium/browser/model/model_serializer.h"
#include "base/containers/span.h"

namespace arcium {

namespace {

// Version 1 -> 2: folders gained an optional `parent_id`.
//
// Deliberately content-free. Every folder in a version 1 file is at the top
// level, and an absent `parent_id` is exactly what the top level means, so
// there is nothing to rewrite. The step earns its place by moving the version
// and by being the shape the next one copies: Stage 3 puts a real space
// dimension into this same file, and a pipeline first exercised by that
// migration would be first exercised on the migration that matters.
bool MigrateV1ToV2(base::DictValue&) {
  return true;
}

// Version 2 -> 3: spaces gained an icon, a gradient preset and a last active
// tab, and the model gained a last active space.
//
// Unlike the step before it this one writes: a version 2 space has no
// `gradient` key at all, and a reader that defaults a missing key and a
// migration that writes the default disagree the moment the default changes.
// The last active tab and space are deliberately left absent — a file written
// before spaces existed has no honest answer, and both fall back to the first
// space.
bool MigrateV2ToV3(base::DictValue& dict) {
  base::ListValue* spaces = dict.FindList("spaces");
  if (!spaces) {
    return true;
  }
  for (base::Value& item : *spaces) {
    base::DictValue* space = item.GetIfDict();
    if (!space) {
      continue;
    }
    space->Set("icon", "");
    space->Set("gradient", 0);
  }
  return true;
}

// Indexed by source version: kSteps[0] takes a version 1 dict to version 2.
using MigrationStep = bool (*)(base::DictValue&);
constexpr MigrationStep kSteps[] = {&MigrateV1ToV2, &MigrateV2ToV3};
static_assert(
    std::size(kSteps) == static_cast<size_t>(kModelSchemaVersion) - 1,
    "Bumping kModelSchemaVersion needs a step that gets a file there");

}  // namespace

std::optional<base::DictValue> MigrateModelDict(base::DictValue dict) {
  const std::optional<int> version = dict.FindInt("version");
  // No version is not a model file this build can reason about; a version
  // from the future would be half-read rather than migrated, and half-read is
  // the one outcome worse than empty.
  if (!version || *version < 1 || *version > kModelSchemaVersion) {
    return std::nullopt;
  }
  // Through a span rather than subscripting the array directly: a raw
  // subscript with a computed index is what -Wunsafe-buffer-usage exists to
  // catch, and span's own operator[] checks the bound it was built from.
  const base::span<const MigrationStep> steps(kSteps);
  for (int from = *version; from < kModelSchemaVersion; ++from) {
    if (!steps[from - 1](dict)) {
      return std::nullopt;
    }
    // Stamped by the pipeline rather than by each step, so a step cannot
    // forget and leave the loop running forever.
    dict.Set("version", from + 1);
  }
  return dict;
}

}  // namespace arcium
