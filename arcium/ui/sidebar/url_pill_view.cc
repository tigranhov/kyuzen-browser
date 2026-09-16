// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/url_pill_view.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/pill_label.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "base/functional/bind.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// Off only in tests, where a fade would mean every assertion about what is on
// screen had to wait for an animation to land.
bool g_animate_reveal = true;

constexpr float kPillButtonRadius = 5;

}  // namespace

UrlPillView::UrlPillView(Actions actions) : actions_(std::move(actions)) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  // Clicking anywhere on it opens the box, so it reads as a button and needs
  // that role set before it can carry a name.
  GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  GetViewAccessibility().SetName(u"Address");

  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, metrics::kPillButtonGap + 4))
      .SetDefault(views::kMarginsKey,
                  gfx::Insets::VH(0, metrics::kPillButtonGap));

  site_ = AddButton(actions_.open_site_info, u"Site information");

  auto text = std::make_unique<views::Label>();
  text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  text->SetElideBehavior(gfx::ELIDE_TAIL);
  text->SetEnabledColor(kColorArciumRowTextSecondary);
  text->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  text_ = AddChildView(std::move(text));

  extensions_ = AddButton(actions_.open_extensions, u"Extensions");
  copy_ = AddButton(actions_.copy_link, u"Copy link");
  RefreshIcons();
  UpdateButtons();
}

UrlPillView::~UrlPillView() = default;

void UrlPillView::SetUrl(const GURL& url) {
  label_ = PillLabel(url);
  text_->SetText(label_);
}

void UrlPillView::SetConnectionSecure(bool secure) {
  if (secure == secure_) {
    return;
  }
  secure_ = secure;
  RefreshIcons();
  UpdateButtons();
}

void UrlPillView::SetHostedBarSpeaking(bool speaking) {
  if (speaking == speaking_) {
    return;
  }
  speaking_ = speaking;
  // The pill's own text goes too, not only its buttons: the bar draws its
  // question where this text sits, and two lines of writing in one pill is
  // nobody's idea of a question.
  text_->SetVisible(!speaking_);
  // A question is there to be answered, so the bar takes clicks again for as
  // long as it is asking one. See SetHostedView.
  if (hosted_) {
    hosted_->SetCanProcessEventsWithinSubtree(speaking_);
  }
  UpdateButtons();
}

views::View* UrlPillView::SetHostedView(std::unique_ptr<views::View> view) {
  // Added at the front so the pill's own text and buttons hit-test first: the
  // bar behind it is there to be pointed at by bubbles, not clicked.
  hosted_ = AddChildViewAt(std::move(view), 0);
  hosted_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  // Order alone is not enough. The bar's address field sits under the pill's
  // text, which is a label and takes no events, so a click in the middle of
  // the pill reached the field, focused it and left the box unopened. The bar
  // takes no events at all while it is silent; SetHostedBarSpeaking gives them
  // back when it has a question to ask, which is the one time it is meant to
  // be clicked.
  hosted_->SetCanProcessEventsWithinSubtree(speaking_);
  InvalidateLayout();
  return hosted_;
}

void UrlPillView::SetRevealedForTesting(bool revealed) {
  SetRevealed(revealed);
}

base::AutoReset<bool> UrlPillView::DisableRevealAnimationForTesting() {
  return base::AutoReset<bool>(&g_animate_reveal, false);
}

views::ImageButton* UrlPillView::AddButton(base::RepeatingClosure action,
                                           const std::u16string& tooltip) {
  auto button = std::make_unique<views::ImageButton>(base::BindRepeating(
      [](const base::RepeatingClosure& run) {
        // A delegate with nothing behind it -- the playground's smallest
        // host, a view test -- gets a button that does nothing, not one
        // that crashes.
        if (run) {
          run.Run();
        }
      },
      std::move(action)));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetPreferredSize(
      gfx::Size(metrics::kPillButtonSize, metrics::kPillButtonSize));
  button->SetTooltipText(tooltip);
  button->GetViewAccessibility().SetName(tooltip);
  views::InstallRoundRectHighlightPathGenerator(button.get(), gfx::Insets(),
                                                kPillButtonRadius);
  // The reveal is a fade, so each button needs a layer of its own to fade.
  button->SetPaintToLayer();
  button->layer()->SetFillsBoundsOpaquely(false);
  button->SetVisible(false);
  return AddChildView(std::move(button));
}

