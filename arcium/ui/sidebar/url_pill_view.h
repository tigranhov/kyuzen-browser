// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
#define ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace arcium {

// Row 2: a rounded pill. In the browser it hosts Chromium's LocationBarView
// (Task 10). In the playground, or as the fallback, it shows text and calls
// `on_click`.
class UrlPillView : public views::View {
  METADATA_HEADER(UrlPillView, views::View)

 public:
  explicit UrlPillView(base::RepeatingClosure on_click);
  UrlPillView(const UrlPillView&) = delete;
  UrlPillView& operator=(const UrlPillView&) = delete;
  ~UrlPillView() override;

  void SetPlaceholderText(const std::u16string& text);
  // Replaces the placeholder with `view`, which fills the pill.
  views::View* SetHostedView(std::unique_ptr<views::View> view);
  bool has_hosted_view() const { return hosted_ != nullptr; }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  base::RepeatingClosure on_click_;
  raw_ptr<views::Label> placeholder_ = nullptr;
  raw_ptr<views::View> hosted_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
