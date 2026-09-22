// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_serializer.h"

#include <set>
#include <utility>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/space.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

TEST(ModelSerializerTest, RoundTripPreservesEntriesFoldersAndSpaces) {
  ArciumModel original;
  original.SetArchiveTimeout(original.default_space_id(),
                             ArchiveTimeout::kSevenDays);
  const FolderId folder = original.AddFolderForTesting(u"Work");
  const EntryId pinned = original.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://pin.example/"), u"P");
  original.SetEntryFolder(pinned, folder);
  original.SetCustomTitle(pinned, u"Renamed");
  original.AddEntryForTesting(EntryKind::kFavorite,
                              GURL("https://fav.example/"), u"F");

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));

  ASSERT_EQ(1u, restored.spaces().size());
  EXPECT_EQ(original.default_space_id(), restored.default_space_id());
  EXPECT_EQ(ArchiveTimeout::kSevenDays, restored.spaces()[0].archive_timeout);

  ASSERT_EQ(1u, restored.folders().size());
  EXPECT_EQ(folder, restored.folders()[0].id);
  EXPECT_EQ(u"Work", restored.folders()[0].name);

  const TabEntry* entry = restored.GetEntry(pinned);
  ASSERT_TRUE(entry);
  EXPECT_EQ(EntryKind::kPinned, entry->kind);
  EXPECT_EQ(GURL("https://pin.example/"), entry->url);
  EXPECT_EQ(u"Renamed", entry->custom_title);
  EXPECT_EQ(u"P", entry->last_title);
  ASSERT_TRUE(entry->folder_id.has_value());
  EXPECT_EQ(folder, *entry->folder_id);
  EXPECT_EQ(
      1u,
      restored.EntriesForKind(restored.default_space_id(), EntryKind::kFavorite)
          .size());
}

TEST(ModelSerializerTest, ASpaceRemembersTheTwoTabsSharingItsScreen) {
  ArciumModel original;
  const TabKey first = TabKey::Generate();
  const TabKey second = TabKey::Generate();
  original.SetSpaceSplit(original.default_space_id(),
                         SpaceSplit{first, second, /*stacked=*/true, 0.4});

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));

  ASSERT_EQ(1u, restored.spaces().size());
  const std::optional<SpaceSplit>& split = restored.spaces()[0].split;
  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(first, split->first);
  EXPECT_EQ(second, split->second);
  EXPECT_TRUE(split->stacked);
  EXPECT_DOUBLE_EQ(0.4, split->ratio);
}

TEST(ModelSerializerTest, ASpaceWithNothingSharingItsScreenWritesNoSplit) {
  ArciumModel original;
  const std::string json = base::WriteJson(SerializeModel(original)).value();
  EXPECT_EQ(std::string::npos, json.find("split"));
}

TEST(ModelSerializerTest, ASplitNamingOnlyOneTabIsDropped) {
  // Half a record would ask for a split of a tab with itself, and a whole
  // model file is not refused over a convenience.
  ArciumModel original;
  original.SetSpaceSplit(original.default_space_id(),
                         SpaceSplit{TabKey::Generate(), TabKey::Generate()});
  base::DictValue dict = SerializeModel(original);
  base::ListValue* spaces = dict.FindList("spaces");
  ASSERT_TRUE(spaces);
  (*spaces)[0].GetDict().FindDict("split")->Remove("second");

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  ASSERT_EQ(1u, restored.spaces().size());
  EXPECT_FALSE(restored.spaces()[0].split.has_value());
}

TEST(ModelSerializerTest, ASplitNamingOneTabTwiceIsDropped) {
  ArciumModel original;
  const TabKey only = TabKey::Generate();
  original.SetSpaceSplit(original.default_space_id(),
                         SpaceSplit{only, TabKey::Generate()});
  base::DictValue dict = SerializeModel(original);
  base::ListValue* spaces = dict.FindList("spaces");
  ASSERT_TRUE(spaces);
  (*spaces)[0].GetDict().FindDict("split")->Set("second", only.value());

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  EXPECT_FALSE(restored.spaces()[0].split.has_value());
}

TEST(ModelSerializerTest, RoundTripPreservesPositionsAndCollapsedState) {
  ArciumModel original;
  const FolderId folder = original.AddFolderForTesting(u"Work");
  original.SetFolderCollapsed(folder, true);
  const EntryId first = original.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://one.example/"), u"1");
  const EntryId second = original.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://two.example/"), u"2");

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));

  ASSERT_EQ(1u, restored.folders().size());
  EXPECT_TRUE(restored.folders()[0].collapsed);
  EXPECT_EQ(0, restored.folders()[0].position);

  const TabEntry* restored_first = restored.GetEntry(first);
  const TabEntry* restored_second = restored.GetEntry(second);
  ASSERT_TRUE(restored_first);
  ASSERT_TRUE(restored_second);
  EXPECT_EQ(0, restored_first->position);
  EXPECT_EQ(1, restored_second->position);
}

