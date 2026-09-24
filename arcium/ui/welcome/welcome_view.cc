// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/welcome/welcome_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/welcome/welcome_steps.h"
#include "arcium/ui/welcome/welcome_style.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"

namespace arcium {
namespace {

using welcome::MakeLabel;
using welcome::StepContent;
using welcome::TextStyle;

constexpr int kDotSize = 6;
constexpr int kDotPitch = 19;
constexpr int kButtonHeight = 36;
constexpr int kButtonWidth = 96;
// Space between the card and the "Skip setup" link under it.
constexpr int kSkipSetupGap = 28;

std::u16string Number(size_t n) {
  return base::NumberToString16(n);
}

std::unique_ptr<views::MdTextButton> MakeButton(
    views::Button::PressedCallback callback,
    bool prominent) {
  auto button = std::make_unique<views::MdTextButton>(std::move(callback), u"");
  button->SetStyle(prominent ? ui::ButtonStyle::kProminent
                             : ui::ButtonStyle::kDefault);
  button->SetBgColorIdOverride(prominent ? kColorArciumWelcomeAccent
                                         : kColorArciumWelcomeTile);
  if (prominent) {
    button->SetEnabledTextColors(kColorArciumWelcomeOnAccent);
  } else {
    button->SetStrokeColorIdOverride(kColorArciumWelcomeTileBorder);
    button->SetEnabledTextColors(kColorArciumWelcomeText);
  }
  button->SetCornerRadius(8);
  button->SetMinSize(gfx::Size(kButtonWidth, kButtonHeight));
  button->SetMaxSize(gfx::Size(0, kButtonHeight));
  return button;
}

StepContent BuildStep(WelcomeModel& model, WelcomeStep step, bool import_only) {
  switch (step) {
    case WelcomeStep::kSetup:
      return welcome::BuildSetupStep(model, import_only);
    case WelcomeStep::kLogins:
      return welcome::BuildLoginsStep(model);
    case WelcomeStep::kSearch:
      return welcome::BuildSearchStep(model);
    case WelcomeStep::kDefaultBrowser:
      return welcome::BuildDefaultBrowserStep(model);
    case WelcomeStep::kSync:
      return welcome::BuildSyncStep(model);
    case WelcomeStep::kDone:
      return welcome::BuildDoneStep(model);
  }
}

// The footer: Back at one edge, Skip and Continue at the other, and the
// dots in the middle of the card whatever the buttons' widths.
class Footer : public views::View {
  METADATA_HEADER(Footer, views::View)

 public:
  Footer() = default;
  Footer(const Footer&) = delete;
  Footer& operator=(const Footer&) = delete;
  ~Footer() override = default;

  void SetParts(views::View* back,
                views::View* middle,
                views::View* skip,
                views::View* go) {
    back_ = back;
    middle_ = middle;
    skip_ = skip;
    go_ = go;
  }

  void Layout(PassKey) override {
    const gfx::Rect area = GetContentsBounds();
    const auto place = [&](views::View* view, int x) {
      const gfx::Size size = view->GetPreferredSize();
      view->SetBounds(x, area.y() + (area.height() - size.height()) / 2,
                      size.width(), size.height());
      return size.width();
    };
    place(back_, area.x());
    int right = area.right();
    right -= place(go_, right - go_->GetPreferredSize().width());
    if (skip_->GetVisible()) {
      right -= 10;
      place(skip_, right - skip_->GetPreferredSize().width());
    }
    place(middle_,
          area.CenterPoint().x() - middle_->GetPreferredSize().width() / 2);
  }

 private:
  raw_ptr<views::View> back_ = nullptr;
  raw_ptr<views::View> middle_ = nullptr;
  raw_ptr<views::View> skip_ = nullptr;
  raw_ptr<views::View> go_ = nullptr;
};

BEGIN_METADATA(Footer)
END_METADATA

}  // namespace

namespace welcome {
StepContent::StepContent() = default;
StepContent::StepContent(StepContent&&) = default;
StepContent& StepContent::operator=(StepContent&&) = default;
StepContent::~StepContent() = default;
}  // namespace welcome

// One dot per step, the current one in the accent.
class WelcomeView::Dots : public views::View {
  METADATA_HEADER(Dots, views::View)

 public:
  Dots() = default;
  Dots(const Dots&) = delete;
  Dots& operator=(const Dots&) = delete;
  ~Dots() override = default;

