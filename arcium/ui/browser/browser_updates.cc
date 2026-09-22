// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/browser_updates.h"

#include <memory>
#include <utility>

#include "arcium/browser/update/browser_update_status.h"
#include "arcium/browser/update/sparkle_backend.h"
#include "arcium/browser/update/update_controller.h"
#include "base/debug/leak_annotations.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/after_startup_task_utils.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/browser_shutdown.h"

namespace arcium {
namespace {

// Never deleted. Sparkle replaces the application as it quits, which happens
// after every owner the browser could give this has been torn down.
UpdateController* g_controller = nullptr;

}  // namespace

BrowserUpdates::BrowserUpdates() = default;
BrowserUpdates::~BrowserUpdates() = default;

void BrowserUpdates::PostBrowserStart() {
  // After startup rather than now: nothing about updating is needed to draw
  // the first window, and reading the framework is disk work.
  AfterStartupTaskUtils::PostTask(
      FROM_HERE, base::SequencedTaskRunner::GetCurrentDefault(),
      base::BindOnce(&BrowserUpdates::LoadFramework,
                     weak_factory_.GetWeakPtr()));
}

void BrowserUpdates::PostMainMessageLoopRun() {
  if (g_controller) {
    g_controller->Shutdown();
  }
}

void BrowserUpdates::LoadFramework() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&LoadSparkleFramework),
      base::BindOnce(&BrowserUpdates::OnFrameworkLoaded,
                     weak_factory_.GetWeakPtr()));
}

void BrowserUpdates::OnFrameworkLoaded(bool loaded) {
  if (!loaded || browser_shutdown::HasShutdownStarted()) {
    return;
  }
  std::unique_ptr<UpdaterBackend> backend = MakeSparkleBackend();
  if (!backend) {
    return;
  }
  g_controller = new UpdateController(g_browser_process->local_state(),
                                      std::move(backend));
  ANNOTATE_LEAKING_OBJECT_PTR(g_controller);
  SetBrowserUpdateStatus(g_controller);
}

}  // namespace arcium
