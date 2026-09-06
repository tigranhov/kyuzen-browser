// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/snapshot.h"

#include <cstdlib>

#include "arcium/ui/sidebar/view_snapshot.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

constexpr float kScale = 2.f;
constexpr gfx::Size kSnapshotWindowSize(1100, 720);

void WriteSnapshot(views::View* view, base::FilePath path) {
  // The examples window opens small; give the sidebar room for every section.
  if (views::Widget* widget = view->GetWidget()) {
    widget->SetSize(kSnapshotWindowSize);
    widget->LayoutRootViewIfNecessary();
  }
  LogViewHierarchy(view);
  WriteViewSnapshot(view, path, kScale,
                    base::BindOnce([](bool ok) { std::exit(ok ? 0 : 1); }));
}

}  // namespace

void MaybeScheduleSnapshot(views::View* view) {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kSnapshotSwitch)) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&WriteSnapshot, view,
                     command_line->GetSwitchValuePath(kSnapshotSwitch)),
      base::Seconds(2));
}

}  // namespace arcium
