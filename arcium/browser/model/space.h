// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_SPACE_H_
#define ARCIUM_BROWSER_MODEL_SPACE_H_

#include <optional>
#include <string>

#include "arcium/browser/model/entry_id.h"
#include "base/time/time.h"

namespace arcium {

// How long a Today tab may sit idle before it is archived.
enum class ArchiveTimeout { kTwelveHours, kOneDay, kSevenDays, kNever };

// Returns std::nullopt for kNever, which means no expiry is ever scheduled.
std::optional<base::TimeDelta> ArchiveTimeoutToDelta(ArchiveTimeout timeout);

// Stage 2 has exactly one space. The id travels through the model from the
// start so Stage 3 is a UI change rather than a data migration.
struct Space {
  SpaceId id;
  std::u16string name;
  ArchiveTimeout archive_timeout = ArchiveTimeout::kTwelveHours;
  int position = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_SPACE_H_
