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
inline constexpr int kModelSchemaVersion = 1;

base::DictValue SerializeModel(const ArciumModel& model);

// Returns false only when the file is unusable as a whole: a missing or newer
// version. Individual malformed entries are dropped, because losing one row
// beats refusing to start.
bool DeserializeModel(const base::DictValue& dict, ArciumModel* model);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_MODEL_SERIALIZER_H_
