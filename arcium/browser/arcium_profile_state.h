// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_ARCIUM_PROFILE_STATE_H_
#define ARCIUM_BROWSER_ARCIUM_PROFILE_STATE_H_

#include <memory>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model_store.h"
#include "arcium/browser/tab_binding.h"
#include "base/files/file_path.h"
#include "base/supports_user_data.h"

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
class ArciumProfileState : public base::SupportsUserData::Data {
 public:
  // Creates the state on first call for `context` and starts the load, then
  // returns the same object for the life of the profile.
  static ArciumProfileState* GetForBrowserContext(
      content::BrowserContext* context);

  // Extension-less data files beside Chromium's own `Bookmarks`.
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

 private:
  ArciumProfileState(const base::FilePath& profile_path, bool off_the_record);

  ArciumModel model_;
  TabBinding binding_;
  // Null while off the record. Declared after the two objects it reads, so
  // it is destroyed first.
  std::unique_ptr<ModelStore> store_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_ARCIUM_PROFILE_STATE_H_
