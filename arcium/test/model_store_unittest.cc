// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model_store.h"

#include "arcium/browser/model/arcium_model.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
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

TEST_F(ModelStoreTest, LoadRecordsTheSaveTimeAsTheIdleFloor) {
  {
    ArciumModel model;
    ModelStore store(&model, path());
    model.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
    task_environment_.FastForwardBy(ModelStore::kSaveDelay);
    task_environment_.RunUntilIdle();
  }

  ArciumModel restored;
  ModelStore store(&restored, path());
  EXPECT_TRUE(store.last_save_time().is_null());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  // Task 9 uses this as the idle floor for restored tabs; null means a
  // browser closed overnight never archives yesterday's Today tabs.
  EXPECT_FALSE(store.last_save_time().is_null());
}

TEST_F(ModelStoreTest, LoadingAMissingFileLeavesTheIdleFloorNull) {
  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(store.last_save_time().is_null());
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

}  // namespace
}  // namespace arcium