TEST(ModelSerializerTest, CreatedAtSurvivesToTheMicrosecond) {
  ArciumModel original;
  const EntryId id = original.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  // An odd microsecond count is what a double silently rounds away.
  const base::Time odd = base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(13442473600000001));
  const_cast<TabEntry*>(original.GetEntry(id))->created_at = odd;

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));
  ASSERT_TRUE(restored.GetEntry(id));
  EXPECT_EQ(odd, restored.GetEntry(id)->created_at);
}

TEST(ModelSerializerTest, AFileWhoseSpacesAreAllMalformedStillLoadsItsEntries) {
  ArciumModel original;
  original.AddEntryForTesting(EntryKind::kPinned, GURL("https://keep.example/"),
                              u"Keep");
  base::DictValue dict = SerializeModel(original);
  base::ListValue* spaces = dict.FindList("spaces");
  ASSERT_TRUE(spaces);
  (*spaces)[0].GetDict().Set("id", "not-a-uuid");

  ArciumModel restored;
  // Row-level damage: the entry is the data that matters and it survives.
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  ASSERT_EQ(1u, restored.entries().size());
  EXPECT_EQ(GURL("https://keep.example/"), restored.entries()[0].url);
  ASSERT_EQ(1u, restored.spaces().size());
  EXPECT_EQ(restored.default_space_id(), restored.entries()[0].space_id);
}

TEST(ModelSerializerTest, UnknownFieldsAreIgnoredNotFatal) {
  ArciumModel original;
  original.AddEntryForTesting(EntryKind::kPinned, GURL("https://a.example/"),
                              u"A");
  base::DictValue dict = SerializeModel(original);
  dict.Set("something_from_the_future", "hello");
  base::ListValue* entries = dict.FindList("entries");
  ASSERT_TRUE(entries);
  (*entries)[0].GetDict().Set("also_new", 42);

  ArciumModel restored;
  EXPECT_TRUE(DeserializeModel(dict, &restored));
  EXPECT_EQ(1u, restored.entries().size());
}

TEST(ModelSerializerTest, ANewerSchemaVersionIsRefused) {
  ArciumModel original;
  base::DictValue dict = SerializeModel(original);
  dict.Set("version", kModelSchemaVersion + 1);

  ArciumModel restored;
  EXPECT_FALSE(DeserializeModel(dict, &restored));
}

TEST(ModelSerializerTest, AMissingVersionIsRefused) {
  base::DictValue dict;
  dict.Set("entries", base::ListValue());

  ArciumModel restored;
  EXPECT_FALSE(DeserializeModel(dict, &restored));
}

TEST(ModelSerializerTest, EntriesWithBadIdsOrUrlsAreDroppedNotFatal) {
  ArciumModel original;
  original.AddEntryForTesting(EntryKind::kPinned, GURL("https://good.example/"),
                              u"good");
  base::DictValue dict = SerializeModel(original);
  base::ListValue* entries = dict.FindList("entries");
  ASSERT_TRUE(entries);

  base::DictValue bad_id;
  bad_id.Set("id", "not-a-uuid");
  bad_id.Set("kind", "pinned");
  bad_id.Set("url", "https://bad.example/");
  entries->Append(std::move(bad_id));

  base::DictValue bad_url;
  bad_url.Set("id", EntryId::Generate().value());
  bad_url.Set("kind", "pinned");
  bad_url.Set("url", "not a url");
  entries->Append(std::move(bad_url));

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  EXPECT_EQ(1u, restored.entries().size());
  EXPECT_EQ(GURL("https://good.example/"), restored.entries()[0].url);
}

TEST(ModelSerializerTest, TruncatedJsonDoesNotParse) {
  ArciumModel original;
  original.AddEntryForTesting(EntryKind::kPinned, GURL("https://a.example/"),
                              u"A");
  std::string json = *base::WriteJson(SerializeModel(original));
  const std::string truncated = json.substr(0, json.size() / 2);
  EXPECT_FALSE(
      base::JSONReader::ReadDict(truncated, base::JSON_PARSE_RFC).has_value());
}

