// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Step 4, Default browser: one button, which says so once macOS agrees.

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_steps.h"
#include "arcium/ui/welcome/welcome_style.h"
#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"

namespace arcium::welcome {
namespace {

// Kyuzen's own mark, a ring around a dot, in the accent.
class KyuzenMark : public views::View {
  METADATA_HEADER(KyuzenMark, views::View)

 public:
  KyuzenMark() { SetPreferredSize(gfx::Size(20, 20)); }

  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(GetColorProvider()->GetColor(kColorArciumWelcomeAccent));
    canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 5, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(2);
    canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 2 - 1, flags);
  }
};

BEGIN_METADATA(KyuzenMark)
END_METADATA

// An app with a link in it, and where the link goes.
std::unique_ptr<views::View> LinkFrom(const gfx::VectorIcon& icon,
                                      const std::u16string& app) {
  auto tile = MakeTile();
  auto* row = tile->SetLayoutManager(std::make_unique<views::BoxLayout>());
  row->set_inside_border_insets(gfx::Insets::VH(10, 14));
  row->set_between_child_spacing(12);
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  tile->AddChildView(MakeIcon(icon, 20, kColorArciumWelcomeText));
  auto* words = tile->AddChildView(std::make_unique<views::BoxLayoutView>());
  words->SetOrientation(views::BoxLayout::Orientation::kVertical);
  words->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
  words->AddChildView(MakeLabel(app, TextStyle::kBody));
  auto* link =
      words->AddChildView(MakeLabel(u"Open link", TextStyle::kCaption));
  link->SetEnabledColor(kColorArciumWelcomeAccent);
  row->SetFlexForView(words, 1);
  tile->AddChildView(MakeIcon(vector_icons::kArrowForwardIcon, 16,
                              kColorArciumWelcomeTextSecondary));
  auto target = MakeTile();
  auto* target_row =
      target->SetLayoutManager(std::make_unique<views::BoxLayout>());
  target_row->set_inside_border_insets(gfx::Insets::VH(8, 10));
  target_row->set_between_child_spacing(8);
  target_row->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  target->AddChildView(std::make_unique<KyuzenMark>());
  target->AddChildView(MakeLabel(u"Kyuzen", TextStyle::kBody));
  tile->AddChildView(std::move(target));
  return tile;
}

}  // namespace

StepContent BuildDefaultBrowserStep(WelcomeModel& model) {
  StepContent step;
  step.title = u"Default browser";
  step.subtitle = u"Open links from other apps in Kyuzen.";

  auto choices = std::make_unique<views::BoxLayoutView>();
  choices->SetOrientation(views::BoxLayout::Orientation::kVertical);
  choices->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
  choices->SetBetweenChildSpacing(10);
  if (model.is_default_browser()) {
    // Done: the button becomes the fact it asked for.
    auto done = MakeTile(true);
    auto* row = done->SetLayoutManager(std::make_unique<views::BoxLayout>());
    row->set_inside_border_insets(gfx::Insets::VH(12, 16));
    row->set_between_child_spacing(10);
    row->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    done->AddChildView(MakeIcon(vector_icons::kCheckCircleFilledIcon, 18,
                                kColorArciumWelcomeAccent));
    done->AddChildView(
        MakeLabel(u"Kyuzen is your default browser", TextStyle::kBodyStrong));
    choices->AddChildView(std::move(done));
  } else {
    auto* make = choices->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(
            [](WelcomeModel* model) { model->MakeDefaultBrowser(); }, &model),
        u"Make Kyuzen my default browser"));
    make->SetStyle(ui::ButtonStyle::kProminent);
    make->SetBgColorIdOverride(kColorArciumWelcomeAccent);
    make->SetEnabledTextColors(kColorArciumWelcomeOnAccent);
    make->SetCornerRadius(8);
    make->SetMinSize(gfx::Size(0, 44));
    choices->AddChildView(
        MakeLabel(u"macOS will ask you to confirm.", TextStyle::kCaption));
  }
  step.choices = std::move(choices);

  step.panel_title = u"Links open here";
  step.panel_subtitle = u"From your apps to Kyuzen";
  auto panel = std::make_unique<views::BoxLayoutView>();
  panel->SetOrientation(views::BoxLayout::Orientation::kVertical);
  panel->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  panel->SetBetweenChildSpacing(10);
  panel->AddChildView(LinkFrom(vector_icons::kMailIcon, u"Mail"));
  panel->AddChildView(LinkFrom(vector_icons::kChatIcon, u"Slack"));
  panel->AddChildView(LinkFrom(vector_icons::kDescriptionIcon, u"Notes"));
  step.panel = std::move(panel);
  step.panel_note = u"Your links open in the browser you chose.";
  return step;
}

}  // namespace arcium::welcome
