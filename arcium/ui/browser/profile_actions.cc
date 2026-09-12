// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/profile_actions.h"

#include <string>
#include <utility>
#include <vector>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/profile_reopen.h"
#include "arcium/ui/browser/session_rebuild_nudge.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"

namespace arcium {

namespace {

// The partition's HTTP cache is not inside the partition: Chromium keeps it
// under the user's cache directory, which on macOS is a different tree
// altogether. Nothing else deletes it, so a profile that goes takes it.
void DeleteProfileCache(Profile* profile, const ProfileId& id) {
  const base::FilePath partition_path =
      PartitionDirectory(profile->GetPath(), id)
          .Append(FILE_PATH_LITERAL("def"));
  base::FilePath cache_path;
  chrome::GetUserCacheDirectory(partition_path, &cache_path);
  const base::FilePath domain_directory = cache_path.DirName();
  // GetUserCacheDirectory hands back what it was given when it cannot map
  // it, so the guard is on the name: never delete a directory that is not
  // this profile's own.
  if (!IsArciumPartitionDomain(domain_directory.BaseName().MaybeAsASCII())) {
    return;
  }
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(base::IgnoreResult(&base::DeletePathRecursively),
                     domain_directory));
}

// A partition something opened this session cannot be deleted, only
// emptied. Chrome's own callers answer this by asking for the cleanup
// sweep at the next launch, which is the only thing that removes such a
// directory.
void AskForCleanupAtNextLaunch(base::WeakPtr<Profile> profile) {
  if (profile) {
    profile->GetPrefs()->SetBoolean(
        prefs::kShouldGarbageCollectStoragePartitions, true);
  }
}

void ObliterateWhenCleared(base::WeakPtr<Profile> profile,
                           const std::string& partition_domain) {
  if (!profile) {
    return;
  }
  profile->AsyncObliterateStoragePartition(
      partition_domain, base::BindOnce(&AskForCleanupAtNextLaunch, profile),
      base::DoNothing());
}

}  // namespace

void MoveSpaceToProfile(content::BrowserContext* context,
                        SpaceId space,
                        ProfileId profile) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(context);
  if (!state || context->IsOffTheRecord()) {
    return;
  }
  ArciumModel* model = state->model();
  if (!model->GetSpace(space) || !model->GetProfile(profile) ||
      model->ProfileOfSpace(space) == profile) {
    return;
  }
  model->SetSpaceProfile(space, profile);

  Profile* chrome_profile = Profile::FromBrowserContext(context);
  ProfileBrowserCollection::GetForProfile(chrome_profile)
      ->ForEach([&](BrowserWindowInterface* window) {
        TabStripModel* strip = window->GetTabStripModel();
        for (int i = 0; i < strip->count(); ++i) {
          if (SpaceOfTab(*model, *state->binding(),
                         strip->GetTabAtIndex(i)->GetHandle()) == space) {
            // In place, so the index still names the same tab afterwards.
            ReopenTabInProfile(strip, i, profile);
          }
        }
        return true;
      });
  // Every tab of the space now records another profile in the session file.
  RequestSessionRebuild(chrome_profile);
}

void DeleteArciumProfile(content::BrowserContext* context, ProfileId profile) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(context);
  if (!state || context->IsOffTheRecord() || profile == DefaultProfileId() ||
      !state->model()->GetProfile(profile)) {
    return;
  }
  // The spaces move first, so their tabs are already off this storage when
  // it goes.
  std::vector<SpaceId> spaces;
  for (const Space& space : state->model()->spaces()) {
    if (space.profile_id == profile) {
      spaces.push_back(space.id);
    }
  }
  for (const SpaceId& space : spaces) {
    MoveSpaceToProfile(context, space, DefaultProfileId());
  }
  state->model()->RemoveProfile(profile);

  Profile* chrome_profile = Profile::FromBrowserContext(context);
  const std::string partition_domain = PartitionDomainForProfile(profile);
  if (!IsPartitionLoaded(context, profile)) {
    // Nothing holds it open, so content deletes the whole directory now.
    DeleteProfileCache(chrome_profile, profile);
    context->AsyncObliterateStoragePartition(
        partition_domain, base::DoNothing(), base::DoNothing());
    return;
  }
  // Cookies, site data and the cache through the remover -- obliterate
  // does not reach the cache -- and then the partition itself.
  ClearArciumProfileData(
      context, profile,
      base::BindOnce(&ObliterateWhenCleared, chrome_profile->GetWeakPtr(),
                     partition_domain));
}

}  // namespace arcium
