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

  // Whether Load() has applied the file, whatever it held. Until then the
  // model is a placeholder with one space, and nothing may judge a tab by it.
  bool load_finished() const { return load_finished_; }
  // Whether Load() found no file, or a file it understood. False for a file
  // it had to move aside: the model is then empty, not the user's, and
  // anything that deletes what the model does not name must not run.
  bool load_succeeded() const { return load_succeeded_; }

  // If there is a pending write, performs it immediately. For tests only:
  // production code relies on the debounced schedule, not a forced flush.
  void SaveNowForTesting();

  int scheduled_save_count_for_testing() const { return scheduled_saves_; }
  int initiated_save_count_for_testing() const { return initiated_saves_; }
  bool saves_suppressed_for_testing() const { return saves_suppressed_; }

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
    // False only when the file was unusable AND could not be moved aside, so
    // its bytes are still at the store's own path. See OnLoaded.
    bool bytes_preserved = true;
    // True when there was no file at all, which a first launch looks like.
    // Nothing is lost by treating that model as the user's.
    bool file_absent = false;
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
  int scheduled_saves_ = 0;
  int initiated_saves_ = 0;
  // Suppresses OnArciumModelChanged() while Load() is applying a file to the
  // model: ArciumModel::ReplaceAll()'s notification must not be mistaken for
  // a user mutation, or every successful load would schedule a save of the
  // bytes it just read.
  bool loading_ = false;
  // Set when Load() found a file it could neither use nor preserve. Writing
  // would destroy it, so this store writes nothing for the rest of its life.
  // This assumes no mutation reaches the model before Load() finishes: an
  // earlier mutation would already have armed ImportantFileWriter's timer,
  // and setting this afterwards does not disarm it. That holds today because
  // Load() runs synchronously right after construction and every mutator is
  // a user command. Whoever adds one that runs during startup breaks it.
  bool saves_suppressed_ = false;
  bool load_finished_ = false;
  bool load_succeeded_ = false;
  base::WeakPtrFactory<ModelStore> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_STORE_H_
