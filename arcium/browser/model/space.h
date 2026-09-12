// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_SPACE_H_
#define ARCIUM_BROWSER_MODEL_SPACE_H_

#include <optional>
#include <string>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/entry_id.h"
#include "base/time/time.h"

namespace arcium {

// How long a Today tab may sit idle before it is archived.
enum class ArchiveTimeout { kTwelveHours, kOneDay, kSevenDays, kNever };

// Returns std::nullopt for kNever, which means no expiry is ever scheduled.
std::optional<base::TimeDelta> ArchiveTimeoutToDelta(ArchiveTimeout timeout);

// A space owns its favourites, pins, folders and Today. Stage 3a made it
// several; the id travelled through the model from Stage 2 so this was a UI
// change rather than a data migration.
struct Space {
  SpaceId id;
  std::u16string name;
  // One emoji, or empty to draw the first letter of the name.
  std::u16string icon;
  // An index into the fixed palette in arcium/ui/sidebar/space_gradients.h.
  // 0 is the sidebar's original pair, so a space that never chose one looks
  // exactly as it did before spaces existed.
  int gradient = 0;
  ArchiveTimeout archive_timeout = ArchiveTimeout::kTwelveHours;
  int position = 0;
  // The tab a switch to this space lands on. A TabKey and not a SessionID:
  // Chromium's tab ids do not survive a restart (Stage 2 finding 1) and this
  // has to name a Today tab across one.
  TabKey last_active_tab;
  // The profile whose logins this space's tabs use. Defaulted in the struct,
  // not by callers: Space() is default-constructed to read defaults from
  // (ArchiveService::TimeoutForTab), and an invalid id there would be a space
  // with no storage at all.
  ProfileId profile_id = DefaultProfileId();
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_SPACE_H_
