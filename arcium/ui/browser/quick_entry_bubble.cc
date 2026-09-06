// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/quick_entry_bubble.h"

#include <memory>
#include <utility>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {
constexpr int kWidth = 560;
constexpr int kFieldHeight = 40;
}  // namespace

QuickEntryBubble::QuickEntryBubble(BrowserView* browser_view,
                                   SubmitCallback on_submit)
    : views::BubbleDialogDelegate(/*anchor_view=*/nullptr,
                                  views::BubbleBorder::Arrow::NONE),
      browser_view_(browser_view),
      on_submit_(std::move(on_submit)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetShowCloseButton(false);
  set_margins(gfx::Insets(12));
  set_close_on_deactivate(true);
  set_parent_window(browser_view_->GetWidget()->GetNativeView());

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::FillLayout>());
  auto field = std::make_unique<views::Textfield>();
  field->set_controller(this);
  field->SetPlaceholderText(u"Search or enter address");
  field->SetPreferredSize(gfx::Size(kWidth, kFieldHeight));
  field_ = contents->AddChildView(std::move(field));
  SetContentsView(std::move(contents));
}

QuickEntryBubble::~QuickEntryBubble() = default;

void QuickEntryBubble::FocusField() {
  field_->RequestFocus();
}

bool QuickEntryBubble::HandleKeyEvent(views::Textfield* sender,
                                      const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    const std::u16string text(field_->GetText());
    GetWidget()->Close();
    if (!text.empty() && on_submit_) {
      std::move(on_submit_).Run(text);
    }
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    GetWidget()->Close();
    return true;
  }
  return false;
}

gfx::Rect QuickEntryBubble::GetAnchorRect() const {
  // Centre horizontally over the contents area, a fifth of the way down.
  ContentsContainerView* container =
      browser_view_->GetActiveContentsContainerView();
  const gfx::Rect contents = container ? container->GetBoundsInScreen()
                                       : browser_view_->GetBoundsInScreen();
  const int x = contents.x() + (contents.width() - kWidth) / 2;
  const int y = contents.y() + contents.height() / 5;
  return gfx::Rect(x, y, kWidth, 1);
}

}  // namespace arcium
