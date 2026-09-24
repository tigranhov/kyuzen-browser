// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/import_finder.h"

#include <algorithm>
#include <string>
#include <vector>

#include "arcium/browser/import/import_plan.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

std::string ReadFixture(const char* name) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string text;
  CHECK(base::ReadFileToString(
      root.AppendASCII("arcium/test/data/import").AppendASCII(name), &text));
  return text;
}

// Wraps text as a mozLz4 file of one uncompressed run, which is all a
// decoder needs to see.
std::string MozLz4(const std::string& text) {
  std::string file("mozLz40\0", 8);
  for (int shift = 0; shift < 32; shift += 8) {
    file.push_back(static_cast<char>((text.size() >> shift) & 0xFF));
  }
  size_t rest = text.size();
  file.push_back(static_cast<char>(std::min<size_t>(rest, 15) << 4));
  if (rest >= 15) {
    for (rest -= 15; rest >= 255; rest -= 255) {
      file.push_back(static_cast<char>(0xFF));
    }
    file.push_back(static_cast<char>(rest));
  }
  return file + text;
}

// A sidebar with one space, for profiles that only need to be told apart.
std::string OneSpace(const std::string& name) {
  return R"({"spaces": [{"uuid": "{)" + name + R"(}", "name": ")" + name +
         R"("}]})";
}

// A made-up home folder, laid out as Zen and Arc lay theirs out on a Mac.
class ImportFinderTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(home_.CreateUniqueTempDir()); }

  base::FilePath Support() const {
    return home_.GetPath().AppendASCII("Library").AppendASCII(
        "Application Support");
  }
  base::FilePath ZenRoot() const { return Support().AppendASCII("zen"); }
  base::FilePath ArcRoot() const { return Support().AppendASCII("Arc"); }

  static void Write(const base::FilePath& path, const std::string& text) {
    ASSERT_TRUE(base::CreateDirectory(path.DirName()));
    ASSERT_TRUE(base::WriteFile(path, text));
  }

  // A Zen profile folder under Profiles/ holding `session` as its sidebar.
  void WriteZenProfile(const std::string& folder, const std::string& session) {
    Write(ZenRoot()
              .AppendASCII("Profiles")
              .AppendASCII(folder)
              .AppendASCII("zen-sessions.jsonlz4"),
          MozLz4(session));
  }

  void WriteProfilesIni(const std::string& text) {
    Write(ZenRoot().AppendASCII("profiles.ini"), text);
  }

  static std::vector<std::string> Names(const std::vector<FoundSource>& found) {
    std::vector<std::string> names;
    for (const FoundSource& source : found) {
      names.push_back(source.kind == ImportSourceKind::kZen
                          ? "zen:" + source.profile_name
                          : "arc");
    }
    return names;
  }

  base::ScopedTempDir home_;
};

TEST_F(ImportFinderTest, FindsZenThenArc) {
  WriteZenProfile("a1.Default (release)", ReadFixture("zen-sessions.json"));
  Write(ZenRoot()
            .AppendASCII("Profiles")
            .AppendASCII("a1.Default (release)")
            .AppendASCII("containers.json"),
        ReadFixture("zen-containers.json"));
  WriteProfilesIni(
      "[Install1A2B]\nDefault=Profiles/a1.Default (release)\n\n"
      "[Profile0]\nName=Default (release)\nIsRelative=1\n"
      "Path=Profiles/a1.Default (release)\n");
  Write(ArcRoot().AppendASCII("StorableSidebar.json"),
        ReadFixture("arc-sidebar.json"));

  const std::vector<FoundSource> found = FindSources(home_.GetPath());
  ASSERT_EQ((std::vector<std::string>{"zen:Default (release)", "arc"}),
            Names(found));
  EXPECT_EQ(2u, found[0].plan.counts().spaces);
  EXPECT_EQ("Work", found[0].plan.profiles[0].name);
  EXPECT_EQ(2u, found[1].plan.counts().spaces);
}

TEST_F(ImportFinderTest, AHomeWithNeitherFindsNothing) {
  EXPECT_TRUE(FindSources(home_.GetPath()).empty());
}

// profiles.ini lists profiles in the order they were made; the one Zen
// starts is the one its install names, and it comes first.
TEST_F(ImportFinderTest, TheProfileZenStartsWithComesFirst) {
  WriteZenProfile("old", OneSpace("Old"));
  WriteZenProfile("new", OneSpace("New"));
  WriteProfilesIni(
      "[Profile0]\nName=old\nIsRelative=1\nPath=Profiles/old\nDefault=1\n\n"
      "[Profile1]\nName=new\nIsRelative=1\nPath=Profiles/new\n\n"
      "[Install1A2B]\nDefault=Profiles/new\nLocked=1\n");
  EXPECT_EQ((std::vector<std::string>{"zen:new", "zen:old"}),
            Names(FindSources(home_.GetPath())));
}

