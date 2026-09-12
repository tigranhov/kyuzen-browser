// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Profiles: the list itself, which space uses which, and keeping both
// invariants true after a mutation -- Default first and always present, and
// no space left pointing at an id that has gone. The rest of ArciumModel --
// spaces, entries, folders in arcium_model.cc -- never needs to know how a
// profile is stored beyond calling GetProfile to check one still exists.

#include <algorithm>
#include <optional>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/space.h"

namespace arcium {

namespace {

ArciumProfile MakeDefaultProfile() {
  ArciumProfile profile;
  profile.id = DefaultProfileId();
  profile.name = u"Default";
  return profile;
}

}  // namespace

const ArciumProfile* ArciumModel::GetProfile(ProfileId id) const {
  for (const ArciumProfile& profile : profiles_) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

ProfileId ArciumModel::ProfileOfSpace(SpaceId space) const {
  const Space* found = GetSpace(space);
  return found && GetProfile(found->profile_id) ? found->profile_id
                                                : DefaultProfileId();
}

ProfileId ArciumModel::AddProfile(const std::u16string& name, int color) {
  ArciumProfile profile;
  profile.id = ProfileId::Generate();
  profile.name = name;
  profile.color = color;
  profile.position = static_cast<int>(profiles_.size());
  const ProfileId id = profile.id;
  profiles_.push_back(std::move(profile));
  Notify();
  return id;
}

void ArciumModel::RenameProfile(ProfileId id, const std::u16string& name) {
  ArciumProfile* profile = FindProfile(id);
  if (!profile || profile->name == name) {
    return;
  }
  profile->name = name;
  Notify();
}

void ArciumModel::SetProfileColor(ProfileId id, int color) {
  ArciumProfile* profile = FindProfile(id);
  if (!profile || profile->color == color) {
    return;
  }
  profile->color = color;
  Notify();
}

void ArciumModel::RemoveProfile(ProfileId id) {
  if (id == DefaultProfileId() || !GetProfile(id)) {
    return;
  }
  std::erase_if(profiles_, [id](const ArciumProfile& p) { return p.id == id; });
  NormaliseProfiles();
  Notify();
}

void ArciumModel::SetSpaceProfile(SpaceId space_id, ProfileId profile) {
  Space* space = FindSpace(space_id);
  if (!space || !GetProfile(profile) || space->profile_id == profile) {
    return;
  }
  space->profile_id = profile;
  Notify();
}

ArciumProfile* ArciumModel::FindProfile(ProfileId id) {
  for (ArciumProfile& profile : profiles_) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

void ArciumModel::NormaliseProfiles() {
  // Keep the first Default the list holds, whole -- its name and colour are
  // the owner's to change and must survive a save -- and drop any second
  // copy and any row with an unusable id.
  std::optional<ArciumProfile> saved_default;
  std::erase_if(profiles_, [&](const ArciumProfile& p) {
    if (p.id == DefaultProfileId()) {
      if (!saved_default) {
        saved_default = p;
      }
      return true;
    }
    return !p.id.is_valid();
  });
  std::sort(profiles_.begin(), profiles_.end(),
            [](const ArciumProfile& a, const ArciumProfile& b) {
              return a.position < b.position;
            });
  profiles_.insert(profiles_.begin(),
                   saved_default ? *saved_default : MakeDefaultProfile());
  for (size_t i = 0; i < profiles_.size(); ++i) {
    profiles_[i].position = static_cast<int>(i);
  }
  for (Space& space : spaces_) {
    if (!GetProfile(space.profile_id)) {
      space.profile_id = DefaultProfileId();
    }
  }
}

}  // namespace arcium
