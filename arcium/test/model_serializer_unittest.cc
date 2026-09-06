// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_serializer.h"

#include "arcium/browser/model/arcium_model.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
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

}  // namespace
}  // namespace arcium