void UrlPillView::RefreshIcons() {
  site_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          secure_ ? vector_icons::kLockIcon : vector_icons::kWarningIcon,
          secure_ ? kColorArciumRowTextSecondary : ui::kColorAlertHighSeverity,
          metrics::kPillIconSize));
  extensions_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kExtensionFilledIcon,
                                     kColorArciumRowTextSecondary,
                                     metrics::kPillIconSize));
  copy_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kContentCopyIcon,
                                     kColorArciumRowTextSecondary,
                                     metrics::kPillIconSize));
}

void UrlPillView::UpdateButtons() {
  // A bar with something to say owns the whole pill.
  if (speaking_) {
    SetButtonShown(site_, false);
    SetButtonShown(extensions_, false);
    SetButtonShown(copy_, false);
    return;
  }
  // The warning is the one thing here whose whole value is being seen
  // unasked, so it does not wait for a pointer.
  SetButtonShown(site_, revealed_ || !secure_);
  SetButtonShown(extensions_, revealed_);
  SetButtonShown(copy_, revealed_);
}

void UrlPillView::SetButtonShown(views::ImageButton* button, bool shown) {
  if (button->GetVisible() == shown) {
    return;
  }
  if (!g_animate_reveal) {
    button->SetVisible(shown);
    button->layer()->SetOpacity(shown ? 1.f : 0.f);
    return;
  }
  if (shown) {
    button->SetVisible(true);
    button->layer()->SetOpacity(0.f);
  }
  views::AnimationBuilder()
      .SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
      .OnEnded(base::BindOnce(
          [](base::WeakPtr<views::View> button, bool shown) {
            if (button && !shown) {
              button->SetVisible(false);
            }
          },
          button->GetWeakPtr(), shown))
      .Once()
      .SetDuration(base::Milliseconds(metrics::kPillRevealMs))
      .SetOpacity(button, shown ? 1.f : 0.f, gfx::Tween::LINEAR);
}

void UrlPillView::SetRevealed(bool revealed) {
  if (revealed == revealed_) {
    return;
  }
  revealed_ = revealed;
  UpdateButtons();
}

bool UrlPillView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton()) {
    actions_.open_box.Run();
    return true;
  }
  return views::View::OnMousePressed(event);
}

void UrlPillView::OnMouseEntered(const ui::MouseEvent& event) {
  SetRevealed(true);
}

void UrlPillView::OnMouseExited(const ui::MouseEvent& event) {
  const views::FocusManager* focus = GetFocusManager();
  SetRevealed(focus && focus->GetFocusedView() &&
              Contains(focus->GetFocusedView()));
}

void UrlPillView::AddedToWidget() {
  views::View::AddedToWidget();
  if (views::FocusManager* focus = GetFocusManager()) {
    focus->AddFocusChangeListener(this);
  }
}

void UrlPillView::RemovedFromWidget() {
  if (views::FocusManager* focus = GetFocusManager()) {
    focus->RemoveFocusChangeListener(this);
  }
  views::View::RemovedFromWidget();
}

void UrlPillView::OnWillChangeFocus(views::View* before, views::View* now) {}

void UrlPillView::OnDidChangeFocus(views::View* before, views::View* now) {
  SetRevealed(IsMouseHovered() || (now && Contains(now)));
}

void UrlPillView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateRoundedRectBackground(
      kColorArciumControlBackground,
      static_cast<float>(metrics::kUrlPillHeight) / 3));
}

void UrlPillView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  if (hosted_) {
    hosted_->SetBoundsRect(GetLocalBounds());
  }
}

gfx::Size UrlPillView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   metrics::kUrlPillHeight);
}

BEGIN_METADATA(UrlPillView)
END_METADATA

}  // namespace arcium
