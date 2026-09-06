// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"

#include <memory>

#include "base/functional/callback_helpers.h"
#include "base/memory/ptr_util.h"
#include "content/public/browser/browser_context.h"

namespace arcium {

namespace {

// The address of this object is the key; its value is never read.
const char kArciumProfileStateKey[] = "arcium_profile_state";

}  // namespace

// static
ArciumProfileState* ArciumProfileState::GetForBrowserContext(
    content::BrowserContext* context) {
  auto* state = static_cast<ArciumProfileState*>(
      context->GetUserData(kArciumProfileStateKey));
  if (state) {
    return state;
  }
  auto owned = base::WrapUnique(new ArciumProfileState(context->GetPath()));
  state = owned.get();
  context->SetUserData(kArciumProfileStateKey, std::move(owned));
  // Reads on a background sequence; the sidebar draws live tabs meanwhile and
  // entries appear when the read completes. Once per profile, not per window.
  state->store_.Load(base::DoNothing());
  return state;
}

// static
base::FilePath ArciumProfileState::ModelPath(
    const base::FilePath& profile_path) {
  return profile_path.Append(FILE_PATH_LITERAL("Arcium Model"));
}

// static
base::FilePath ArciumProfileState::ArchivePath(
    const base::FilePath& profile_path) {
  return profile_path.Append(FILE_PATH_LITERAL("Arcium Archive"));
}

ArciumProfileState::ArciumProfileState(const base::FilePath& profile_path)
    : store_(&model_, ModelPath(profile_path)) {}

ArciumProfileState::~ArciumProfileState() = default;

}  // namespace arcium