// Without an install section, Firefox's older default mark decides.
TEST_F(ImportFinderTest, WithoutAnInstallTheMarkedDefaultComesFirst) {
  WriteZenProfile("one", OneSpace("One"));
  WriteZenProfile("two", OneSpace("Two"));
  WriteProfilesIni(
      "[Profile0]\nName=one\nIsRelative=1\nPath=Profiles/one\n\n"
      "[Profile1]\nName=two\nIsRelative=1\nPath=Profiles/two\nDefault=1\n");
  EXPECT_EQ((std::vector<std::string>{"zen:two", "zen:one"}),
            Names(FindSources(home_.GetPath())));
}

// Two installs each name a profile; the one saved last stands for this
// Mac's Zen.
TEST_F(ImportFinderTest, OfTwoInstallsTheOneSavedLastComesFirst) {
  WriteZenProfile("beta", OneSpace("Beta"));
  WriteZenProfile("main", OneSpace("Main"));
  const base::FilePath beta = ZenRoot()
                                  .AppendASCII("Profiles")
                                  .AppendASCII("beta")
                                  .AppendASCII("zen-sessions.jsonlz4");
  const base::Time long_ago = base::Time::Now() - base::Days(30);
  ASSERT_TRUE(base::TouchFile(beta, long_ago, long_ago));
  WriteProfilesIni(
      "[Install1111]\nDefault=Profiles/beta\n\n"
      "[Install2222]\nDefault=Profiles/main\n\n"
      "[Profile0]\nName=beta\nIsRelative=1\nPath=Profiles/beta\n\n"
      "[Profile1]\nName=main\nIsRelative=1\nPath=Profiles/main\n");
  EXPECT_EQ((std::vector<std::string>{"zen:main", "zen:beta"}),
            Names(FindSources(home_.GetPath())));
}

// A profile nobody arranged is not worth a choice.
TEST_F(ImportFinderTest, AProfileWithNoSpacesIsNotOffered) {
  WriteZenProfile("full", OneSpace("Full"));
  WriteZenProfile("bare", "{}");
  WriteProfilesIni(
      "[Profile0]\nName=bare\nIsRelative=1\nPath=Profiles/bare\n\n"
      "[Profile1]\nName=full\nIsRelative=1\nPath=Profiles/full\n");
  EXPECT_EQ((std::vector<std::string>{"zen:full"}),
            Names(FindSources(home_.GetPath())));
}

// Zen falls back to the copy it made at its last clean quit when the main
// file is damaged, and so does the finder.
TEST_F(ImportFinderTest, ADamagedSidebarFallsBackToTheCleanCopy) {
  WriteZenProfile("p", OneSpace("Broken"));
  const base::FilePath folder =
      ZenRoot().AppendASCII("Profiles").AppendASCII("p");
  Write(folder.AppendASCII("zen-sessions.jsonlz4"), "mozLz40\0garbage");
  const base::FilePath clean =
      folder.AppendASCII("zen-sessions-backup").AppendASCII("clean.jsonlz4");
  Write(clean, MozLz4(OneSpace("Clean")));
  WriteProfilesIni("[Profile0]\nName=p\nIsRelative=1\nPath=Profiles/p\n");
  const std::vector<FoundSource> found = FindSources(home_.GetPath());
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(clean, found[0].path);
  EXPECT_EQ("Clean", found[0].plan.spaces[0].name);
}

// The sidebar file names Arc's profiles by folder; Arc's own list has the
// names people chose.
TEST_F(ImportFinderTest, ArcProfilesKeepTheNamesArcShows) {
  Write(ArcRoot().AppendASCII("StorableSidebar.json"),
        ReadFixture("arc-sidebar.json"));
  Write(ArcRoot().AppendASCII("User Data").AppendASCII("Local State"),
        R"({"profile": {"info_cache": {"Profile 1": {"name": "Client"}}}})");
  const std::vector<FoundSource> found = FindSources(home_.GetPath());
  ASSERT_EQ(1u, found.size());
  ASSERT_EQ(1u, found[0].plan.profiles.size());
  EXPECT_EQ("Client", found[0].plan.profiles[0].name);
}

// A picked file is read for what it is, whatever its name.
TEST_F(ImportFinderTest, APickedFileIsReadForWhatItIs) {
  const base::FilePath zen = home_.GetPath().AppendASCII("copied.jsonlz4");
  Write(zen, ReadFixture("zen-sessions.jsonlz4"));
  const base::FilePath arc = home_.GetPath().AppendASCII("copied.json");
  Write(arc, ReadFixture("arc-sidebar.json"));
  const base::FilePath text = home_.GetPath().AppendASCII("notes.txt");
  Write(text, "a shopping list");

  const std::optional<FoundSource> from_zen = ReadSourceFile(zen);
  ASSERT_TRUE(from_zen);
  EXPECT_EQ(ImportSourceKind::kZen, from_zen->kind);
  // Its containers file stayed on the other Mac.
  EXPECT_EQ("Container 2", from_zen->plan.profiles[0].name);
  const std::optional<FoundSource> from_arc = ReadSourceFile(arc);
  ASSERT_TRUE(from_arc);
  EXPECT_EQ(ImportSourceKind::kArc, from_arc->kind);
  EXPECT_FALSE(ReadSourceFile(text));
  EXPECT_FALSE(ReadSourceFile(home_.GetPath().AppendASCII("missing")));
}

}  // namespace
}  // namespace arcium