TEST(ModelSerializerTest, AnEntryInAnUnknownFolderLandsAtTheTopLevel) {
  ArciumModel original;
  const EntryId id = original.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  base::DictValue dict = SerializeModel(original);
  base::ListValue* entries = dict.FindList("entries");
  ASSERT_TRUE(entries);
  (*entries)[0].GetDict().Set("folder_id", FolderId::Generate().value());

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  const TabEntry* entry = restored.GetEntry(id);
  ASSERT_TRUE(entry);
  EXPECT_FALSE(entry->folder_id.has_value());
}

TEST(ModelSerializerTest, RoundTripPreservesTheFolderTree) {
  ArciumModel source;
  const FolderId outer = source.AddFolderForTesting(u"Outer");
  const FolderId inner = source.AddFolderForTesting(u"Inner", outer);

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(source), &restored));

  ASSERT_TRUE(restored.GetFolder(inner));
  EXPECT_EQ(outer, restored.GetFolder(inner)->parent_id);
  EXPECT_EQ(1, restored.FolderDepth(inner));
  ASSERT_TRUE(restored.GetFolder(outer));
  EXPECT_FALSE(restored.GetFolder(outer)->parent_id.has_value());
}

TEST(ModelSerializerTest, AParentThatIsNotInTheFileLandsAtTheTopLevel) {
  const std::string space = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string another_root =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string folder = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string missing =
      base::Uuid::GenerateRandomV4().AsLowercaseString();

  base::DictValue dict;
  dict.Set("version", kModelSchemaVersion);
  base::ListValue spaces;
  base::DictValue space_value;
  space_value.Set("id", space);
  spaces.Append(std::move(space_value));
  dict.Set("spaces", std::move(spaces));
  base::ListValue folders;
  // A genuine top-level folder ahead of the dangling one in the file. Its
  // presence matters: the cycle/depth pass looks a missing parent id up with
  // std::map::operator[], which silently default-constructs an index of 0
  // for a key it does not have rather than signalling "not found". With only
  // one folder in the file that 0 would point back at the dangling folder
  // itself, and the cycle guard would coincidentally reset its parent for an
  // unrelated reason -- making this test pass even with the top-level repair
  // pass deleted. A second root ahead of it means index 0 lands on that
  // unrelated folder instead, so only the repair pass this test is about can
  // detach `folder`.
  base::DictValue root_value;
  root_value.Set("id", another_root);
  root_value.Set("space_id", space);
  folders.Append(std::move(root_value));
  base::DictValue folder_value;
  folder_value.Set("id", folder);
  folder_value.Set("space_id", space);
  folder_value.Set("parent_id", missing);
  folders.Append(std::move(folder_value));
  dict.Set("folders", std::move(folders));

  ArciumModel model;
  ASSERT_TRUE(DeserializeModel(dict, &model));
  const Folder* restored = model.GetFolder(FolderId::FromString(folder));
  ASSERT_TRUE(restored);
  EXPECT_FALSE(restored->parent_id.has_value());
}

TEST(ModelSerializerTest, AParentInAnotherSpaceLandsAtTheTopLevel) {
  const std::string space_a =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string space_b =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string parent = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string child = base::Uuid::GenerateRandomV4().AsLowercaseString();

  base::DictValue dict;
  dict.Set("version", kModelSchemaVersion);
  base::ListValue spaces;
  for (const std::string& id : {space_a, space_b}) {
    base::DictValue value;
    value.Set("id", id);
    spaces.Append(std::move(value));
  }
  dict.Set("spaces", std::move(spaces));

  base::ListValue folders;
  base::DictValue parent_value;
  parent_value.Set("id", parent);
  parent_value.Set("space_id", space_b);
  folders.Append(std::move(parent_value));
  base::DictValue child_value;
  child_value.Set("id", child);
  child_value.Set("space_id", space_a);
  // A parent that is in the file, and findable, but in the other space. A
  // folder tree does not straddle spaces, and this is the only damage the
  // first repair pass alone can catch -- the cycle pass would walk this link
  // happily, because there is nothing circular or too deep about it.
  child_value.Set("parent_id", parent);
  folders.Append(std::move(child_value));
  dict.Set("folders", std::move(folders));

  ArciumModel model;
  ASSERT_TRUE(DeserializeModel(dict, &model));
  const Folder* restored = model.GetFolder(FolderId::FromString(child));
  ASSERT_TRUE(restored);
  EXPECT_FALSE(restored->parent_id.has_value());
  // The parent is untouched in its own space; only the link was wrong.
  EXPECT_TRUE(model.GetFolder(FolderId::FromString(parent)));
}

