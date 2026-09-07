// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"

#include <memory>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/browser_context.h"

namespace arcium {

namespace {

// The address of this object is the key; its value is never read.
const char kArciumProfileStateKey[] = "arcium_profile_state";

// Runs on the archive's own sequence. A file that will not open leaves the
// store closed and every later Add a no-op; an unopenable archive must not
// stop the browser, and there is nothing in it worth a recovery path.
void OpenArchive(ArchiveStore* archive, base::FilePath path) {
  std::ignore = archive->Open(path);
}

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
    // BEST_EFFORT: nothing waits on an archive write, and the open below is
    // posted rather than done here, so it is off the startup path. It is not
    // lazy — every regular profile opens the file whether or not anything is
    // ever archived, and carries the connection for the life of the process.
    // BLOCK_SHUTDOWN so a tab archived during teardown is not lost between the
    // close and the write.
    archive_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
    archive_ = std::make_unique<ArchiveStore>();
    // Constructed here, used only there. Without this the sql::Database would
    // stay bound to the UI thread's sequence and every posted call would trip
    // its sequence checker.
    archive_->DetachFromSequence();
    archive_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&OpenArchive, base::Unretained(archive_.get()),
                       ArchivePath(profile_path)));
  }
  model_.AddObserver(this);
}

ArciumProfileState::~ArciumProfileState() {
  model_.RemoveObserver(this);
  if (archive_) {
    // Behind every write already posted, and on the sequence that owns it.
    archive_runner_->DeleteSoon(FROM_HERE, std::move(archive_));
  }
}

void ArciumProfileState::OnArciumModelChanged() {
  // ArciumModel::ReplaceAll - which ModelStore::Load calls once the first
  // window is already interactive - drops entries without telling the
  // binding, and Task 9's archiving will do the same. Release the orphans
  // here, once per profile, so the binding cannot accumulate dead ids.
  //
  // Observer order relative to a window's SidebarTabModel is not guaranteed,
  // which is why SidebarTabModel also checks the model itself rather than
  // trusting the binding alone.
  for (const EntryId& id : binding_.BoundEntries()) {
    if (!model_.GetEntry(id)) {
      binding_.UnbindEntry(id);
    }
  }
}

}  // namespace arcium
