// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
#define ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_

#include <memory>
#include <string>

#include "base/auto_reset.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

class GURL;

namespace views {
class ImageButton;
class Label;
}  // namespace views

namespace arcium {

// Row 2: the address pill. It shows where you are and nothing else until you
// reach for it, and it hosts Chromium's real location bar behind its own text
// -- not to draw it, but because Chrome's password bubbles, page information
// and permission chips all reach for that view and there is nowhere else in
// this window for them to point.
class UrlPillView : public views::View, public views::FocusChangeListener {
  METADATA_HEADER(UrlPillView, views::View)

 public:
  // What the pill cannot do itself. All four are window-level.
  struct Actions {
    base::RepeatingClosure open_box;
    base::RepeatingClosure open_extensions;
    base::RepeatingClosure copy_link;
    base::RepeatingClosure open_site_info;
  };

  explicit UrlPillView(Actions actions);
  UrlPillView(const UrlPillView&) = delete;
  UrlPillView& operator=(const UrlPillView&) = delete;
  ~UrlPillView() override;

  // The address of the tab on screen.
  void SetUrl(const GURL& url);
  // False draws the site button as a warning and shows it without hovering.
  void SetConnectionSecure(bool secure);
  // True while the hosted bar has something of its own to show -- a permission
  // request. The pill then shows nothing at all, so what the bar is asking is
  // both visible and clickable.
  void SetHostedBarSpeaking(bool speaking);

  // Replaces the placeholder with `view`, which fills the pill behind the
  // pill's own text.
  views::View* SetHostedView(std::unique_ptr<views::View> view);
  bool has_hosted_view() const { return hosted_ != nullptr; }

  const std::u16string& domain_for_testing() const { return domain_; }
  views::ImageButton* site_button_for_testing() { return site_; }
  views::ImageButton* extensions_button_for_testing() { return extensions_; }
  views::ImageButton* copy_button_for_testing() { return copy_; }
  void SetRevealedForTesting(bool revealed);
  static base::AutoReset<bool> DisableRevealAnimationForTesting();

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // views::FocusChangeListener. Keyboard focus reveals the buttons on the same
  // terms as hover, because a control only a pointer can reach is not a
  // control everyone can reach.
  void OnWillChangeFocus(views::View* before, views::View* now) override;
  void OnDidChangeFocus(views::View* before, views::View* now) override;

 private:
  views::ImageButton* AddButton(base::RepeatingClosure action,
                                const std::u16string& tooltip);
  // Recomputes all three buttons from `revealed_`, `secure_` and `speaking_`.
  void UpdateButtons();
  void SetButtonShown(views::ImageButton* button, bool shown);
  void SetRevealed(bool revealed);
  void RefreshIcons();

  Actions actions_;
  raw_ptr<views::Label> text_ = nullptr;
  raw_ptr<views::ImageButton> site_ = nullptr;
  raw_ptr<views::ImageButton> extensions_ = nullptr;
  raw_ptr<views::ImageButton> copy_ = nullptr;
  raw_ptr<views::View> hosted_ = nullptr;
  std::u16string domain_;
  bool secure_ = true;
  bool revealed_ = false;
  bool speaking_ = false;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