TEST(ModelSerializerTest, ACycleBetweenFoldersIsBrokenNotFatal) {
  const std::string space = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string a = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string b = base::Uuid::GenerateRandomV4().AsLowercaseString();

  base::DictValue dict;
  dict.Set("version", kModelSchemaVersion);
  base::ListValue spaces;
  base::DictValue space_value;
  space_value.Set("id", space);
  spaces.Append(std::move(space_value));
  dict.Set("spaces", std::move(spaces));
  base::ListValue folders;
  for (const auto& [id, parent] :
       std::vector<std::pair<std::string, std::string>>{{a, b}, {b, a}}) {
    base::DictValue folder_value;
    folder_value.Set("id", id);
    folder_value.Set("space_id", space);
    folder_value.Set("parent_id", parent);
    folders.Append(std::move(folder_value));
  }
  dict.Set("folders", std::move(folders));

  ArciumModel model;
  ASSERT_TRUE(DeserializeModel(dict, &model));
  // Both folders survive; the cycle does not. Exactly one of them is a root,
  // and neither depth runs away.
  ASSERT_EQ(2u, model.folders().size());
  const int depth_a = model.FolderDepth(FolderId::FromString(a));
  const int depth_b = model.FolderDepth(FolderId::FromString(b));
  EXPECT_EQ(1, (depth_a == 0) + (depth_b == 0));
  EXPECT_LE(depth_a, kMaxFolderDepth - 1);
  EXPECT_LE(depth_b, kMaxFolderDepth - 1);
}

TEST(ModelSerializerTest, AFolderDeeperThanTheCapIsDetachedToTheTopLevel) {
  const std::string space = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::vector<std::string> ids;
  // Two levels past the cap, so the repair has something to do. A chain
  // exactly as deep as the cap is legal and would make every assertion below
  // pass without the repair pass existing at all.
  for (int i = 0; i < kMaxFolderDepth + 2; ++i) {
    ids.push_back(base::Uuid::GenerateRandomV4().AsLowercaseString());
  }

  base::DictValue dict;
  dict.Set("version", kModelSchemaVersion);
  base::ListValue spaces;
  base::DictValue space_value;
  space_value.Set("id", space);
  spaces.Append(std::move(space_value));
  dict.Set("spaces", std::move(spaces));
  base::ListValue folders;
  for (size_t i = 0; i < ids.size(); ++i) {
    base::DictValue folder_value;
    folder_value.Set("id", ids[i]);
    folder_value.Set("space_id", space);
    if (i > 0) {
      folder_value.Set("parent_id", ids[i - 1]);
    }
    folders.Append(std::move(folder_value));
  }
  dict.Set("folders", std::move(folders));

  ArciumModel model;
  ASSERT_TRUE(DeserializeModel(dict, &model));
  // Every folder is kept, and none of them is drawn deeper than the sidebar
  // can indent.
  ASSERT_EQ(ids.size(), model.folders().size());
  int detached = 0;
  for (const std::string& id : ids) {
    const FolderId parsed = FolderId::FromString(id);
    EXPECT_LE(model.FolderDepth(parsed), kMaxFolderDepth - 1);
    if (!model.GetFolder(parsed)->parent_id.has_value()) {
      ++detached;
    }
  }
  // The chain was longer than the cap, so the repair must have cut it
  // somewhere -- more than the one folder that was a root to begin with.
  EXPECT_GT(detached, 1);
}

TEST(ModelSerializerTest, ASpacesIconGradientAndLastTabSurviveARoundTrip) {
  ArciumModel written;
  const SpaceId id = written.AddSpace(u"Work");
  const TabKey key = TabKey::Generate();
  written.SetSpaceIcon(id, u"💼");
  written.SetSpaceGradient(id, 2);
  written.SetLastActiveTab(id, key);
  written.SetLastActiveSpace(id);

  ArciumModel read;
  ASSERT_TRUE(DeserializeModel(SerializeModel(written), &read));
  const Space* space = read.GetSpace(id);
  ASSERT_TRUE(space);
  EXPECT_EQ(u"💼", space->icon);
  EXPECT_EQ(2, space->gradient);
  EXPECT_EQ(key, space->last_active_tab);
  EXPECT_EQ(id, read.last_active_space());
}

TEST(ModelSerializerTest, ALastActiveSpaceNamingNothingFallsBackToTheFirst) {
  ArciumModel written;
  base::DictValue dict = SerializeModel(written);
  dict.Set("last_active_space", SpaceId::Generate().value());
  ArciumModel read;
  ASSERT_TRUE(DeserializeModel(dict, &read));
  EXPECT_EQ(read.default_space_id(), read.last_active_space());
}

