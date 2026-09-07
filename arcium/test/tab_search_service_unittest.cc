// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/tab_search_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "base/files/scoped_temp_dir.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class TabSearchServiceTest : public BrowserWithTestWindowTest {
 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(archive_.Open(temp_dir_.GetPath().AppendASCII("Archive")));
    // Production order: ArchiveService is built by BrowserSidebarController
    // while the strip is still empty, and the store's sequence in this fixture
    // is the test's own, so a posted read is drained by RunUntilIdle.
    ASSERT_EQ(0, strip()->count());
    sidebar_model_ =
        std::make_unique<SidebarTabModel>(strip(), &model_, &binding_);
    archive_service_ = std::make_unique<ArchiveService>(
        strip(), &model_, &binding_, &archive_,
        base::SequencedTaskRunner::GetCurrentDefault());
    service_ = std::make_unique<TabSearchService>(strip(), &model_, &binding_,
                                                  archive_service_.get());
  }

  void TearDown() override {
    service_.reset();
    // Both observe the strip the base class is about to tear down.
    archive_service_.reset();
    sidebar_model_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // Appended rather than BrowserWithTestWindowTest::AddTab, and built as a
  // TestWebContents: AddTab goes through chrome's navigation stack and yields
  // a real WebContentsImpl, which WebContentsTester::For then static_casts to
  // TestWebContents and hands back a bogus vtable. Only contents made by
  // CreateTestWebContents may be given to For(), which is the only way to
  // control a title — and every ranking assertion here depends on titles.
  void AddTabWithTitle(const GURL& url, const std::u16string& title) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    content::WebContentsTester* tester =
        content::WebContentsTester::For(contents.get());
    tester->NavigateAndCommit(url);
    tester->SetTitle(title);
    strip()->AppendWebContents(std::move(contents), /*foreground=*/false);
  }

  ArchivedTab MakeArchivedAt(const std::string& url,
                             const std::u16string& title,
                             base::Time archived_at) {
    ArchivedTab tab;
    tab.url = GURL(url);
    tab.title = title;
    tab.space_id = model_.default_space_id();
    tab.archived_at = archived_at;
    return tab;
  }

  ArchivedTab MakeArchived(const std::string& url,
                           const std::u16string& title) {
    return MakeArchivedAt(url, title, base::Time::Now());
  }

  // The only entry point a UI caller has, so it is the one the tests drive.
  // RunUntilIdle drains both the posted store read and its reply.
  std::vector<SearchResult> Search(const std::u16string& query, int limit) {
    std::vector<SearchResult> out;
    bool ran = false;
    service_->Search(
        query, limit,
        base::BindLambdaForTesting([&](std::vector<SearchResult> results) {
          out = std::move(results);
          ran = true;
        }));
    task_environment()->RunUntilIdle();
    EXPECT_TRUE(ran) << "the search callback never ran";
    return out;
  }

  base::ScopedTempDir temp_dir_;
  ArciumModel model_;
  TabBinding binding_;
  ArchiveStore archive_;
  std::unique_ptr<SidebarTabModel> sidebar_model_;
  std::unique_ptr<ArchiveService> archive_service_;
  std::unique_ptr<TabSearchService> service_;
};

TEST_F(TabSearchServiceTest, MatchesTitleAndUrlAcrossAllThreeSources) {
  AddTabWithTitle(GURL("https://live.example/"), u"Live page");
  model_.AddEntry(EntryKind::kPinned, GURL("https://entry.example/"), u"Entry");
  archive_.Add(MakeArchived("https://archived.example/", u"Archived"));

  EXPECT_EQ(1u, Search(u"live", 10).size());
  EXPECT_EQ(1u, Search(u"entry", 10).size());
  EXPECT_EQ(1u, Search(u"archived", 10).size());
  EXPECT_EQ(3u, Search(u"example", 10).size());
}

TEST_F(TabSearchServiceTest, LiveTabsOutrankEntriesWhichOutrankTheArchive) {
  AddTabWithTitle(GURL("https://match.example/live"), u"match");
  model_.AddEntry(EntryKind::kPinned, GURL("https://match.example/entry"),
                  u"match");
  archive_.Add(MakeArchived("https://match.example/archive", u"match"));

  std::vector<SearchResult> results = Search(u"match", 10);
  ASSERT_EQ(3u, results.size());
  EXPECT_EQ(SearchResult::Source::kLiveTab, results[0].source);
  EXPECT_EQ(SearchResult::Source::kEntry, results[1].source);
  EXPECT_EQ(SearchResult::Source::kArchive, results[2].source);
}

