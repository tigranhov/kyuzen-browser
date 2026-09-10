// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_MODEL_SERIALIZER_H_
#define ARCIUM_BROWSER_MODEL_MODEL_SERIALIZER_H_

#include "base/values.h"

namespace arcium {

class ArciumModel;

// Bumped whenever a field changes meaning. A file claiming a newer version is
// refused rather than half-read, so a downgrade cannot silently drop data.
//
// 2: folders gained `parent_id`. The bump is not for reading -- an absent
//    parent is exactly what a version 1 folder meant -- but for writing:
//    without it a Stage 2 build would open a nested file, draw every folder
//    flat, and flatten the tree for good on its next save.
// 3: spaces gained `icon`, `gradient` and `last_active_tab`, and the model
//    gained `last_active_space`. Bumped for the same reason 2 was: a Stage 2
//    build opening a Stage 3 file would drop all four on its next save.
inline constexpr int kModelSchemaVersion = 3;

base::DictValue SerializeModel(const ArciumModel& model);

// Returns false only when the file is unusable as a whole: a missing or
// newer version. Everything else is row-level damage and is recovered rather
// than refused: a malformed space, folder or entry is dropped, and if every
// space is dropped this way a default space is synthesised so that entries
// and folders in an otherwise-intact file are not thrown away along with a
// corrupt spaces list.
bool DeserializeModel(const base::DictValue& dict, ArciumModel* model);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_MODEL_SERIALIZER_H_
