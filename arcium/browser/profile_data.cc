// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_data.h"

#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "base/functional/callback.h"
#include "base/scoped_observation.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"

namespace arcium {

namespace {

// Everything Chrome's own dialog removes for "cookies and other site data"
// and "cached images and files", limited to what lives in a storage
// partition: a filter that names a partition may not carry anything else
// (browsing_data_remover_impl.cc DCHECKs it), and the rest -- history, form
// data, site settings -- is shared between profiles anyway.
constexpr uint64_t kProfileDataMask =
    content::BrowsingDataRemover::DATA_TYPE_ON_STORAGE_PARTITION &
    (content::BrowsingDataRemover::DATA_TYPE_COOKIES |
     content::BrowsingDataRemover::DATA_TYPE_DOM_STORAGE |
     content::BrowsingDataRemover::DATA_TYPE_PRIVACY_SANDBOX |
     content::BrowsingDataRemover::DATA_TYPE_DEVICE_BOUND_SESSIONS |
     content::BrowsingDataRemover::DATA_TYPE_CACHE);

// The remover reports a removal to one observer, which has to be watching
// before the removal starts. Chrome's own helper does the same thing in an
// anonymous namespace of its own (browsing_data_important_sites_util.cc).
class OneRemovalObserver : public content::BrowsingDataRemover::Observer {
 public:
  OneRemovalObserver(content::BrowsingDataRemover* remover,
                     base::OnceClosure done)
      : done_(std::move(done)) {
    observation_.Observe(remover);
  }

  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    observation_.Reset();
    std::move(done_).Run();
    delete this;
  }

 private:
  ~OneRemovalObserver() override = default;

  base::OnceClosure done_;
  base::ScopedObservation<content::BrowsingDataRemover,
                          content::BrowsingDataRemover::Observer>
      observation_{this};
};

}  // namespace

void ClearArciumProfileData(content::BrowserContext* context,
                            const ProfileId& profile,
                            base::OnceClosure done) {
  std::unique_ptr<content::BrowsingDataFilterBuilder> filter =
      content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kPreserve);
  // No partition means the default profile, which is Chromium's own
  // partition: an empty preserve filter clears exactly that.
  if (const std::optional<content::StoragePartitionConfig> partition =
          PartitionForProfile(context, profile)) {
    filter->SetStoragePartitionConfig(*partition);
  }
  content::BrowsingDataRemover* remover = context->GetBrowsingDataRemover();
  remover->RemoveWithFilterAndReply(
      base::Time(), base::Time::Max(), kProfileDataMask,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      std::move(filter), new OneRemovalObserver(remover, std::move(done)));
}

std::optional<std::unordered_set<base::FilePath>> PartitionPathsToKeep(
    content::BrowserContext* context) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(context);
  if (!state || !state->model_load_succeeded()) {
    return std::nullopt;
  }
  std::unordered_set<base::FilePath> paths;
  for (const ArciumProfile& profile : state->model()->profiles()) {
    const base::FilePath path =
        PartitionDirectory(context->GetPath(), profile.id);
    if (!path.empty()) {
      paths.insert(path);
    }
  }
  return paths;
}

}  // namespace arcium