  void Set(size_t count, size_t current) {
    count_ = count;
    current_ = current;
    PreferredSizeChanged();
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(count_ == 0 ? 0 : (count_ - 1) * kDotPitch + kDotSize,
                     kDotSize);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    for (size_t i = 0; i < count_; ++i) {
      flags.setColor(colors->GetColor(i == current_
                                          ? kColorArciumWelcomeAccent
                                          : kColorArciumWelcomeDotIdle));
      canvas->DrawCircle(
          gfx::PointF(i * kDotPitch + kDotSize / 2.0f, kDotSize / 2.0f),
          kDotSize / 2.0f, flags);
    }
  }

 private:
  size_t count_ = 0;
  size_t current_ = 0;
};

BEGIN_METADATA(WelcomeView, Dots)
END_METADATA

WelcomeView::WelcomeView(WelcomeModel* model, Mode mode, WelcomeStep first)
    : model_(model), mode_(mode), step_(first) {
  SetBackground(views::CreateSolidBackground(kColorArciumWelcomeBackground));
  BuildCard();
  observation_.Observe(model_);
  const std::vector<WelcomeStep> steps = Steps();
  if (std::ranges::find(steps, step_) == steps.end()) {
    step_ = steps.front();
  }
  Rebuild();
}

WelcomeView::~WelcomeView() = default;

std::vector<WelcomeStep> WelcomeView::Steps() const {
  if (mode_ == Mode::kImportOnly) {
    return {WelcomeStep::kSetup};
  }
  std::vector<WelcomeStep> steps = {WelcomeStep::kSetup, WelcomeStep::kLogins,
                                    WelcomeStep::kSearch,
                                    WelcomeStep::kDefaultBrowser};
  if (model_->sync_available()) {
    steps.push_back(WelcomeStep::kSync);
  }
  steps.push_back(WelcomeStep::kDone);
  return steps;
}

views::View* WelcomeView::choices_for_testing() {
  return static_cast<views::ScrollView*>(choices_.get())->contents();
}

size_t WelcomeView::IndexOf(WelcomeStep step) const {
  const std::vector<WelcomeStep> steps = Steps();
  return static_cast<size_t>(std::ranges::find(steps, step) - steps.begin());
}

void WelcomeView::BuildCard() {
  auto* card = AddChildView(std::make_unique<views::BoxLayoutView>());
  card_ = card;
  card->SetOrientation(views::BoxLayout::Orientation::kVertical);
  card->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  card->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumWelcomeCard, welcome::kCardRadius, 1));
  card->SetBorder(views::CreateRoundedRectBorder(
      1, welcome::kCardRadius, kColorArciumWelcomeCardBorder));

  auto* body = card->AddChildView(std::make_unique<views::BoxLayoutView>());
  card->SetFlexForView(body, 1);
  body->SetInsideBorderInsets(gfx::Insets::TLBR(26, welcome::kCardPadding, 22,
                                                welcome::kCardPadding - 4));
  body->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* left = body->AddChildView(std::make_unique<views::BoxLayoutView>());
  body->SetFlexForView(left, 1);
  left->SetOrientation(views::BoxLayout::Orientation::kVertical);
  left->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  eyebrow_ = left->AddChildView(MakeLabel(u"", TextStyle::kEyebrow));
  title_ = left->AddChildView(MakeLabel(u"", TextStyle::kTitle));
  title_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(8, 0, 6, 0));
  subtitle_ = left->AddChildView(MakeLabel(u"", TextStyle::kSubtitle));
  subtitle_->SetMultiLine(true);
  subtitle_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 18, 0));
  choices_ = left->AddChildView(std::make_unique<views::View>());
  left->SetFlexForView(choices_, 1);

  divider_ = body->AddChildView(std::make_unique<views::View>());
  divider_->SetBackground(
      views::CreateSolidBackground(kColorArciumWelcomeCardBorder));
  divider_->SetPreferredSize(gfx::Size(1, 1));
  divider_->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, 26));

  auto* panel_box =
      body->AddChildView(std::make_unique<views::BoxLayoutView>());
  panel_box_ = panel_box;
  panel_box->SetOrientation(views::BoxLayout::Orientation::kVertical);
  panel_box->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  panel_box->SetInsideBorderInsets(gfx::Insets::VH(22, 20));
  panel_box->SetPreferredSize(gfx::Size(welcome::kPanelWidth, 0));
  panel_box->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumWelcomePanel, welcome::kPanelRadius));
  panel_title_ =
      panel_box->AddChildView(MakeLabel(u"", TextStyle::kPanelTitle));
  panel_subtitle_ = panel_box->AddChildView(MakeLabel(u"", TextStyle::kBody));
  panel_subtitle_->SetEnabledColor(kColorArciumWelcomeTextSecondary);
  panel_subtitle_->SetMultiLine(true);
  panel_subtitle_->SetProperty(views::kMarginsKey,
                               gfx::Insets::TLBR(2, 0, 18, 0));
  panel_ = panel_box->AddChildView(std::make_unique<views::View>());
  panel_box->SetFlexForView(panel_, 1);
  panel_note_ = panel_box->AddChildView(MakeLabel(u"", TextStyle::kCaption));
  panel_note_->SetMultiLine(true);

  auto* rule = card->AddChildView(std::make_unique<views::View>());
  rule->SetBackground(
      views::CreateSolidBackground(kColorArciumWelcomeCardBorder));
  rule->SetPreferredSize(gfx::Size(1, 1));

  auto* footer = card->AddChildView(std::make_unique<Footer>());
  footer->SetPreferredSize(gfx::Size(0, welcome::kFooterHeight));
  footer->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(0, welcome::kCardPadding - 4)));
  back_ = footer->AddChildView(MakeButton(
      base::BindRepeating(&WelcomeView::OnBack, base::Unretained(this)),
      false));
  back_->SetText(u"Back");
  auto* middle = footer->AddChildView(std::make_unique<views::BoxLayoutView>());
  middle->SetOrientation(views::BoxLayout::Orientation::kVertical);
  middle->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  middle->SetBetweenChildSpacing(8);
  dots_ = middle->AddChildView(std::make_unique<Dots>());
  position_ = middle->AddChildView(MakeLabel(u"", TextStyle::kCaption));
  skip_ = footer->AddChildView(MakeButton(
      base::BindRepeating(&WelcomeView::OnSkip, base::Unretained(this)),
      false));
  continue_ = footer->AddChildView(MakeButton(
      base::BindRepeating(&WelcomeView::OnContinue, base::Unretained(this)),
      true));
  footer->SetParts(back_, middle, skip_, continue_);

  auto link = std::make_unique<views::Link>(u"Skip setup");
  link->SetCallback(
      base::BindRepeating(&WelcomeView::OnSkipSetup, base::Unretained(this)));
  link->SetForceUnderline(true);
  link->SetEnabledColor(kColorArciumWelcomeTextSecondary);
  skip_setup_ = AddChildView(std::move(link));
}

