// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_PROFILE_PARTITION_H_
#define ARCIUM_BROWSER_PROFILE_PARTITION_H_

#include <optional>
#include <string>
#include <string_view>

#include "arcium/browser/model/entry_id.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/browser/preloading.h"
#include "content/public/browser/storage_partition_config.h"

class GURL;

namespace content {
class BrowserContext;
class SiteInstance;
class WebContents;
}  // namespace content

namespace arcium {

// The one place an Arcium profile becomes storage. The default profile is
// Chromium's default partition and keeps Chromium's own code paths; every
// other profile is the partition "arcium-<id>" of the one Chromium profile,
// one partition domain per profile so erasing a profile erases one domain.
//
// Content documents a partition domain as "[a-z]*" but checks only that it
// is not empty (storage_partition_config.cc). A profile id is a lowercase
// UUID, so the domain carries digits and hyphens; the disk path and download
// resumption take it as it is, and stripping them would let ids collide.

inline constexpr char kPartitionDomainPrefix[] = "arcium-";

// "" for the default profile, "arcium-<id>" for any other.
std::string PartitionDomainForProfile(const ProfileId& profile);

// Whether `partition_domain` is an Arcium profile's. The prefix cannot
// collide with an extension's partition, whose domain is its 32-letter id.
bool IsArciumPartitionDomain(std::string_view partition_domain);

// Whether `relative_partition_path`, a partition's path relative to the
// Chromium profile's directory, is an Arcium profile's:
// Storage/ext/arcium-<id>/def. The only fact about a partition that reaches
// the network context's configuration.
bool IsArciumPartitionPath(const base::FilePath& relative_partition_path);

// <profile_path>/Storage/ext/arcium-<id>, the directory holding `profile`'s
// partition; empty for the default profile. Built by hand: content keeps its
// helper internal, and asking a partition for its path would create it.
base::FilePath PartitionDirectory(const base::FilePath& profile_path,
                                  const ProfileId& profile);

// The partition `profile`'s tabs live in, or nullopt for the default
// profile, which uses Chromium's default partition.
std::optional<content::StoragePartitionConfig> PartitionForProfile(
    content::BrowserContext* context,
    const ProfileId& profile);

// Whether `profile`'s partition has been created this session. Never
// creates it. False for the default profile, which is not Arcium's to erase.
bool IsPartitionLoaded(content::BrowserContext* context,
                       const ProfileId& profile);

// Which storage a page belongs in.
enum class PageStorage {
  // A page with nothing of its own to keep: no URL, about:blank,
  // about:srcdoc.
  kAny,
  // Browser and extension pages: always the default partition, so an
  // extension's pages read the same storage as its background worker
  // whatever space they open in.
  kShared,
  // Every other page: the storage of the tab's profile.
  kProfile,
};
PageStorage StorageForUrl(const GURL& url);

// A SiteInstance fixed to `profile`'s partition for a new tab about to show
// `url`, or nullptr when Chromium's own choice is right: the default
// profile, a browser or extension page, or an off-the-record context, where
// profiles do not apply.
scoped_refptr<content::SiteInstance> SiteInstanceForProfile(
    content::BrowserContext* context,
    const ProfileId& profile,
    const GURL& url);

// The partition domain of the storage `contents` uses: "" for the default
// partition. Creates the partition if the tab's SiteInstance has not yet.
std::string PartitionDomainOfTab(content::WebContents* contents);

// Whether a main-frame navigation to `url`, in a tab using
// `tab_partition_domain` whose space is on `space_profile`, lands in the
// storage it belongs in. A tab on a partition that is neither the default
// nor Arcium's -- a guest, an app -- is not Arcium's to judge, and passes.
bool IsInRightStorage(const GURL& url,
                      std::string_view tab_partition_domain,
                      const ProfileId& space_profile);

// The storage a tab Chromium is recreating must keep: a SiteInstance fixed
// to the partition `old_contents` uses, or nullptr when that is the default
// partition and Chromium's own choice -- none at all -- is right.
scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(
    content::WebContents* old_contents);

// Whether a page may be prerendered for `contents`. A prerender builds its
// own frame tree in the default partition and activation swaps the tab into
// it, so a tab in a profile refuses: R3.9 -- nothing loads unless it is
// asked for -- argues the same way.
content::PreloadingEligibility PrerenderEligibilityForTab(
    content::WebContents& contents,
    content::PreloadingEligibility chromium_answer);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_PROFILE_PARTITION_H_
