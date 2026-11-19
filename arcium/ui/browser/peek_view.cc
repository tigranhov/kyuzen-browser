// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/peek_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "arcium/ui/sidebar/pill_label.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "url/gurl.h"

namespace arcium {

namespace {

constexpr int kButtonSize = 32;
constexpr int kButtonGap = 8;
constexpr int kIconSize = 16;
// The floating radius every raised surface shares.
constexpr float kCardRadius = 12;
// The caption: a small capsule inset from the card's corner.
constexpr int kCaptionHeight = 24;
constexpr int kCaptionInset = 8;
constexpr int kCaptionPadding = 8;
constexpr int kCaptionIconSize = 14;
constexpr int kCaptionMaxWidth = 320;

// The one floating shadow: a wide soft fall and a tight contact shadow.
gfx::ShadowValues FloatingShadow() {
  return {gfx::ShadowValue(gfx::Vector2d(0, 24), 64,
                           SkColorSetA(SK_ColorBLACK, 0x73)),
          gfx::ShadowValue(gfx::Vector2d(0, 4), 16,
                           SkColorSetA(SK_ColorBLACK, 0x47))};
}
// Dark enough that the page behind reads as set aside, light enough that the
// reader still sees which page the peek came from.
constexpr SkAlpha kScrimAlpha = 0x66;

std::unique_ptr<views::ImageButton> MakeButton(
    const gfx::VectorIcon& icon,
    const std::u16string& name,
    views::Button::PressedCallback callback) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(
                            icon, kColorArciumRowTextActive, kIconSize));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  views::InstallCircleHighlightPathGenerator(button.get());
  button->SetTooltipText(name);
  button->GetViewAccessibility().SetName(name);
  return button;
}

}  // namespace

PeekView::PeekView(Actions actions) : actions_(std::move(actions)) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(u"Peek");
  web_view_ = AddChildView(std::make_unique<views::WebView>());

  // Close and Open as tab share one capsule on the raised surface, where the
  // two circles sat.
  auto capsule = std::make_unique<views::View>();
  capsule->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  capsule->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumSurfaceRaised, kButtonSize / 2.f));
  capsule_ = AddChildView(std::move(capsule));
  close_button_ = capsule_->AddChildView(
      MakeButton(vector_icons::kCloseIcon, u"Close peek",
                 base::BindRepeating(&PeekView::Run, base::Unretained(this),
                                     actions_.close)));
  open_button_ = capsule_->AddChildView(
      MakeButton(vector_icons::kOpenInNewFlippableIcon, u"Open as tab",
                 base::BindRepeating(&PeekView::Run, base::Unretained(this),
                                     actions_.open_as_tab)));

  // The caption sits over the page, so it needs a layer above the page's:
  // painted into this view's own layer it would be under the card.
  auto caption = std::make_unique<views::View>();
  auto* caption_layout =
      caption->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kCaptionPadding), 6));
  caption_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  caption->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumSurfaceRaised, kCaptionHeight / 2.f));
  caption->SetCanProcessEventsWithinSubtree(false);
  caption->SetPaintToLayer();
  caption->layer()->SetFillsBoundsOpaquely(false);
  caption_icon_ = caption->AddChildView(std::make_unique<views::ImageView>());
  caption_icon_->SetImageSize(gfx::Size(kCaptionIconSize, kCaptionIconSize));
  caption_text_ = caption->AddChildView(std::make_unique<views::Label>());
  caption_text_->SetEnabledColor(kColorArciumRowText);
  caption_text_->SetFontList(
      views::Label::GetDefaultFontList().DeriveWithSizeDelta(-1));
  caption_text_->SetElideBehavior(gfx::ELIDE_HEAD);
  caption_ = AddChildView(std::move(caption));
  caption_->SetVisible(false);
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
  // The page is clipped to the card's radius by the compositor, the same
  // clip the window already uses for its page corners.
  web_view_->holder()->SetNativeViewCornerRadii(
      gfx::RoundedCornersF(kCardRadius));
  if (!page) {
    caption_->SetVisible(false);
  }
}

void PeekView::SetCaption(const GURL& url, const ui::ImageModel& favicon) {
  caption_text_->SetText(PillLabel(url));
  caption_icon_->SetImage(favicon.IsEmpty() ? ui::ImageModel::FromVectorIcon(
                                                  vector_icons::kGlobeIcon,
                                                  kColorArciumRowTextSecondary,
                                                  kCaptionIconSize)
                                            : favicon);
  caption_->SetVisible(true);
  InvalidateLayout();
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
  capsule_->SetBounds(card.right() + kButtonGap, card.y(), kButtonSize,
                      kButtonSize * 2);
  const int caption_width =
      std::min({caption_->GetPreferredSize().width(), kCaptionMaxWidth,
                std::max(0, card.width() - 2 * kCaptionInset)});
  caption_->SetBounds(card.x() + kCaptionInset, card.y() + kCaptionInset,
                      caption_width, kCaptionHeight);
}

void PeekView::OnPaint(gfx::Canvas* canvas) {
  canvas->FillRect(GetLocalBounds(), SkColorSetA(SK_ColorBLACK, kScrimAlpha));
  const gfx::RectF card(CardBounds());
  // The shadow, painted under where the page will sit; the page covers its
  // inside, so only the fall around the card shows.
  cc::PaintFlags shadow;
  shadow.setAntiAlias(true);
  shadow.setColor(SK_ColorBLACK);
  shadow.setLooper(gfx::CreateShadowDrawLooper(FloatingShadow()));
  canvas->DrawRoundRect(card, kCardRadius, shadow);
  // A hairline just outside the page's edge, so a white page on a light
  // page still has an edge.
  cc::PaintFlags edge;
  edge.setAntiAlias(true);
  edge.setStyle(cc::PaintFlags::kStroke_Style);
  edge.setStrokeWidth(1);
  edge.setColor(GetColorProvider()->GetColor(kColorArciumDivider));
  gfx::RectF ring = card;
  ring.Outset(0.5f);
  canvas->DrawRoundRect(ring, kCardRadius + 0.5f, edge);
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