void WelcomeView::Rebuild() {
  rebuild_pending_ = false;
  const std::vector<WelcomeStep> steps = Steps();
  const size_t index = IndexOf(step_);
  const bool import_only = mode_ == Mode::kImportOnly;
  StepContent content = BuildStep(*model_, step_, import_only);

  eyebrow_->SetText(u"STEP " + Number(index + 1) + u" OF " +
                    Number(steps.size()));
  eyebrow_->SetVisible(!import_only);
  title_->SetText(content.title);
  subtitle_->SetText(content.subtitle);

  // The choices scroll when a source brings more spaces than fit, rather
  // than pushing the footer off the card.
  views::View* parent = choices_->parent();
  const size_t at = parent->GetIndexOf(choices_).value();
  parent->RemoveChildViewT(choices_.ExtractAsDangling());
  auto scroll = std::make_unique<views::ScrollView>();
  // Painted in the card's colour rather than left clear: the scroller draws
  // to a layer of its own, and text on a clear layer cannot be drawn crisply.
  scroll->SetBackgroundColor(kColorArciumWelcomeCard);
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetContents(std::move(content.choices));
  choices_ = parent->AddChildViewAt(std::move(scroll), at);
  static_cast<views::BoxLayoutView*>(parent)->SetFlexForView(choices_, 1);

  panel_title_->SetText(content.panel_title);
  panel_subtitle_->SetText(content.panel_subtitle);
  panel_subtitle_->SetVisible(!content.panel_subtitle.empty());
  panel_note_->SetText(content.panel_note);
  panel_note_->SetVisible(!content.panel_note.empty());
  const size_t panel_at = panel_box_->GetIndexOf(panel_).value();
  panel_box_->RemoveChildViewT(panel_.ExtractAsDangling());
  panel_ = panel_box_->AddChildViewAt(std::move(content.panel), panel_at);
  static_cast<views::BoxLayoutView*>(panel_box_.get())
      ->SetFlexForView(panel_, 1);

  const bool last = step_ == WelcomeStep::kDone;
  back_->SetVisible(!import_only);
  back_->SetEnabled(index > 0);
  dots_->Set(import_only ? 0 : steps.size(), index);
  position_->SetText(Number(index + 1) + u" of " + Number(steps.size()));
  position_->SetVisible(!import_only);
  skip_->SetText(import_only ? u"Cancel" : u"Skip");
  skip_->SetVisible(!last);
  continue_->SetText(import_only ? u"Import"
                     : last      ? u"Start browsing"
                                 : u"Continue");
  // Importing needs something to import.
  continue_->SetEnabled(!import_only || model_->chosen_source().has_value());
  skip_setup_->SetVisible(!import_only && !last);
  InvalidateLayout();
}

