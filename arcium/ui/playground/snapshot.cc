// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/snapshot.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
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

constexpr float kScale = 2.f;
constexpr gfx::Size kSnapshotWindowSize(1100, 720);

// Logs class, bounds and visibility for `view` and its descendants, so layout
// problems can be read from the log of a --snapshot run.
void DumpHierarchy(const views::View* view, int depth) {
  LOG(ERROR) << std::string(depth * 2, ' ') << view->GetClassName() << " "
             << view->bounds().ToString()
             << (view->GetVisible() ? "" : " hidden");
  for (const views::View* child : view->children()) {
    DumpHierarchy(child, depth + 1);
  }
}

void WriteSnapshot(views::View* view, base::FilePath path) {
  // A root paint must start at the layer origin, so paint the widget's root
  // view and crop to `view` afterwards.
  views::Widget* widget = view->GetWidget();
  CHECK(widget);
  // The examples window opens small; give the sidebar room for every section.
  widget->SetSize(kSnapshotWindowSize);
  widget->LayoutRootViewIfNecessary();
  views::View* root = widget->GetRootView();
  DumpHierarchy(view, 0);
  const gfx::Size size = root->size();
  auto list = base::MakeRefCounted<cc::DisplayItemList>();
  ui::PaintContext context(list.get(), kScale, gfx::Rect(size),
                           /*is_pixel_canvas=*/false);
  root->Paint(views::PaintInfo::CreateRootPaintInfo(context, size));
  list->Finalize();

  SkBitmap full;
  full.allocN32Pixels(size.width() * kScale, size.height() * kScale);
  full.eraseColor(SK_ColorBLACK);
  SkCanvas canvas(full);
  canvas.scale(kScale, kScale);
  list->Raster(&canvas, cc::PlaybackParams(nullptr));

  gfx::Rect crop = view->ConvertRectToWidget(view->GetLocalBounds());
  crop = gfx::ScaleToEnclosingRect(crop, kScale);
  SkBitmap bitmap;
  CHECK(full.extractSubset(&bitmap, SkIRect::MakeXYWH(crop.x(), crop.y(),
                                                     crop.width(),
                                                     crop.height())));

  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/true);
  if (!png || !base::WriteFile(path, *png)) {
    LOG(ERROR) << "snapshot: could not write " << path;
    std::exit(1);
  }
  LOG(ERROR) << "snapshot: wrote " << path << " (" << size.ToString() << " @"
            << kScale << "x)";
  std::exit(0);
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
