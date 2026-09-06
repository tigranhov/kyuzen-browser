// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_NAV_ROW_VIEW_H_
#define ARCIUM_UI_SIDEBAR_NAV_ROW_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace gfx {
struct VectorIcon;
}

namespace views {
class ImageButton;
}

namespace arcium {

// Row 1 of the sidebar: [caption inset][sidebar toggle] ... [back][fwd][reload]
class NavRowView : public views::View {
  METADATA_HEADER(NavRowView, views::View)

 public:
  struct Delegate {
    base::RepeatingClosure toggle_sidebar;
    base::RepeatingClosure back;
    base::RepeatingClosure forward;
    base::RepeatingClosure reload;
  };

  explicit NavRowView(Delegate delegate);
  NavRowView(const NavRowView&) = delete;
  NavRowView& operator=(const NavRowView&) = delete;
  ~NavRowView() override;

  void SetLeadingInset(int inset);
  void SetBackEnabled(bool enabled);
  void SetForwardEnabled(bool enabled);

  // True if `point` (in this view's coordinates) is not over a button.
  bool IsPointOnBackground(const gfx::Point& point) const;

 private:
  views::ImageButton* AddButton(base::RepeatingClosure callback,
                                const gfx::VectorIcon& icon,
                                const std::u16string& tooltip);

  Delegate delegate_;
  raw_ptr<views::View> leading_spacer_ = nullptr;
  raw_ptr<views::ImageButton> toggle_ = nullptr;
  raw_ptr<views::View> flex_spacer_ = nullptr;
  raw_ptr<views::ImageButton> back_ = nullptr;
  raw_ptr<views::ImageButton> forward_ = nullptr;
  raw_ptr<views::ImageButton> reload_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_NAV_ROW_VIEW_H_