TEST(ModelSerializerTest, AnUnreadableGradientOrIconLeavesTheDefaults) {
  ArciumModel written;
  base::DictValue dict = SerializeModel(written);
  base::ListValue* spaces = dict.FindList("spaces");
  ASSERT_TRUE(spaces);
  base::DictValue* space = (*spaces)[0].GetIfDict();
  ASSERT_TRUE(space);
  space->Set("gradient", "not a number");
  space->Set("icon", 7);
  ArciumModel read;
  ASSERT_TRUE(DeserializeModel(dict, &read));
  EXPECT_EQ(0, read.spaces().front().gradient);
  EXPECT_TRUE(read.spaces().front().icon.empty());
}

TEST(ModelSerializerTest, RoundTripPreservesProfilesAndEachSpacesProfile) {
  ArciumModel original;
  const ProfileId work = original.AddProfile(u"Work", 4);
  const SpaceId office = original.AddSpace(u"Office", work);

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));

  ASSERT_EQ(2u, restored.profiles().size());
  EXPECT_EQ(DefaultProfileId(), restored.profiles()[0].id);
  const ArciumProfile* profile = restored.GetProfile(work);
  ASSERT_TRUE(profile);
  EXPECT_EQ(u"Work", profile->name);
  EXPECT_EQ(4, profile->color);
  EXPECT_EQ(work, restored.ProfileOfSpace(office));
  EXPECT_EQ(DefaultProfileId(),
            restored.ProfileOfSpace(restored.default_space_id()));
}

TEST(ModelSerializerTest, ASpaceNamingAMissingProfileFallsBackToDefault) {
  ArciumModel original;
  const ProfileId work = original.AddProfile(u"Work", 1);
  const SpaceId office = original.AddSpace(u"Office", work);
  base::DictValue dict = SerializeModel(original);
  // Drop Work from the list but leave the space pointing at it.
  base::ListValue* profiles = dict.FindList("profiles");
  ASSERT_TRUE(profiles);
  profiles->EraseIf([&](const base::Value& value) {
    const std::string* id = value.GetDict().FindString("id");
    return id && *id == work.value();
  });

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  // The space's own stored id, not ProfileOfSpace's fallback: that accessor
  // has a guard of its own and would read Default even if deserializing had
  // left the dead id sitting in the space.
  EXPECT_EQ(DefaultProfileId(), restored.GetSpace(office)->profile_id);
}

TEST(ModelSerializerTest, ARemovedProfilesSpaceDoesNotSurviveARoundTrip) {
  ArciumModel original;
  const ProfileId work = original.AddProfile(u"Work", 1);
  const SpaceId office = original.AddSpace(u"Office", work);
  original.RemoveProfile(work);

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));
  EXPECT_EQ(DefaultProfileId(), restored.GetSpace(office)->profile_id);
}

TEST(ModelSerializerTest, AFileWithoutTheDefaultProfileStillHasIt) {
  ArciumModel original;
  base::DictValue dict = SerializeModel(original);
  dict.Set("profiles", base::ListValue());

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  ASSERT_EQ(1u, restored.profiles().size());
  EXPECT_EQ(DefaultProfileId(), restored.profiles()[0].id);
}

// Unlike a bad space, folder or entry, a profile row that cannot be used
// fails the whole read: PartitionPathsToKeep (arcium/browser/profile_data.h)
// trusts profiles() to be complete, and a skipped row here would make the
// keep list miss a real profile's directory while still reporting success --
// which is exactly the incomplete list Chrome's own sweep would delete.
TEST(ModelSerializerTest, AProfileThatCannotBeUsedFailsTheWholeRead) {
  ArciumModel original;
  original.AddProfile(u"Work", 1);
  base::DictValue dict = SerializeModel(original);
  base::ListValue* profiles = dict.FindList("profiles");
  ASSERT_TRUE(profiles);
  base::DictValue bad;
  bad.Set("id", "not-a-uuid");
  profiles->Append(std::move(bad));

  ArciumModel restored;
  EXPECT_FALSE(DeserializeModel(dict, &restored));
}

TEST(ModelSerializerTest, ARenamedDefaultProfileKeepsItsName) {
  ArciumModel original;
  original.RenameProfile(DefaultProfileId(), u"Personal");

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));
  ASSERT_EQ(1u, restored.profiles().size());
  EXPECT_EQ(u"Personal", restored.profiles()[0].name);
}

}  // namespace
}  // namespace arcium
