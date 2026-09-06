// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TINT_BACKGROUND_H_
#define ARCIUM_UI_SIDEBAR_TINT_BACKGROUND_H_

#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"

namespace arcium {

// Two-stop linear gradient from kColorArciumSidebarBackgroundTop at the top
// leading corner to kColorArciumSidebarBackgroundBottom at the bottom. The
// shader is rebuilt only when the size or colours change.
class TintBackground : public views::Background {
 public:
  TintBackground();
  ~TintBackground() override;

  // views::Background:
  void Paint(gfx::Canvas* canvas, views::View* view) const override;
  void OnViewThemeChanged(views::View* view) override;

 private:
  mutable gfx::Size cached_size_;
  mutable SkColor cached_top_ = 0;
  mutable SkColor cached_bottom_ = 0;
  mutable sk_sp<cc::PaintShader> shader_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TINT_BACKGROUND_H_
