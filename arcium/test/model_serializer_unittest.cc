// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_serializer.h"

#include <set>
#include <utility>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/folder.h"
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
  const FolderId folder = original.AddFolder(u"Work");
  const EntryId pinned =
      original.AddEntry(EntryKind::kPinned, GURL("https://pin.example/"), u"P");
  original.SetEntryFolder(pinned, folder);
  original.SetCustomTitle(pinned, u"Renamed");
  original.AddEntry(EntryKind::kFavorite, GURL("https://fav.example/"), u"F");

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

TEST(ModelSerializerTest, RoundTripPreservesPositionsAndCollapsedState) {
  ArciumModel original;
  const FolderId folder = original.AddFolder(u"Work");
  original.SetFolderCollapsed(folder, true);
  const EntryId first =
      original.AddEntry(EntryKind::kPinned, GURL("https://one.example/"), u"1");
  const EntryId second =
      original.AddEntry(EntryKind::kPinned, GURL("https://two.example/"), u"2");

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
  const EntryId id =
      original.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
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
  original.AddEntry(EntryKind::kPinned, GURL("https://keep.example/"), u"Keep");
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
  original.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
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
  original.AddEntry(EntryKind::kPinned, GURL("https://good.example/"), u"good");
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
  original.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  std::string json = *base::WriteJson(SerializeModel(original));
  const std::string truncated = json.substr(0, json.size() / 2);
  EXPECT_FALSE(
      base::JSONReader::ReadDict(truncated, base::JSON_PARSE_RFC).has_value());
}

TEST(ModelSerializerTest, AnEntryInAnUnknownFolderLandsAtTheTopLevel) {
  ArciumModel original;
  const EntryId id =
      original.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
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
  const FolderId outer = source.AddFolder(u"Outer");
  const FolderId inner = source.AddFolder(u"Inner", outer);

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

}  // namespace
}  // namespace arcium
