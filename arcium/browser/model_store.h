// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_STORE_H_
#define ARCIUM_BROWSER_MODEL_STORE_H_

#include <optional>

#include "arcium/browser/model/arcium_model.h"
#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"

namespace arcium {

// Persists ArciumModel as one JSON file, atomically and off the UI thread.
// Modelled on components/bookmarks/browser/bookmark_storage.h, which the
// master spec names as the precedent.
class ModelStore : public ArciumModel::Observer,
                   public base::ImportantFileWriter::BackgroundDataSerializer {
 public:
  // Matches BookmarkStorage. Long enough that a drag reordering ten rows
  // writes once, short enough that a crash loses almost nothing.
  static constexpr base::TimeDelta kSaveDelay = base::Milliseconds(2500);

  // `model` must not be null and must outlive this object: the destructor
  // dereferences it while flushing a pending write.
  ModelStore(ArciumModel* model, const base::FilePath& path);
  ModelStore(const ModelStore&) = delete;
  ModelStore& operator=(const ModelStore&) = delete;
  ~ModelStore() override;

  // Reads the file on a background sequence and runs `done` on the calling
  // sequence. Never blocks: the sidebar draws live tabs meanwhile.
  void Load(base::OnceClosure done);

  // If there is a pending write, performs it immediately. For tests only:
  // production code relies on the debounced schedule, not a forced flush.
  void SaveNowForTesting();

  // When the model was last written, either by a debounced save or by
  // loading an existing file from disk. Used as the idle floor for tabs
  // restored after a quit, so a browser closed overnight archives
  // yesterday's tabs.
  base::Time last_save_time() const { return last_save_time_; }

  int scheduled_save_count_for_testing() const { return scheduled_saves_; }
  int initiated_save_count_for_testing() const { return initiated_saves_; }

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

  // base::ImportantFileWriter::BackgroundDataSerializer:
  base::ImportantFileWriter::BackgroundDataProducerCallback
  GetSerializedDataProducerForBackgroundSequence() override;

  // Result of reading the file on the background sequence. Public so the
  // free function that produces it (in model_store.cc's anonymous namespace)
  // can name the type; constructed only by ModelStore and its .cc helper.
  struct LoadResult {
    std::optional<base::DictValue> dict;
    base::Time last_modified;
  };

 private:
  // Shared body for the destructor and SaveNowForTesting(). Named apart from
  // the ForTesting() method so production code (the destructor) never calls
  // a function whose name matches PRESUBMIT's test-only pattern.
  void SaveNowIfScheduled();

  void OnLoaded(base::OnceClosure done, LoadResult result);

  raw_ptr<ArciumModel> model_;
  scoped_refptr<base::SequencedTaskRunner> background_runner_;
  base::ImportantFileWriter writer_;
  base::Time last_save_time_;
  int scheduled_saves_ = 0;
  int initiated_saves_ = 0;
  // Suppresses OnArciumModelChanged() while Load() is applying a file to the
  // model: ArciumModel::ReplaceAll()'s notification must not be mistaken for
  // a user mutation, or every successful load would schedule a save of the
  // bytes it just read.
  bool loading_ = false;
  base::WeakPtrFactory<ModelStore> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_STORE_H_
