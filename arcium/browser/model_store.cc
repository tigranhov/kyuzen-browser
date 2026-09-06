// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model_store.h"

#include <optional>
#include <string>
#include <utility>

#include "arcium/browser/model/model_serializer.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/thread_pool.h"

namespace arcium {

namespace {

// Runs on the background sequence. Returns nullopt for a missing, unreadable
// or unparseable file; all three mean "start empty", never "crash".
std::optional<base::DictValue> ReadFileOnBackgroundSequence(
    const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return std::nullopt;
  }
  return base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
}

}  // namespace

ModelStore::ModelStore(ArciumModel* model, const base::FilePath& path)
    : model_(model),
      background_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      writer_(path, background_runner_, kSaveDelay) {
  model_->AddObserver(this);
}

ModelStore::~ModelStore() {
  model_->RemoveObserver(this);
  // A pending save must not be lost at shutdown: BLOCK_SHUTDOWN on the runner
  // plus this flush is what makes a quit right after a drag durable.
  SaveNowForTesting();
}

void ModelStore::Load(base::OnceClosure done) {
  background_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadFileOnBackgroundSequence, writer_.path()),
      base::BindOnce(&ModelStore::OnLoaded, weak_factory_.GetWeakPtr(),
                     std::move(done)));
}

void ModelStore::SaveNowForTesting() {
  if (writer_.HasPendingWrite()) {
    writer_.DoScheduledWrite();
  }
}

void ModelStore::OnLoaded(base::OnceClosure done,
                          std::optional<base::DictValue> dict) {
  if (dict) {
    // A false return means the file is unusable as a whole. The model is left
    // as constructed — empty and valid — rather than partly filled.
    DeserializeModel(*dict, model_);
  }
  std::move(done).Run();
}

void ModelStore::OnArciumModelChanged() {
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
  ++completed_saves_;
  return base::BindOnce(
      [](base::DictValue snapshot) -> std::optional<std::string> {
        return base::WriteJson(snapshot);
      },
      SerializeModel(*model_));
}

}  // namespace arcium
