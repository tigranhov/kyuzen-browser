// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/archive_store.h"

#include <tuple>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ArchiveStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    ASSERT_TRUE(store_.Open(dir_.GetPath().AppendASCII("archive.db")));
    space_ = SpaceId::Generate();
  }

  ArchivedTab MakeTab(const std::string& url,
                      const std::u16string& title,
                      base::Time when) {
    ArchivedTab tab;
    tab.url = GURL(url);
    tab.title = title;
    tab.space_id = space_;
    tab.archived_at = when;
    return tab;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  ArchiveStore store_;
  SpaceId space_;
};

TEST_F(ArchiveStoreTest, AddedTabsComeBackNewestFirst) {
  const base::Time now = base::Time::Now();
  store_.Add(MakeTab("https://old.example/", u"Old", now - base::Hours(2)));
  store_.Add(MakeTab("https://new.example/", u"New", now));
  store_.Add(MakeTab("https://mid.example/", u"Mid", now - base::Hours(1)));

  std::vector<ArchivedTab> tabs = store_.ListRecent(space_, 10);
  ASSERT_EQ(3u, tabs.size());
  EXPECT_EQ(GURL("https://new.example/"), tabs[0].url);
  EXPECT_EQ(GURL("https://mid.example/"), tabs[1].url);
  EXPECT_EQ(GURL("https://old.example/"), tabs[2].url);
}

TEST_F(ArchiveStoreTest, ListRecentHonoursTheLimit) {
  const base::Time now = base::Time::Now();
  for (int i = 0; i < 10; ++i) {
    store_.Add(MakeTab("https://example.com/" + base::NumberToString(i), u"T",
                       now - base::Minutes(i)));
  }
  EXPECT_EQ(3u, store_.ListRecent(space_, 3).size());
}

TEST_F(ArchiveStoreTest, ListRecentIsScopedToOneSpace) {
  const SpaceId other = SpaceId::Generate();
  store_.Add(MakeTab("https://mine.example/", u"Mine", base::Time::Now()));
  ArchivedTab theirs =
      MakeTab("https://theirs.example/", u"Theirs", base::Time::Now());
  theirs.space_id = other;
  store_.Add(theirs);

  std::vector<ArchivedTab> tabs = store_.ListRecent(space_, 10);
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ(GURL("https://mine.example/"), tabs[0].url);
}

TEST_F(ArchiveStoreTest, SearchIsCaseAndDiacriticInsensitive) {
  const base::Time now = base::Time::Now();
  store_.Add(
      MakeTab("https://github.com/tigranhov/arcium", u"Arcium repo", now));
  store_.Add(MakeTab("https://de.example/", u"ÖKONOMIE heute", now));

  ASSERT_EQ(1u, store_.Search(u"ARCIUM", 10).size());
  EXPECT_EQ(u"Arcium repo", store_.Search(u"ARCIUM", 10)[0].title);
  ASSERT_EQ(1u, store_.Search(u"github", 10).size());
  EXPECT_EQ(u"Arcium repo", store_.Search(u"github", 10)[0].title);
  // The half that silently did not work before: lower(title) LIKE ? plus
  // base::ToLowerASCII is byte-identical to a plain LIKE, so a non-ASCII
  // query used to match nothing.
  ASSERT_EQ(1u, store_.Search(u"ökonomie", 10).size());
  EXPECT_EQ(u"ÖKONOMIE heute", store_.Search(u"ökonomie", 10)[0].title);
  EXPECT_EQ(0u, store_.Search(u"nothing here", 10).size());
}

TEST_F(ArchiveStoreTest, SearchEscapesLikeMetacharactersInTheQuery) {
  const base::Time now = base::Time::Now();
  store_.Add(MakeTab("https://a.example/", u"100% done", now));
  // Contains "100" but not the literal substring "100%". An unescaped needle
  // turns the query's '%' into a wildcard, so "100%" would match this row
  // too (and, in the reviewer's measurement against a larger corpus, 140
  // rows instead of 1).
  store_.Add(MakeTab("https://b.example/", u"1005 users online", now));

  std::vector<ArchivedTab> results = store_.Search(u"100%", 10);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(u"100% done", results[0].title);
}

TEST_F(ArchiveStoreTest, RemoveDropsOneRow) {
  const base::Time now = base::Time::Now();
  store_.Add(MakeTab("https://a.example/", u"A", now));
  store_.Remove(GURL("https://a.example/"), now);
  EXPECT_TRUE(store_.ListRecent(space_, 10).empty());
}

TEST_F(ArchiveStoreTest, ReopeningTheDatabaseKeepsItsRows) {
  store_.Add(MakeTab("https://a.example/", u"A", base::Time::Now()));
  const base::FilePath path = dir_.GetPath().AppendASCII("archive.db");

  ArchiveStore reopened;
  ASSERT_TRUE(reopened.Open(path));
  EXPECT_EQ(1u, reopened.ListRecent(space_, 10).size());
}

TEST_F(ArchiveStoreTest, ASecondConnectionDoesNotDestroyTheFirstsData) {
  store_.Add(MakeTab("https://keep.example/", u"Keep", base::Time::Now()));
  const base::FilePath path = dir_.GetPath().AppendASCII("archive.db");

  // A second live connection must never be mistaken for corruption. Whether
  // it succeeds or fails on lock contention is not the point of this test.
  ArchiveStore second;
  std::ignore = second.Open(path);

  // Whatever the second connection concluded, the data is still there.
  ArchiveStore third;
  ASSERT_TRUE(third.Open(path));
  EXPECT_EQ(1u, third.ListRecent(space_, 10).size());
}

TEST_F(ArchiveStoreTest, OpeningACorruptFileStartsAFreshDatabase) {
  const base::FilePath path = dir_.GetPath().AppendASCII("corrupt.db");
  ASSERT_TRUE(base::WriteFile(path, "this is not a sqlite database"));
  ArchiveStore store;
  EXPECT_TRUE(store.Open(path));
  store.Add(MakeTab("https://a.example/", u"A", base::Time::Now()));
  EXPECT_EQ(1u, store.ListRecent(space_, 10).size());
}

}  // namespace
}  // namespace arcium