void WelcomeView::GoTo(WelcomeStep step) {
  step_ = step;
  Rebuild();
}

void WelcomeView::OnBack() {
  const size_t index = IndexOf(step_);
  if (index > 0) {
    GoTo(Steps()[index - 1]);
  }
}

void WelcomeView::OnSkip() {
  if (mode_ == Mode::kImportOnly) {
    model_->Finish();
    return;
  }
  // Skipping the first step is choosing to bring nothing. Skipping the
  // second keeps the logins as they are and still brings what the first
  // step chose; with nothing chosen there is nothing to bring, and the
  // example spaces are not made for someone who skipped them.
  if (step_ == WelcomeStep::kSetup) {
    model_->ChooseSource(std::nullopt);
  } else if (step_ == WelcomeStep::kLogins &&
             model_->chosen_source().has_value()) {
    model_->ApplyImport();
  }
  const size_t index = IndexOf(step_);
  const WelcomeStep next = Steps()[index + 1];
  model_->StepReached(next);
  GoTo(next);
}

void WelcomeView::OnContinue() {
  if (mode_ == Mode::kImportOnly) {
    model_->ApplyImport();
    model_->Finish();
    return;
  }
  if (step_ == WelcomeStep::kDone) {
    model_->Finish();
    return;
  }
  if (step_ == WelcomeStep::kLogins) {
    model_->ApplyImport();
  }
  const WelcomeStep next = Steps()[IndexOf(step_) + 1];
  model_->StepReached(next);
  GoTo(next);
}

void WelcomeView::OnSkipSetup() {
  model_->Finish();
}

void WelcomeView::OnWelcomeChanged() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<WelcomeView> view) {
                       if (view && view->rebuild_pending_) {
                         view->Rebuild();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void WelcomeView::Layout(PassKey) {
  const gfx::Rect area = GetContentsBounds();
  const int width = std::min(welcome::kCardWidth, area.width() - 48);
  const bool panel = width >= welcome::kPanelMinCardWidth;
  divider_->SetVisible(panel);
  panel_box_->SetVisible(panel);
  // The panel narrows with the card before it goes.
  const gfx::Size panel_size(
      std::clamp(width * 38 / 100, welcome::kPanelMinWidth,
                 welcome::kPanelWidth),
      0);
  if (panel_box_->GetPreferredSize() != panel_size) {
    panel_box_->SetPreferredSize(panel_size);
  }
  // Room for "Skip setup" whether or not this step shows it, so the card
  // holds still when the last step drops it.
  const gfx::Size link = skip_setup_->GetPreferredSize();
  const int block =
      welcome::kCardHeight +
      (mode_ == Mode::kWelcome ? kSkipSetupGap + link.height() : 0);
  const int top = area.y() + std::max(24, (area.height() - block) / 2);
  card_->SetBounds(area.x() + (area.width() - width) / 2, top, width,
                   welcome::kCardHeight);
  skip_setup_->SetBounds(area.CenterPoint().x() - link.width() / 2,
                         card_->bounds().bottom() + kSkipSetupGap, link.width(),
                         link.height());
}

BEGIN_METADATA(WelcomeView)
END_METADATA

}  // namespace arcium
