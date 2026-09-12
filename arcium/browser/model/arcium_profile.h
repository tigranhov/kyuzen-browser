// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_ARCIUM_PROFILE_H_
#define ARCIUM_BROWSER_MODEL_ARCIUM_PROFILE_H_

#include <string>

#include "arcium/browser/model/entry_id.h"

namespace arcium {

// The Default profile's id. Fixed rather than generated, so every build and
// every file names it the same way and no generated id can collide with it.
// Well formed, because TypedId::FromString refuses anything that is not.
inline constexpr char kDefaultProfileIdValue[] =
    "00000000-0000-4000-8000-000000000000";

inline ProfileId DefaultProfileId() {
  return ProfileId::FromString(kDefaultProfileIdValue);
}

// An Arcium profile: a set of logins inside the one Chromium profile. The
// Default profile is Chromium's default storage partition; every other
// profile is a partition of its own, named from its id
// (arcium/browser/profile_partition.h). Not to be confused with
// ArciumProfileState, which is Arcium's per-Chromium-profile state holder.
struct ArciumProfile {
  ProfileId id;
  std::u16string name;
  // An index into the fixed palette in arcium/ui/sidebar/profile_colors.h.
  int color = 0;
  int position = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ARCIUM_PROFILE_H_
