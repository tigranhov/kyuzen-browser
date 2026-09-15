// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PEEK_VIEW_H_
#define ARCIUM_UI_BROWSER_PEEK_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}

namespace views {
class ImageButton;
class WebView;
}  // namespace views

namespace arcium {

// What a peek draws: a scrim over the page area, a card holding the page, and
// beside the card the two buttons Zen's Glance puts there -- close, and open
// as a tab. Owns no page; the controller hands it one and takes it back.
class PeekView : public views::View {
  METADATA_HEADER(PeekView, views::View)

 public:
  struct Actions {
    base::RepeatingClosure close;
    base::RepeatingClosure open_as_tab;
  };

  explicit PeekView(Actions actions);
  PeekView(const PeekView&) = delete;
  PeekView& operator=(const PeekView&) = delete;
  ~PeekView() override;

  // Shows `page` in the card, or detaches whatever it showed for null.
  void SetPage(content::WebContents* page);
  void FocusPage();

  // The card, in this view's coordinates: the page area inset by a twentieth
  // on each side, less the column the buttons sit in.
  gfx::Rect CardBounds() const;

  views::ImageButton* close_button_for_testing() { return close_button_; }
  views::ImageButton* open_button_for_testing() { return open_button_; }

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

 private:
  // Every action runs on its own turn: each one removes this view, and a view
  // must not be removed from inside its own event handler.
  void Run(const base::RepeatingClosure& action);

  Actions actions_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::ImageButton> open_button_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PEEK_VIEW_H_
