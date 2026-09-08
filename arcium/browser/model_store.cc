// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model_store.h"

#include <optional>
#include <string>
#include <utility>

#include "arcium/browser/model/model_migration.h"
#include "arcium/browser/model/model_serializer.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/thread_pool.h"

namespace arcium {

namespace {

// The file could not be understood. Moving it aside is what makes "degrades
// to an empty model" true rather than a euphemism for deleting it: the next
// mutation schedules a save, and the save would otherwise land on top of the
// only copy of whatever the file held. A single sidecar, overwritten each
// time, because the useful one is always the most recent.
void MoveUnreadableFileAside(const base::FilePath& path) {
  base::Move(path, path.AddExtension(FILE_PATH_LITERAL("unreadable")));
}

// Runs on the background sequence, which is where the migration has to run:
// the promise is that no schema work ever happens on the UI thread, and
// OnLoaded is the UI thread. `dict` is nullopt for a missing, unreadable,
// unparseable or unmigratable file; all four mean "start empty", never
// "crash", and the last two also mean "keep the bytes".
ModelStore::LoadResult ReadFileOnBackgroundSequence(
    const base::FilePath& path) {
  ModelStore::LoadResult result;
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return result;
  }
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
  if (!dict) {
    MoveUnreadableFileAside(path);
    return result;
  }
  result.dict = MigrateModelDict(std::move(*dict));
  if (!result.dict) {
    MoveUnreadableFileAside(path);
  }
  return result;
}

}  // namespace

ModelStore::ModelStore(ArciumModel* model, const base::FilePath& path)
    : model_(model),
      background_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          // USER_VISIBLE, not BookmarkStorage's BEST_EFFORT: the sidebar
          // reads this same model to paint pinned/favorite rows on the next
          // launch, so the write and the eventual read both sit on the path
          // to a visible frame, not purely in the background.
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      writer_(path, background_runner_, kSaveDelay) {
  model_->AddObserver(this);
}

ModelStore::~ModelStore() {
  model_->RemoveObserver(this);
  // A pending save must not be lost at shutdown: BLOCK_SHUTDOWN on the runner
  // plus this flush is what makes a quit right after a drag durable.
  SaveNowIfScheduled();
}

void ModelStore::Load(base::OnceClosure done) {
  background_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadFileOnBackgroundSequence, writer_.path()),
      base::BindOnce(&ModelStore::OnLoaded, weak_factory_.GetWeakPtr(),
                     std::move(done)));
}

void ModelStore::SaveNowForTesting() {
  SaveNowIfScheduled();
}

void ModelStore::SaveNowIfScheduled() {
  if (writer_.HasPendingWrite()) {
    writer_.DoScheduledWrite();
  }
}

void ModelStore::OnLoaded(base::OnceClosure done, LoadResult result) {
  // Suppress OnArciumModelChanged() for the duration of ReplaceAll()'s
  // notification: reading a file must not dirty the model and schedule a
  // rewrite of the bytes just read.
  loading_ = true;
  if (result.dict) {
    // A false return means the file is unusable as a whole. The model is
    // left as constructed — empty and valid — rather than partly filled.
    DeserializeModel(*result.dict, model_);
  }
  loading_ = false;
  std::move(done).Run();
}

void ModelStore::OnArciumModelChanged() {
  if (loading_) {
    return;
  }
  // One scheduled save per burst: the counter only moves when there was no
  // pending write, which is what ABurstOfMutationsWritesOnce asserts.
  if (!writer_.HasPendingWrite()) {
    ++scheduled_saves_;
  }
  writer_.ScheduleWriteWithBackgroundDataSerializer(this);
}

// ImportantFileWriter::BackgroundDataSerializer. Called when the debounce
// timer fires, NOT on every mutation. Snapshots the model on this sequence
// and serialises on the background sequence, so the UI thread never does the
// JSON work.
base::ImportantFileWriter::BackgroundDataProducerCallback
ModelStore::GetSerializedDataProducerForBackgroundSequence() {
  ++initiated_saves_;
  return base::BindOnce(
      [](base::DictValue snapshot) -> std::optional<std::string> {
        return base::WriteJson(snapshot);
      },
      SerializeModel(*model_));
}

}  // namespace arcium
