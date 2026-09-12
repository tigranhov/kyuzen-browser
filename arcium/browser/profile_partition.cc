// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_partition.h"

#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// Content's own layout (storage_partition_impl_map.cc): an unnamed
// partition of domain D lives in <profile>/Storage/ext/D/def.
constexpr base::FilePath::CharType kStorageDirname[] =
    FILE_PATH_LITERAL("Storage");
constexpr base::FilePath::CharType kExtensionsDirname[] =
    FILE_PATH_LITERAL("ext");
constexpr base::FilePath::CharType kDefaultPartitionDirname[] =
    FILE_PATH_LITERAL("def");

// extensions::kExtensionScheme, which //arcium/browser cannot depend on.
constexpr char kExtensionScheme[] = "chrome-extension";

}  // namespace

std::string PartitionDomainForProfile(const ProfileId& profile) {
  if (!profile.is_valid() || profile == DefaultProfileId()) {
    return std::string();
  }
  return base::StrCat({kPartitionDomainPrefix, profile.value()});
}

bool IsArciumPartitionDomain(std::string_view partition_domain) {
  return partition_domain.size() >
             std::string_view(kPartitionDomainPrefix).size() &&
         base::StartsWith(partition_domain, kPartitionDomainPrefix);
}

bool IsArciumPartitionPath(const base::FilePath& relative_partition_path) {
  const std::vector<base::FilePath::StringType> parts =
      relative_partition_path.GetComponents();
  return parts.size() == 4 && parts[0] == kStorageDirname &&
         parts[1] == kExtensionsDirname &&
         IsArciumPartitionDomain(base::FilePath(parts[2]).AsUTF8Unsafe()) &&
         parts[3] == kDefaultPartitionDirname;
}

base::FilePath PartitionDirectory(const base::FilePath& profile_path,
                                  const ProfileId& profile) {
  const std::string domain = PartitionDomainForProfile(profile);
  if (domain.empty()) {
    return base::FilePath();
  }
  return profile_path.Append(kStorageDirname)
      .Append(kExtensionsDirname)
      .AppendASCII(domain);
}

std::optional<content::StoragePartitionConfig> PartitionForProfile(
    content::BrowserContext* context,
    const ProfileId& profile) {
  const std::string domain = PartitionDomainForProfile(profile);
  if (domain.empty()) {
    return std::nullopt;
  }
  return content::StoragePartitionConfig::Create(
      context, domain, /*partition_name=*/"", /*in_memory=*/false);
}

bool IsPartitionLoaded(content::BrowserContext* context,
                       const ProfileId& profile) {
  const std::optional<content::StoragePartitionConfig> config =
      PartitionForProfile(context, profile);
  return config && context->GetStoragePartition(*config, /*can_create=*/false);
}

PageStorage StorageForUrl(const GURL& url) {
  if (url.is_empty() || url.IsAboutBlank() || url.IsAboutSrcdoc()) {
    return PageStorage::kAny;
  }
  if (url.SchemeIs(content::kChromeUIScheme) ||
      url.SchemeIs(content::kChromeUIUntrustedScheme) ||
      url.SchemeIs(content::kChromeDevToolsScheme) ||
      url.SchemeIs(kExtensionScheme)) {
    return PageStorage::kShared;
  }
  return PageStorage::kProfile;
}

scoped_refptr<content::SiteInstance> SiteInstanceForProfile(
    content::BrowserContext* context,
    const ProfileId& profile,
    const GURL& url) {
  if (context->IsOffTheRecord() || StorageForUrl(url) == PageStorage::kShared) {
    return nullptr;
  }
  const std::optional<content::StoragePartitionConfig> config =
      PartitionForProfile(context, profile);
  if (!config) {
    return nullptr;
  }
  return content::SiteInstance::CreateForFixedStoragePartition(context, url,
                                                               *config);
}

std::string PartitionDomainOfTab(content::WebContents* contents) {
  return contents->GetBrowserContext()
      ->GetStoragePartition(contents->GetSiteInstance())
      ->GetConfig()
      .partition_domain();
}

bool IsInRightStorage(const GURL& url,
                      std::string_view tab_partition_domain,
                      const ProfileId& space_profile) {
  if (!tab_partition_domain.empty() &&
      !IsArciumPartitionDomain(tab_partition_domain)) {
    return true;
  }
  switch (StorageForUrl(url)) {
    case PageStorage::kAny:
      return true;
    case PageStorage::kShared:
      return tab_partition_domain.empty();
    case PageStorage::kProfile:
      return tab_partition_domain == PartitionDomainForProfile(space_profile);
  }
  NOTREACHED();
}

scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(
    content::WebContents* old_contents) {
  content::BrowserContext* context = old_contents->GetBrowserContext();
  const content::StoragePartitionConfig config =
      context->GetStoragePartition(old_contents->GetSiteInstance())
          ->GetConfig();
  if (!IsArciumPartitionDomain(config.partition_domain())) {
    return nullptr;
  }
  return content::SiteInstance::CreateForFixedStoragePartition(
      context, old_contents->GetLastCommittedURL(), config);
}

content::PreloadingEligibility PrerenderEligibilityForTab(
    content::WebContents& contents,
    content::PreloadingEligibility chromium_answer) {
  return IsArciumPartitionDomain(PartitionDomainOfTab(&contents))
             ? content::PreloadingEligibility::kNonDefaultStoragePartition
             : chromium_answer;
}

}  // namespace arcium
