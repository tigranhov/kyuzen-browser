// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/cold_favicon_cache.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

namespace arcium {

namespace {

gfx::Image AnIcon() {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 16);
  bitmap.eraseColor(SK_ColorRED);
  return gfx::Image::CreateFrom1xBitmap(bitmap);
}

// Stands in for the profile's favicon database. It records what it was asked
// and hands back the reply callback, so a test answers a lookup at the moment
// it chooses rather than at the moment the cache asked -- which is what makes
// the in-flight rule observable at all.
class FakeLookup {
 public:
  ColdFaviconCache::Lookup AsCallback() {
    return base::BindRepeating(&FakeLookup::Ask, base::Unretained(this));
  }

  size_t ask_count() const { return asked_.size(); }
  const std::vector<GURL>& asked() const { return asked_; }

  void Answer(size_t index, const gfx::Image& image) {
    std::move(replies_[index]).Run(image);
  }

 private:
  void Ask(const GURL& url, base::OnceCallback<void(const gfx::Image&)> reply) {
    asked_.push_back(url);
    replies_.push_back(std::move(reply));
  }

  std::vector<GURL> asked_;
  std::vector<base::OnceCallback<void(const gfx::Image&)>> replies_;
};

class ColdFaviconCacheTest : public testing::Test {
 protected:
  size_t ready_count() const { return ready_count_; }

  std::unique_ptr<ColdFaviconCache> MakeCache() {
    return std::make_unique<ColdFaviconCache>(
        lookup_.AsCallback(),
        base::BindRepeating(&ColdFaviconCacheTest::OnReady,
                            base::Unretained(this)));
  }

  FakeLookup lookup_;

 private:
  void OnReady() { ++ready_count_; }

  size_t ready_count_ = 0;
};

// Functions rather than namespace-scope objects: a GURL at file scope needs
// an exit-time destructor, which this tree builds with -Wexit-time-destructors.
GURL News() {
  return GURL("https://news.ycombinator.com/");
}
GURL Reddit() {
  return GURL("https://www.reddit.com/");
}

TEST_F(ColdFaviconCacheTest, AnAnsweredLookupIsDrawnAndAnnounced) {
  std::unique_ptr<ColdFaviconCache> cache = MakeCache();
  cache->Request(News());
  ASSERT_EQ(1u, lookup_.ask_count());
  EXPECT_EQ(News(), lookup_.asked()[0]);
  // Nothing to draw until the answer arrives: the row falls back to a globe.
  EXPECT_TRUE(cache->IconFor(News()).IsEmpty());
  EXPECT_EQ(0u, ready_count());

  lookup_.Answer(0, AnIcon());

  EXPECT_FALSE(cache->IconFor(News()).IsEmpty());
  EXPECT_EQ(1u, ready_count());
}

TEST_F(ColdFaviconCacheTest, ReadingAnIconNeverStartsALookup) {
  // The row build that reads this is const and runs on every repaint. If
  // reading asked, a URL with no stored icon would ask once per frame.
  std::unique_ptr<ColdFaviconCache> cache = MakeCache();
  EXPECT_TRUE(cache->IconFor(News()).IsEmpty());
  EXPECT_EQ(0u, lookup_.ask_count());
}

TEST_F(ColdFaviconCacheTest, AUrlIsNotAskedAboutTwiceWhileItsLookupIsInFlight) {
  // Requests arrive once per notification burst, and a burst can precede the
  // answer to the burst before it.
  std::unique_ptr<ColdFaviconCache> cache = MakeCache();
  cache->Request(News());
  cache->Request(News());
  EXPECT_EQ(1u, lookup_.ask_count());
}

TEST_F(ColdFaviconCacheTest, AUrlWithNoStoredIconIsNotAskedAboutAgain) {
  // The rule that keeps a miss costing one lookup for the life of the window
  // rather than one per burst.
  std::unique_ptr<ColdFaviconCache> cache = MakeCache();
  cache->Request(News());
  lookup_.Answer(0, gfx::Image());
  EXPECT_TRUE(cache->IconFor(News()).IsEmpty());

  cache->Request(News());

  EXPECT_EQ(1u, lookup_.ask_count());
}

TEST_F(ColdFaviconCacheTest, AMissIsNotAnnounced) {
  // Announcing would rebuild the sidebar to draw the same globe it already
  // drew.
  std::unique_ptr<ColdFaviconCache> cache = MakeCache();
  cache->Request(News());
  lookup_.Answer(0, gfx::Image());
  EXPECT_EQ(0u, ready_count());
}

TEST_F(ColdFaviconCacheTest, AnAnsweredIconIsNotAskedAboutAgain) {
  std::unique_ptr<ColdFaviconCache> cache = MakeCache();
  cache->Request(News());
  lookup_.Answer(0, AnIcon());

  cache->Request(News());

  EXPECT_EQ(1u, lookup_.ask_count());
}

TEST_F(ColdFaviconCacheTest, EachUrlIsAskedAboutSeparately) {
  std::unique_ptr<ColdFaviconCache> cache = MakeCache();
  cache->Request(News());
  cache->Request(Reddit());
  ASSERT_EQ(2u, lookup_.ask_count());
  lookup_.Answer(1, AnIcon());

  // The answer lands on the URL it was asked for, not on the first pending
  // one.
  EXPECT_TRUE(cache->IconFor(News()).IsEmpty());
  EXPECT_FALSE(cache->IconFor(Reddit()).IsEmpty());
}

TEST_F(ColdFaviconCacheTest, AnAnswerAfterTheCacheIsGoneIsDropped) {
  // A window can close with lookups outstanding. The reply must not run into
  // a destroyed cache.
  {
    std::unique_ptr<ColdFaviconCache> cache = MakeCache();
    cache->Request(News());
  }
  lookup_.Answer(0, AnIcon());
  EXPECT_EQ(0u, ready_count());
}

}  // namespace

}  // namespace arcium
