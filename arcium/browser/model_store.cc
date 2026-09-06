// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model_store.h"

#include <optional>
#include <string>
#include <utility>

#include "arcium/browser/model/model_serializer.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/thread_pool.h"

namespace arcium {

namespace {

// Runs on the background sequence. `dict` is nullopt for a missing,
// unreadable or unparseable file; all three mean "start empty", never
// "crash". `last_modified` is null when the file does not exist; it is read
// alongside the contents so Load() never has to touch the filesystem again
// on the UI thread.
ModelStore::LoadResult ReadFileOnBackgroundSequence(
    const base::FilePath& path) {
  ModelStore::LoadResult result;
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return result;
  }
  base::File::Info info;
  if (base::GetFileInfo(path, &info)) {
    result.last_modified = info.last_modified;
  }
  result.dict = base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
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
  // Null when the file did not exist; leaving last_save_time_ null in that
  // case is correct — there is nothing to use as an idle floor yet.
  if (!result.last_modified.is_null()) {
    last_save_time_ = result.last_modified;
  }
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
  last_save_time_ = base::Time::Now();
  ++initiated_saves_;
  return base::BindOnce(
      [](base::DictValue snapshot) -> std::optional<std::string> {
        return base::WriteJson(snapshot);
      },
      SerializeModel(*model_));
}

}  // namespace arcium
