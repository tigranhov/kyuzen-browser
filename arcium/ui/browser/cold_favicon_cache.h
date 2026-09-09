// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_COLD_FAVICON_CACHE_H_
#define ARCIUM_UI_BROWSER_COLD_FAVICON_CACHE_H_

#include <map>
#include <set>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/models/image_model.h"
#include "url/gurl.h"

namespace gfx {
class Image;
}

namespace arcium {

// The icons for entries that have no tab behind them.
//
// A cold entry used to draw a globe for the life of the window, because
// nothing asked for its real icon -- yet the icon is already on disk.
// Chromium stores favicons in the profile keyed by page URL, independently of
// whether any tab is open, so a relaunched window can draw the icons it had
// before instead of a column of globes.
//
// Never touches the network: this reads what the profile already has and
// answers with nothing when it has nothing. A URL with no stored icon is
// asked about once and then remembered as a miss, so a miss costs one lookup
// for the life of the window rather than one per notification burst.
class ColdFaviconCache {
 public:
  // Answers with the icon stored for a URL, or an empty image when there is
  // none. Injected rather than reaching for FaviconService directly so the
  // rules below are testable without a profile; production binds it in
  // SidebarTabModel.
  using Lookup = base::RepeatingCallback<
      void(const GURL&, base::OnceCallback<void(const gfx::Image&)>)>;

  // `on_icon_ready` runs when an icon arrives that was not there before.
  // Once per icon: the sidebar's own notification is coalesced per run-loop
  // turn, so a burst of arrivals still costs one rebuild.
  ColdFaviconCache(Lookup lookup, base::RepeatingClosure on_icon_ready);
  ColdFaviconCache(const ColdFaviconCache&) = delete;
  ColdFaviconCache& operator=(const ColdFaviconCache&) = delete;
  ~ColdFaviconCache();

  // The icon known for `url`, or an empty model when none is known yet or
  // none is stored. Deliberately does not start a lookup: this is read from
  // the const row build, which runs on every rebuild, and a reading lookup
  // would re-ask for every miss every time.
  ui::ImageModel IconFor(const GURL& url) const;

  // Asks about `url` unless it has already been answered or a question about
  // it is outstanding.
  void Request(const GURL& url);

 private:
  void OnLookupResult(const GURL& url, const gfx::Image& image);

  Lookup lookup_;
  base::RepeatingClosure on_icon_ready_;
  // Every URL that has been answered. An empty model is a remembered miss,
  // which is why this is not simply the icons that exist.
  std::map<GURL, ui::ImageModel> answered_;
  std::set<GURL> in_flight_;
  base::WeakPtrFactory<ColdFaviconCache> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_COLD_FAVICON_CACHE_H_
