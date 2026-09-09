// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/cold_favicon_cache.h"

#include <utility>

#include "base/functional/bind.h"
#include "ui/gfx/image/image.h"

namespace arcium {

ColdFaviconCache::ColdFaviconCache(Lookup lookup,
                                   base::RepeatingClosure on_icon_ready)
    : lookup_(std::move(lookup)), on_icon_ready_(std::move(on_icon_ready)) {}

ColdFaviconCache::~ColdFaviconCache() = default;

ui::ImageModel ColdFaviconCache::IconFor(const GURL& url) const {
  const auto it = answered_.find(url);
  return it == answered_.end() ? ui::ImageModel() : it->second;
}

void ColdFaviconCache::Request(const GURL& url) {
  if (answered_.contains(url) || in_flight_.contains(url)) {
    return;
  }
  in_flight_.insert(url);
  // Through a WeakPtr because a window can close with lookups outstanding:
  // the reply then has nowhere to land and is dropped rather than run into a
  // destroyed cache.
  lookup_.Run(url, base::BindOnce(&ColdFaviconCache::OnLookupResult,
                                  weak_factory_.GetWeakPtr(), url));
}

void ColdFaviconCache::OnLookupResult(const GURL& url,
                                      const gfx::Image& image) {
  in_flight_.erase(url);
  // A miss is recorded, not discarded -- that record is what stops the URL
  // being asked about again on the next burst.
  if (image.IsEmpty()) {
    answered_[url] = ui::ImageModel();
    return;
  }
  answered_[url] = ui::ImageModel::FromImage(image);
  // Only a real icon is announced. Announcing a miss would rebuild the
  // sidebar to draw the same globe it already drew.
  on_icon_ready_.Run();
}

}  // namespace arcium
