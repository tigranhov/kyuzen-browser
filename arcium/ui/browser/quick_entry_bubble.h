// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_QUICK_ENTRY_BUBBLE_H_
#define ARCIUM_UI_BROWSER_QUICK_ENTRY_BUBBLE_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class BrowserView;

namespace views {
class Textfield;
}

namespace arcium {

// Cmd+T: a floating text field over the page. Stage 1 has no suggestions.
// A BubbleDialogDelegate (not a View): the owner creates the Widget with
// views::BubbleDialogDelegate::CreateBubble and keeps this delegate alive
// for as long as the Widget exists.
class QuickEntryBubble : public views::BubbleDialogDelegate,
                         public views::TextfieldController {
 public:
  using SubmitCallback = base::OnceCallback<void(const std::u16string&)>;

  QuickEntryBubble(BrowserView* browser_view, SubmitCallback on_submit);
  QuickEntryBubble(const QuickEntryBubble&) = delete;
  QuickEntryBubble& operator=(const QuickEntryBubble&) = delete;
  ~QuickEntryBubble() override;

  // Call once the Widget is shown.
  void FocusField();

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::BubbleDialogDelegate:
  gfx::Rect GetAnchorRect() const override;

 private:
  raw_ptr<BrowserView> browser_view_;
  SubmitCallback on_submit_;
  raw_ptr<views::Textfield> field_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_QUICK_ENTRY_BUBBLE_H_
