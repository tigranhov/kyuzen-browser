// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/view_snapshot.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "cc/paint/display_item_list.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/compositor/paint_context.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
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

}  // namespace

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

  gfx::Rect crop = view->ConvertRectToWidget(view->GetLocalBounds());
  crop = gfx::ScaleToEnclosingRect(crop, scale);
  SkBitmap bitmap;
  if (!full.extractSubset(&bitmap, SkIRect::MakeXYWH(crop.x(), crop.y(),
                                                    crop.width(),
                                                    crop.height()))) {
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
