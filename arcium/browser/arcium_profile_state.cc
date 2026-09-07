// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"

#include <memory>
#include <utility>

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
  // An incognito BrowserContext is a separate context and so gets a separate
  // state: its own empty model, which dies with the window.
  auto owned = base::WrapUnique(
      new ArciumProfileState(context->GetPath(), context->IsOffTheRecord()));
  state = owned.get();
  context->SetUserData(kArciumProfileStateKey, std::move(owned));
  if (state->store_) {
    // Reads on a background sequence; the sidebar draws live tabs meanwhile
    // and entries appear when the read completes. Once per profile, not per
    // window.
    state->store_->Load(base::DoNothing());
  }
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

ArciumProfileState::ArciumProfileState(const base::FilePath& profile_path,
                                       bool off_the_record) {
  if (!off_the_record) {
    store_ = std::make_unique<ModelStore>(&model_, ModelPath(profile_path));
  }
}

ArciumProfileState::~ArciumProfileState() = default;

}  // namespace arcium