TEST_F(TabSearchServiceTest, ATitlePrefixOutranksAMidWordMatch) {
  AddTabWithTitle(GURL("https://a.example/"), u"The Chromium project");
  AddTabWithTitle(GURL("https://b.example/"), u"Chromium docs");

  std::vector<SearchResult> results = Search(u"chromium", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(u"Chromium docs", results[0].title);
  EXPECT_EQ(u"The Chromium project", results[1].title);
}

TEST_F(TabSearchServiceTest, ATitleMatchOutranksAUrlOnlyMatch) {
  AddTabWithTitle(GURL("https://needle.example/"), u"Nothing to see");
  AddTabWithTitle(GURL("https://b.example/"), u"A needle in there");

  std::vector<SearchResult> results = Search(u"needle", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(u"A needle in there", results[0].title);
  EXPECT_EQ(u"Nothing to see", results[1].title);
}

TEST_F(TabSearchServiceTest, AWarmEntryIsReportedOnceNotTwice) {
  AddTabWithTitle(GURL("https://both.example/"), u"Both");
  sidebar_model_->PinTab(0);

  std::vector<SearchResult> results = Search(u"both", 10);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(SearchResult::Source::kEntry, results[0].source);
  EXPECT_TRUE(results[0].entry_id.is_valid());
}

// The predicate is "an entry that still exists claims this tab", not "this tab
// is bound". ArciumModel::ReplaceAll drops entries without touching the
// binding, so a tab can outlive the entry that claimed it. Filtering such a
// tab out of the live pass would make it unfindable: it is absent from the
// entry pass too, because its entry is gone.
TEST_F(TabSearchServiceTest, ATabBoundToARemovedEntryIsStillFound) {
  AddTabWithTitle(GURL("https://stale.example/"), u"Stale");
  sidebar_model_->PinTab(0);
  ASSERT_EQ(1u, Search(u"stale", 10).size());

  // What ReplaceAll does: the entry goes, the binding stays.
  model_.ReplaceAll(model_.spaces(), model_.folders(), {});
  ASSERT_TRUE(binding_.IsBound(strip()->GetTabAtIndex(0)->GetHandle()));

  std::vector<SearchResult> results = Search(u"stale", 10);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(SearchResult::Source::kLiveTab, results[0].source);
}

TEST_F(TabSearchServiceTest, MatchingIsCaseAndDiacriticInsensitive) {
  AddTabWithTitle(GURL("https://a.example/"), u"Café");
  EXPECT_EQ(1u, Search(u"CAFE", 10).size());
  EXPECT_EQ(1u, Search(u"café", 10).size());
  EXPECT_EQ(1u, service_->SearchLocal(u"CAFE", 10).size());
}

// A characterisation test of a dependency, not a regression test: it pins why
// the in-memory pass does NOT use base::i18n::FoldCase, which is what the
// archive's shadow columns use and what this task was originally specified to
// use. Folding is case-only, so it would fail the test above. If a future
// base makes FoldCase accent-insensitive this test goes red and the comment
// in tab_search_service.cc can be deleted along with it.
TEST(TabSearchFoldingTest, FoldCaseAloneWouldNotMatchAnAccent) {
  EXPECT_EQ(u"cafe", base::i18n::FoldCase(u"CAFE"));
  EXPECT_EQ(u"café", base::i18n::FoldCase(u"Café"));
  EXPECT_NE(base::i18n::FoldCase(u"CAFE"), base::i18n::FoldCase(u"Café"));
}

TEST_F(TabSearchServiceTest, AnEmptyQueryReturnsNothing) {
  AddTabWithTitle(GURL("https://a.example/"), u"A");
  model_.AddEntry(EntryKind::kPinned, GURL("https://b.example/"), u"B");
  archive_.Add(MakeArchived("https://c.example/", u"C"));

  EXPECT_TRUE(Search(u"", 10).empty());
  EXPECT_TRUE(service_->SearchLocal(u"", 10).empty());
}

TEST_F(TabSearchServiceTest, TheLimitIsHonouredAcrossSources) {
  for (int i = 0; i < 5; ++i) {
    AddTabWithTitle(GURL("https://x.example/" + base::NumberToString(i)), u"x");
  }
  archive_.Add(MakeArchived("https://x.example/archived", u"x"));

  EXPECT_EQ(3u, Search(u"x", 3).size());
  EXPECT_EQ(6u, Search(u"x", 10).size());
}

// The synchronous entry point covers what is already in memory and nothing
// else. If it ever reached the archive it would be sync I/O on the UI thread.
TEST_F(TabSearchServiceTest, SearchLocalCoversLiveTabsAndEntriesOnly) {
  AddTabWithTitle(GURL("https://live.example/"), u"only");
  model_.AddEntry(EntryKind::kPinned, GURL("https://entry.example/"), u"only");
  archive_.Add(MakeArchived("https://archived.example/", u"only"));

  std::vector<SearchResult> results = service_->SearchLocal(u"only", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(SearchResult::Source::kLiveTab, results[0].source);
  EXPECT_EQ(SearchResult::Source::kEntry, results[1].source);
  // And the async path over the same query does see the archived row, so the
  // two really do differ by the archive rather than by the query.
  EXPECT_EQ(3u, Search(u"only", 10).size());
}

// Off the record there is no archive at all, and a profile whose archive file
// will not open behaves the same way. Search still has to answer.
TEST_F(TabSearchServiceTest, SearchWorksWithNoArchiveService) {
  AddTabWithTitle(GURL("https://live.example/"), u"solo");
  TabSearchService no_archive(strip(), &model_, &binding_, nullptr);

  std::vector<SearchResult> results;
  bool ran = false;
  no_archive.Search(
      u"solo", 10,
      base::BindLambdaForTesting([&](std::vector<SearchResult> found) {
        results = std::move(found);
        ran = true;
      }));
  // Answered on a later turn, never from inside the call: a caller that can be
  // answered re-entrantly has a second order of events to be correct in.
  EXPECT_FALSE(ran);
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(ran);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(SearchResult::Source::kLiveTab, results[0].source);
}

// A row the user can already reach as a live tab or a pinned entry is noise in
// the archive half: reopening it would only duplicate what is in front of them.
TEST_F(TabSearchServiceTest, AnArchivedRowForALiveUrlIsSuppressed) {
  AddTabWithTitle(GURL("https://dupe.example/"), u"Dupe");
  archive_.Add(MakeArchived("https://dupe.example/", u"Dupe"));
  archive_.Add(MakeArchived("https://other.example/", u"Dupe elsewhere"));

  std::vector<SearchResult> results = Search(u"dupe", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(SearchResult::Source::kLiveTab, results[0].source);
  EXPECT_EQ(GURL("https://other.example/"), results[1].url);
}

// The set the archive half is filtered against is built from every live tab
// and every entry, not only the ones that match this query: a tab that misses
// the query is still a tab the user has, and offering to un-archive its URL
// would offer them something already in front of them. Building the set from
// the matches instead would let both suppressed rows below through.
TEST_F(TabSearchServiceTest, ANonMatchingTabOrEntrySuppressesItsArchivedRow) {
  AddTabWithTitle(GURL("https://tab.example/"), u"Nothing alike");
  model_.AddEntry(EntryKind::kPinned, GURL("https://entry.example/"),
                  u"Nor this");

  archive_.Add(MakeArchived("https://tab.example/", u"Zebra as a tab"));
  archive_.Add(MakeArchived("https://entry.example/", u"Zebra as an entry"));
  archive_.Add(MakeArchived("https://gone.example/", u"Zebra archived only"));

  std::vector<SearchResult> results = Search(u"zebra", 10);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(GURL("https://gone.example/"), results[0].url);
}

// ArchiveStore::Search applies its LIMIT by recency, in SQL, and the URL
// suppression runs afterwards in C++. Asking the store for exactly `limit`
// rows therefore lets suppression empty the archive half completely: here the
// two newest matching rows are both URLs the user already has open, so they
// are dropped and a perfectly good older match is never looked at. Neither tab
// matches the query itself — they are here only as the thing that suppresses.
TEST_F(TabSearchServiceTest, SuppressedNewArchiveRowsDoNotHideOlderMatches) {
  const base::Time now = base::Time::Now();
  AddTabWithTitle(GURL("https://open1.example/"), u"Something open");
  AddTabWithTitle(GURL("https://open2.example/"), u"Also open");

  archive_.Add(MakeArchivedAt("https://open1.example/", u"Zebra newest", now));
  archive_.Add(MakeArchivedAt("https://open2.example/", u"Zebra newer",
                              now - base::Minutes(1)));
  archive_.Add(MakeArchivedAt("https://gone.example/", u"Zebra oldest",
                              now - base::Hours(1)));

  std::vector<SearchResult> results = Search(u"zebra", 2);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(SearchResult::Source::kArchive, results[0].source);
  EXPECT_EQ(GURL("https://gone.example/"), results[0].url);
}

// StoreFetchLimit's bound counts distinct URLs the user can reach, so it is
// exact only if the store never hands back two rows for one URL. It does not
// promise that by itself: the archive's key is (url, archived_at), so
// archiving one tab twice leaves two rows at one URL, and a plain
// ORDER BY archived_at DESC lets both occupy the window.
//
// One non-matching open tab makes the fetch limit 2, and both of that tab's
// URL's archive rows are newer than the only match the user cannot already
// reach. Before ArchiveStore::Search grouped by URL, both filled the window,
// both were suppressed, and the archive half came back empty with a genuine
// match one row further down.
TEST_F(TabSearchServiceTest, RepeatedArchiveRowsForOneUrlDoNotEmptyTheArchive) {
  const base::Time now = base::Time::Now();
  AddTabWithTitle(GURL("https://x.example/"), u"Nothing alike");

  archive_.Add(MakeArchivedAt("https://x.example/", u"Zebra newest", now));
  archive_.Add(MakeArchivedAt("https://x.example/", u"Zebra newer",
                              now - base::Minutes(1)));
  archive_.Add(MakeArchivedAt("https://gone.example/", u"Zebra oldest",
                              now - base::Hours(1)));

  std::vector<SearchResult> results = Search(u"zebra", 1);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(SearchResult::Source::kArchive, results[0].source);
  EXPECT_EQ(GURL("https://gone.example/"), results[0].url);
}

// The suppression above must never fold two live tabs together: two tabs on
// one URL are two things the user can switch to, and collapsing them is how a
// tab becomes unreachable.
TEST_F(TabSearchServiceTest, TwoLiveTabsAtTheSameUrlAreBothReported) {
  AddTabWithTitle(GURL("https://same.example/"), u"First copy");
  AddTabWithTitle(GURL("https://same.example/"), u"Second copy");

  std::vector<SearchResult> results = Search(u"copy", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_NE(results[0].tab_handle, results[1].tab_handle);
  EXPECT_TRUE(results[0].tab_handle.Get());
  EXPECT_TRUE(results[1].tab_handle.Get());
}

// A live result names its tab by handle, which is weak, rather than by strip
// index — the index it had when the archive read was posted may not be the
// index it has when the answer arrives.
TEST_F(TabSearchServiceTest, ALiveResultNamesItsTabByHandle) {
  AddTabWithTitle(GURL("https://a.example/"), u"Alpha");
  AddTabWithTitle(GURL("https://b.example/"), u"Beta");

  std::vector<SearchResult> results = Search(u"beta", 10);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(strip()->GetTabAtIndex(1), results[0].tab_handle.Get());

  // Closing the tab leaves the handle null rather than dangling or stale.
  const tabs::TabHandle handle = results[0].tab_handle;
  strip()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_NONE);
  EXPECT_FALSE(handle.Get());
}

// A callback bound through the service's WeakPtr is dropped when the service
// goes away mid-read rather than writing into freed state.
TEST_F(TabSearchServiceTest, AReadOutstandingWhenTheServiceDiesIsDropped) {
  AddTabWithTitle(GURL("https://a.example/"), u"gone");
  archive_.Add(MakeArchived("https://b.example/", u"gone"));

  bool ran = false;
  service_->Search(u"gone", 10,
                   base::BindLambdaForTesting(
                       [&](std::vector<SearchResult>) { ran = true; }));
  service_.reset();
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(ran);
}

}  // namespace
}  // namespace arcium
