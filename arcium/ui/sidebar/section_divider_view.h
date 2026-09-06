// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
class Separator;
}  // namespace views

namespace arcium {

// Hairline between Pinned and Today. Hovering reveals "Clear", which closes
// every Today tab.
class SectionDividerView : public views::View {
  METADATA_HEADER(SectionDividerView, views::View)

 public:
  explicit SectionDividerView(base::RepeatingClosure on_clear);
  SectionDividerView(const SectionDividerView&) = delete;
  SectionDividerView& operator=(const SectionDividerView&) = delete;
  ~SectionDividerView() override;

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  raw_ptr<views::Separator> line_ = nullptr;
  raw_ptr<views::LabelButton> clear_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_
