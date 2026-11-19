// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/view_snapshot.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "cc/paint/display_item_list.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/paint_context.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

void LogViewHierarchy(const views::View* view, int depth) {
  LOG(ERROR) << std::string(depth * 2, ' ') << view->GetClassName() << " "
             << view->bounds().ToString()
             << (view->GetVisible() ? "" : " hidden");
  for (const views::View* child : view->children()) {
    LogViewHierarchy(child, depth + 1);
  }
}

bool WritePng(const base::FilePath& path, std::vector<uint8_t> png) {
  const bool ok = base::WriteFile(path, png);
  LOG(ERROR) << "snapshot: " << (ok ? "wrote " : "could not write ") << path;
  return ok;
}

// A root paint stops at every view that draws on a layer of its own. Paints
// each such view the way its layer would, in tree order and at its place in
// the widget, so a bubble's contents or a scrolling list are not left blank.
// Clips, transforms and opacity on those layers are not reproduced.
void PaintLayeredDescendants(const views::View* parent,
                             float scale,
                             SkCanvas* canvas) {
  for (views::View* child : parent->children()) {
    if (!child->GetVisible()) {
      continue;
    }
    if (child->layer() && !child->size().IsEmpty()) {
      auto list = base::MakeRefCounted<cc::DisplayItemList>();
      ui::PaintContext context(list.get(), scale, gfx::Rect(child->size()),
                               /*is_pixel_canvas=*/false);
      child->Paint(
          views::PaintInfo::CreateRootPaintInfo(context, child->size()));
      list->Finalize();
      const gfx::Rect at = child->ConvertRectToWidget(child->GetLocalBounds());
      canvas->save();
      canvas->translate(at.x(), at.y());
      list->Raster(canvas, cc::PlaybackParams(nullptr));
      canvas->restore();
    }
    PaintLayeredDescendants(child, scale, canvas);
  }
}

// Encodes the part of `full` under `crop` and writes it on the thread pool.
void WriteCrop(const SkBitmap& full,
               const gfx::Rect& crop,
               const base::FilePath& path,
               base::OnceCallback<void(bool)> done) {
  SkBitmap bitmap;
  if (!full.extractSubset(
          &bitmap,
          SkIRect::MakeXYWH(crop.x(), crop.y(), crop.width(), crop.height()))) {
    LOG(ERROR) << "snapshot: crop " << crop.ToString() << " outside frame";
    std::move(done).Run(false);
    return;
  }
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/true);
  if (!png) {
    LOG(ERROR) << "snapshot: PNG encode failed";
    std::move(done).Run(false);
    return;
  }
  LOG(ERROR) << "snapshot: composited " << crop.width() << "x" << crop.height()
             << " px";
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&WritePng, path, std::move(*png)), std::move(done));
}

// One snapshot, answered once: by the frame if it comes, otherwise by the
// paint fallback.
struct PendingSnapshot : base::RefCounted<PendingSnapshot> {
  PendingSnapshot(views::View* view,
                  base::FilePath path,
                  base::OnceCallback<void(bool)> done)
      : view(view), path(std::move(path)), done(std::move(done)) {}

  // Forgets the view if it goes before the frame does.
  views::ViewTracker view;
  base::FilePath path;
  base::OnceCallback<void(bool)> done;

 private:
  friend class base::RefCounted<PendingSnapshot>;
  ~PendingSnapshot() = default;
};

void OnFrame(scoped_refptr<PendingSnapshot> pending,
             std::unique_ptr<viz::CopyOutputResult> result) {
  views::View* view = pending->view.view();
  if (!pending->done || !view || !view->GetWidget() || result->IsEmpty()) {
    return;
  }
  auto access = result->ScopedAccessSkBitmap();
  const SkBitmap full = access.GetOutScopedBitmap();
  const float scale = static_cast<float>(full.width()) /
                      view->GetWidget()->GetRootView()->width();
  WriteCrop(full,
            gfx::ScaleToEnclosingRect(
                view->ConvertRectToWidget(view->GetLocalBounds()), scale),
            pending->path, std::move(pending->done));
}

void PaintInstead(scoped_refptr<PendingSnapshot> pending) {
  if (!pending->done || !pending->view.view()) {
    return;
  }
  LOG(ERROR) << "snapshot: no frame from the compositor, painting the views";
  WriteViewSnapshot(pending->view.view(), pending->path, /*scale=*/2.f,
                    std::move(pending->done));
}

}  // namespace

void WriteCompositedViewSnapshot(views::View* view,
                                 const base::FilePath& path,
                                 base::OnceCallback<void(bool)> done) {
  views::Widget* widget = view->GetWidget();
  ui::Layer* layer = widget ? widget->GetLayer() : nullptr;
  auto pending =
      base::MakeRefCounted<PendingSnapshot>(view, path, std::move(done));
  if (!layer) {
    PaintInstead(pending);
    return;
  }
  auto request = std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      base::BindOnce(&OnFrame, pending));
  request->set_result_task_runner(
      base::SequencedTaskRunner::GetCurrentDefault());
  layer->RequestCopyOfOutput(std::move(request));
  layer->ScheduleDraw();
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&PaintInstead, pending), base::Seconds(3));
}

void WriteViewSnapshot(views::View* view,
                       const base::FilePath& path,
                       float scale,
                       base::OnceCallback<void(bool)> done) {
  // A root paint must start at the layer origin, so paint the widget's root
  // view and crop to `view` afterwards.
  views::Widget* widget = view->GetWidget();
  if (!widget) {
    LOG(ERROR) << "snapshot: view has no widget";
    std::move(done).Run(false);
    return;
  }
  widget->LayoutRootViewIfNecessary();
  views::View* root = widget->GetRootView();
  const gfx::Size size = root->size();
  auto list = base::MakeRefCounted<cc::DisplayItemList>();
  ui::PaintContext context(list.get(), scale, gfx::Rect(size),
                           /*is_pixel_canvas=*/false);
  root->Paint(views::PaintInfo::CreateRootPaintInfo(context, size));
  list->Finalize();

  SkBitmap full;
  full.allocN32Pixels(size.width() * scale, size.height() * scale);
  full.eraseColor(SK_ColorBLACK);
  SkCanvas canvas(full);
  canvas.scale(scale, scale);
  list->Raster(&canvas, cc::PlaybackParams(nullptr));
  PaintLayeredDescendants(root, scale, &canvas);

  gfx::Rect crop = view->ConvertRectToWidget(view->GetLocalBounds());
  crop = gfx::ScaleToEnclosingRect(crop, scale);
  SkBitmap bitmap;
  if (!full.extractSubset(
          &bitmap,
          SkIRect::MakeXYWH(crop.x(), crop.y(), crop.width(), crop.height()))) {
    LOG(ERROR) << "snapshot: crop " << crop.ToString() << " outside "
               << size.ToString();
    std::move(done).Run(false);
    return;
  }

  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/true);
  if (!png) {
    LOG(ERROR) << "snapshot: PNG encode failed";
    std::move(done).Run(false);
    return;
  }
  LOG(ERROR) << "snapshot: " << crop.width() << "x" << crop.height() << " px";
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&WritePng, path, std::move(*png)), std::move(done));
}

void LogViewHierarchy(const views::View* view) {
  LogViewHierarchy(view, 0);
}

}  // namespace arcium
