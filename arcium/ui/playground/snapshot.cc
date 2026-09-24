// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/snapshot.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/view_snapshot.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

constexpr float kScale = 2.f;
constexpr gfx::Size kSnapshotWindowSize(1100, 720);
// How long the compositor has to hand over a frame before the snapshot falls
// back to painting the views, which it must when the window is not drawn.
constexpr base::TimeDelta kCompositedTimeout = base::Seconds(5);

// Set once a PNG is on its way, so the fallback and a late frame cannot both
// write one. The playground takes one snapshot and exits.
bool g_writing = false;

gfx::Size WindowSize() {
  const std::string value =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          kSnapshotSizeSwitch);
  const std::vector<std::string> parts = base::SplitString(
      value, "x", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  int width = 0;
  int height = 0;
  if (parts.size() == 2 && base::StringToInt(parts[0], &width) &&
      base::StringToInt(parts[1], &height) && width > 0 && height > 0) {
    return gfx::Size(width, height);
  }
  return kSnapshotWindowSize;
}

void Exit(bool ok) {
  std::exit(ok ? 0 : 1);
}

bool WritePng(const base::FilePath& path, std::vector<uint8_t> png) {
  const bool ok = base::WriteFile(path, png);
  LOG(ERROR) << "snapshot: " << (ok ? "wrote " : "could not write ") << path;
  return ok;
}

void PaintViews(views::View* view, base::FilePath path) {
  if (g_writing) {
    return;
  }
  g_writing = true;
  LOG(ERROR) << "snapshot: no frame from the compositor, painting the views";
  WriteViewSnapshot(view, path, kScale, base::BindOnce(&Exit));
}

// Writes the part of the compositor's frame that covers `view`. The frame
// holds everything drawn on a layer of its own, which painting the views
// leaves out: a scrolling list, a text button, a peek.
void OnFrame(views::View* view,
             base::FilePath path,
             std::unique_ptr<viz::CopyOutputResult> result) {
  if (g_writing || result->IsEmpty()) {
    return;
  }
  auto access = result->ScopedAccessSkBitmap();
  const SkBitmap full = access.GetOutScopedBitmap();
  const float scale = static_cast<float>(full.width()) /
                      view->GetWidget()->GetRootView()->width();
  const gfx::Rect crop = gfx::ScaleToEnclosingRect(
      view->ConvertRectToWidget(view->GetLocalBounds()), scale);
  SkBitmap bitmap;
  if (!full.extractSubset(
          &bitmap,
          SkIRect::MakeXYWH(crop.x(), crop.y(), crop.width(), crop.height()))) {
    return;
  }
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/true);
  if (!png) {
    return;
  }
  g_writing = true;
  LOG(ERROR) << "snapshot: composited " << crop.width() << "x" << crop.height()
             << " px";
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&WritePng, path, std::move(*png)), base::BindOnce(&Exit));
}

void WriteSnapshot(views::View* view, base::FilePath path) {
  // The examples window opens small; give the sidebar room for every section.
  views::Widget* widget = view->GetWidget();
  if (widget) {
    widget->SetSize(WindowSize());
    widget->LayoutRootViewIfNecessary();
  }
  LogViewHierarchy(view);
  ui::Layer* layer = widget ? widget->GetLayer() : nullptr;
  if (!layer) {
    PaintViews(view, path);
    return;
  }
  auto request = std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      base::BindOnce(&OnFrame, view, path));
  request->set_result_task_runner(
      base::SequencedTaskRunner::GetCurrentDefault());
  layer->RequestCopyOfOutput(std::move(request));
  layer->ScheduleDraw();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&PaintViews, view, path), kCompositedTimeout);
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
