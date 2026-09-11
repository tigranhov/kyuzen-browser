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

// Two-stop linear gradient from a top colour at the top leading corner to a
// bottom colour at the bottom. The pair is the chosen preset's from
// space_gradients.h, or for preset 0 kColorArciumSidebarBackgroundTop and
// kColorArciumSidebarBackgroundBottom. The shader is rebuilt only when the
// size or colours change.
class TintBackground : public views::Background {
 public:
  struct Stops {
    SkColor top = SK_ColorTRANSPARENT;
    SkColor bottom = SK_ColorTRANSPARENT;
  };

  TintBackground();
  ~TintBackground() override;

  // Chooses the preset painted. An index the palette does not have is preset
  // 0, so a space stored by a build with more presets still draws a sidebar
  // instead of reading past the table.
  void SetPreset(int preset);
  int preset() const { return preset_; }

  // What Paint draws for `view`: the preset's pair for the view's colour
  // mode, or the colour mixer's pair for preset 0.
  Stops StopsFor(const views::View& view) const;

  // views::Background:
  void Paint(gfx::Canvas* canvas, views::View* view) const override;
  void OnViewThemeChanged(views::View* view) override;

 private:
  int preset_ = 0;
  mutable gfx::Size cached_size_;
  mutable SkColor cached_top_ = 0;
  mutable SkColor cached_bottom_ = 0;
  mutable sk_sp<cc::PaintShader> shader_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TINT_BACKGROUND_H_
