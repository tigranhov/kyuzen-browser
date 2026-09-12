// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_ARCIUM_PROFILE_STATE_H_
#define ARCIUM_BROWSER_ARCIUM_PROFILE_STATE_H_

#include <memory>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model_store.h"
#include "arcium/browser/tab_binding.h"
#include "base/files/file_path.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"

namespace content {
class BrowserContext;
}

namespace arcium {

// The per-profile half of the sidebar: the persistent model, the entry-to-tab
// binding and the store that writes the model to disk. Every window on a
// profile shares one of these, which is what makes an entry pinned in one
// window pinned in the other.
//
// Attached to the BrowserContext as user data rather than exposed through a
// ProfileKeyedServiceFactory: a factory has to be registered in Chromium's
// own EnsureBrowserContextKeyedServiceFactoriesBuilt(), and Arcium's rule is
// that upstream files change only through a patch. User data needs no
// upstream registration at all, and the lifetime is the same either way.
//
// An off-the-record context gets its own state, with a model and a binding
// but no store: incognito pins are session-scoped, which is what incognito
// means. See store().
class ArciumProfileState : public base::SupportsUserData::Data,
                           public ArciumModel::Observer {
 public:
  // Creates the state on first call for `context` and starts the load, then
  // returns the same object for the life of the profile.
  static ArciumProfileState* GetForBrowserContext(
      content::BrowserContext* context);

  // The same lookup without the construction, for callers that are only ever
  // passing through: the session-service path runs for every tab of every
  // window on a command rebuild and on every tab close, and it has nothing to
  // say about a profile the sidebar has not opened yet. Constructing here
  // would post an archive open and a model load as a side effect of Chromium
  // walking a tab strip. NULL when nothing has created the state.
  static ArciumProfileState* GetForBrowserContextIfExists(
      content::BrowserContext* context);

  // Extension-less data files beside Chromium's own `Bookmarks`.
  //
  // Never build either path from an off-the-record context's GetPath(): it
  // returns the PARENT profile's directory, so an incognito writer lands on
  // the regular profile's file. `store_` is null off the record for exactly
  // that reason, and whatever wires ArchiveStore up must do the same.
  static base::FilePath ModelPath(const base::FilePath& profile_path);
  static base::FilePath ArchivePath(const base::FilePath& profile_path);

  ArciumProfileState(const ArciumProfileState&) = delete;
  ArciumProfileState& operator=(const ArciumProfileState&) = delete;
  ~ArciumProfileState() override;

  ArciumModel* model() { return &model_; }
  TabBinding* binding() { return &binding_; }

  // NULL for an off-the-record context, which has no persistence at all.
  // OffTheRecordProfileImpl::GetPath() returns the *parent* profile's path,
  // so an incognito store would write incognito browsing state into the
  // regular profile's `Arcium Model` file and race the regular profile's own
  // writer for it. Every caller must null-check.
  ModelStore* store() { return store_.get(); }

  // Whether the model is the user's yet rather than the one-space
  // placeholder. Off the record there is no file, so the model is complete
  // from the start.
  bool model_load_finished() const {
    return !store_ || store_->load_finished();
  }
  // Whether the model was read from disk and understood, or there was no
  // file. Always false off the record, where nothing on disk belongs to
  // this model.
  bool model_load_succeeded() const {
    return store_ && store_->load_succeeded();
  }

  // The profile's archive — one SQLite file, shared by every window on the
  // profile, which is why it lives here rather than beside a window's
  // ArchiveService.
  //
  // NULL off the record, for the same reason store() is: the path would be
  // the regular profile's, and an archived incognito tab is a row that
  // outlives the window that wrote it. Never null-check this and then fall
  // back to the regular profile's archive; there is nothing to fall back to.
  //
  // Only ever touched on archive_runner(): sql::Database blocks and is
  // sequence-affine, and this object is reached from the UI thread.
  ArchiveStore* archive() { return archive_.get(); }
  scoped_refptr<base::SequencedTaskRunner> archive_runner() {
    return archive_runner_;
  }

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

 private:
  ArciumProfileState(const base::FilePath& profile_path, bool off_the_record);

  ArciumModel model_;
  TabBinding binding_;
  // Null while off the record. Declared after the two objects it observes and
  // reads, so it is destroyed first.
  std::unique_ptr<ModelStore> store_;
  // Null while off the record. Lives on `archive_runner_` from the moment it
  // is created and is deleted there too, behind every write already posted.
  scoped_refptr<base::SequencedTaskRunner> archive_runner_;
  std::unique_ptr<ArchiveStore> archive_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_ARCIUM_PROFILE_STATE_H_
