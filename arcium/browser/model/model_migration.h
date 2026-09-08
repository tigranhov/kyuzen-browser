// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_MODEL_MIGRATION_H_
#define ARCIUM_BROWSER_MODEL_MODEL_MIGRATION_H_

#include <optional>

#include "base/values.h"

namespace arcium {

// Brings a model file's dict up to kModelSchemaVersion, one version at a
// time. Pure: no file, no model, no observers. ModelStore runs it on the
// background sequence that read the file, which is what keeps the promise
// that a migration never runs on the UI thread -- and means the UI thread
// never sees a dict that is not current.
//
// std::nullopt means the dict cannot be brought up: a missing or nonsensical
// version, a version newer than this build's, or a step that refused. The
// caller's answer to that is an empty model, never a half-migrated one.
std::optional<base::DictValue> MigrateModelDict(base::DictValue dict);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_MODEL_MIGRATION_H_
