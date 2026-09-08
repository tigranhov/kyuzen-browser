// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model_store.h"

#include <string>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/model_serializer.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ModelStoreTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  base::FilePath path() const {
    return dir_.GetPath().AppendASCII("model.json");
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir dir_;
};

TEST_F(ModelStoreTest, LoadingAMissingFileLeavesAnEmptyUsableModel) {
  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(model.entries().empty());
  EXPECT_EQ(1u, model.spaces().size());
}

TEST_F(ModelStoreTest, AMutationIsWrittenAfterTheSaveDelay) {
  ArciumModel model;
  ModelStore store(&model, path());
  model.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");

  // Nothing on disk yet: the write is debounced, not synchronous.
  EXPECT_FALSE(base::PathExists(path()));

  task_environment_.FastForwardBy(ModelStore::kSaveDelay);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(base::PathExists(path()));
}

TEST_F(ModelStoreTest, ABurstOfMutationsWritesOnce) {
  ArciumModel model;
  ModelStore store(&model, path());
  for (int i = 0; i < 10; ++i) {
    model.AddEntry(EntryKind::kPinned,
                   GURL("https://a.example/" + base::NumberToString(i)), u"A");
  }
  EXPECT_EQ(1, store.scheduled_save_count_for_testing());

  task_environment_.FastForwardBy(ModelStore::kSaveDelay);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, store.initiated_save_count_for_testing());
}

TEST_F(ModelStoreTest, WhatWasSavedComesBack) {
  {
    ArciumModel model;
    ModelStore store(&model, path());
    const EntryId id =
        model.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
    model.SetCustomTitle(id, u"Renamed");
    task_environment_.FastForwardBy(ModelStore::kSaveDelay);
    task_environment_.RunUntilIdle();
  }

  ArciumModel restored;
  ModelStore store(&restored, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  ASSERT_EQ(1u, restored.entries().size());
  EXPECT_EQ(u"Renamed", restored.entries()[0].custom_title);
}

TEST_F(ModelStoreTest, ACorruptFileLeavesAUsableModel) {
  ASSERT_TRUE(base::WriteFile(path(), "{ this is not json"));
  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(model.entries().empty());
  EXPECT_EQ(1u, model.spaces().size());
}

TEST_F(ModelStoreTest, LoadingDoesNotScheduleAWriteOfWhatWasJustRead) {
  {
    ArciumModel model;
    ModelStore store(&model, path());
    model.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
    task_environment_.FastForwardBy(ModelStore::kSaveDelay);
    task_environment_.RunUntilIdle();
  }

  ArciumModel restored;
  ModelStore store(&restored, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();

  // Reading a file must not dirty the model. A rewrite on every launch is
  // work no user asked for, and it is what raced the temp dir at teardown.
  EXPECT_EQ(0, store.scheduled_save_count_for_testing());

  // A genuine user mutation after the load must still schedule normally.
  restored.AddEntry(EntryKind::kPinned, GURL("https://b.example/"), u"B");
  EXPECT_EQ(1, store.scheduled_save_count_for_testing());

  // Flush before teardown so the pending write from the assertion above
  // doesn't race ScopedTempDir's cleanup at the end of the test.
  task_environment_.FastForwardBy(ModelStore::kSaveDelay);
  task_environment_.RunUntilIdle();
}

TEST_F(ModelStoreTest, AVersionOneFileLoadsWithItsFoldersAtTheTopLevel) {
  const std::string space = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string folder = base::Uuid::GenerateRandomV4().AsLowercaseString();
  // Byte for byte what Stage 2 wrote: version 1, and no parent_id anywhere.
  const std::string v1 =
      R"({"version":1,)"
      R"("spaces":[{"id":")" +
      space +
      R"(","name":"Space","archive_timeout":"12h","position":0}],)"
      R"("folders":[{"id":")" +
      folder + R"(","space_id":")" + space +
      R"(","name":"Work","collapsed":true,"position":0}],)"
      R"("entries":[]})";
  ASSERT_TRUE(base::WriteFile(path(), v1));

  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();

  // A2.5.3: everything the Stage 2 file had is intact, and its folders are
  // where "no parent" has always put them.
  ASSERT_EQ(1u, model.folders().size());
  const Folder* restored = model.GetFolder(FolderId::FromString(folder));
  ASSERT_TRUE(restored);
  EXPECT_EQ(u"Work", restored->name);
  EXPECT_TRUE(restored->collapsed);
  EXPECT_FALSE(restored->parent_id.has_value());
  EXPECT_EQ(0, model.FolderDepth(restored->id));
}

TEST_F(ModelStoreTest, AFileFromTheFutureLeavesAnEmptyModelAndIsMovedAside) {
  const std::string from_the_future =
      R"({"version":9999,"spaces":[],"folders":[],"entries":[]})";
  ASSERT_TRUE(base::WriteFile(path(), from_the_future));

  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();

  EXPECT_TRUE(model.entries().empty());
  EXPECT_TRUE(model.folders().empty());
  EXPECT_EQ(1u, model.spaces().size());

  // Moved aside, not left in place: the next mutation schedules a save, and
  // the save would land on top of the only copy of whatever this file held.
  // Asserted by contents rather than by name -- what matters is that the
  // bytes survived somewhere, not what the sidecar is called.
  EXPECT_FALSE(base::PathExists(path()));
  base::FileEnumerator files(dir_.GetPath(), /*recursive=*/false,
                             base::FileEnumerator::FILES);
  const base::FilePath kept = files.Next();
  ASSERT_FALSE(kept.empty());
  EXPECT_TRUE(files.Next().empty());
  std::string kept_contents;
  ASSERT_TRUE(base::ReadFileToString(kept, &kept_contents));
  EXPECT_EQ(from_the_future, kept_contents);
}

}  // namespace
}  // namespace arcium
