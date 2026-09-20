// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/peek_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/webview/webview.h"

namespace arcium {

namespace {

constexpr int kButtonSize = 28;
constexpr int kButtonGap = 8;
constexpr int kIconSize = 16;
// Dark enough that the page behind reads as set aside, light enough that the
// reader still sees which page the peek came from.
constexpr SkAlpha kScrimAlpha = 0x66;

std::unique_ptr<views::ImageButton> MakeButton(
    const gfx::VectorIcon& icon,
    const std::u16string& name,
    views::Button::PressedCallback callback) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, ui::kColorIcon, kIconSize));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorBubbleBackground, kButtonSize / 2.f));
  button->SetTooltipText(name);
  button->GetViewAccessibility().SetName(name);
  return button;
}

}  // namespace

PeekView::PeekView(Actions actions) : actions_(std::move(actions)) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(u"Peek");
  web_view_ = AddChildView(std::make_unique<views::WebView>());
  close_button_ = AddChildView(
      MakeButton(vector_icons::kCloseIcon, u"Close peek",
                 base::BindRepeating(&PeekView::Run, base::Unretained(this),
                                     actions_.close)));
  open_button_ = AddChildView(
      MakeButton(vector_icons::kOpenInNewFlippableIcon, u"Open as tab",
                 base::BindRepeating(&PeekView::Run, base::Unretained(this),
                                     actions_.open_as_tab)));
  // A layer of its own, because the page this is drawn over has one: a view
  // without a layer paints into its parent's, which is beneath every layer
  // inside it. Without this the card shows -- a WebView carries its own
  // layer -- and everything painted around it does not, leaving a peek with
  // no dimming and no buttons.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  // Reaches here only when the page leaves Escape unhandled, which is Zen's
  // own condition: a page that uses the key keeps it.
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

PeekView::~PeekView() = default;

void PeekView::SetPage(content::WebContents* page) {
  web_view_->SetWebContents(page);
}

void PeekView::FocusPage() {
  web_view_->RequestFocus();
}

gfx::Rect PeekView::CardBounds() const {
  gfx::Rect card = GetLocalBounds();
  card.Inset(gfx::Insets::VH(card.height() / 20, card.width() / 20));
  card.set_width(std::max(0, card.width() - kButtonSize - kButtonGap));
  return card;
}

void PeekView::Layout(PassKey) {
  const gfx::Rect card = CardBounds();
  web_view_->SetBoundsRect(card);
  const int x = card.right() + kButtonGap;
  close_button_->SetBounds(x, card.y(), kButtonSize, kButtonSize);
  open_button_->SetBounds(x, card.y() + kButtonSize + kButtonGap, kButtonSize,
                          kButtonSize);
}

void PeekView::OnPaint(gfx::Canvas* canvas) {
  canvas->FillRect(GetLocalBounds(), SkColorSetA(SK_ColorBLACK, kScrimAlpha));
}

bool PeekView::OnMousePressed(const ui::MouseEvent& event) {
  // The card and the buttons take their own presses; anything that reaches
  // here landed on the scrim.
  if (!CardBounds().Contains(event.location())) {
    Run(actions_.close);
  }
  return true;
}

bool PeekView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() != ui::VKEY_ESCAPE) {
    return false;
  }
  Run(actions_.close);
  return true;
}

void PeekView::Run(const base::RepeatingClosure& action) {
  if (action) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, action);
  }
}

BEGIN_METADATA(PeekView)
END_METADATA

}  // namespace arcium
