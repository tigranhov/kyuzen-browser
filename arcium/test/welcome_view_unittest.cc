// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/welcome/welcome_view.h"

#include <memory>
#include <string>
#include <vector>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_welcome_model.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/test_event.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {
namespace {

// Every label under `root` that reads `text`, visible or not; a test asks
// about visibility separately, where it matters.
std::vector<views::Label*> LabelsReading(views::View* root,
                                         const std::u16string& text) {
  std::vector<views::Label*> found;
  if (auto* label = views::AsViewClass<views::Label>(root);
      label && label->GetText() == text) {
    found.push_back(label);
  }
  for (views::View* child : root->children()) {
    std::vector<views::Label*> below = LabelsReading(child, text);
    found.insert(found.end(), below.begin(), below.end());
  }
  return found;
}

bool Shows(views::View* root, const std::u16string& text) {
  return !LabelsReading(root, text).empty();
}

// The button under `root` whose label reads `text`.
views::LabelButton* ButtonReading(views::View* root,
                                  const std::u16string& text) {
  if (auto* button = views::AsViewClass<views::LabelButton>(root);
      button && button->GetText() == text) {
    return button;
  }
  for (views::View* child : root->children()) {
    if (views::LabelButton* found = ButtonReading(child, text)) {
      return found;
    }
  }
  return nullptr;
}

void Click(views::Button* button) {
  views::test::ButtonTestApi(button).NotifyClick(ui::test::TestEvent());
}

class WelcomeViewTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    // Before ViewsTestBase::SetUp(), as in the sidebar's view tests: it would
    // otherwise bring this binary to the front.
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    contents_ = widget_->SetContentsView(std::make_unique<views::View>());
    contents_->SetLayoutManager(std::make_unique<views::FillLayout>());
    widget_->SetBounds(gfx::Rect(0, 0, 1000, 720));
    widget_->Show();
  }

  void TearDown() override {
    view_ = nullptr;
    contents_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  // Hidden for the reason the sidebar's view tests give: draining the thread
  // pool with it can hang on a draw. Everything the card waits for is posted
  // to this thread, so RunPendingMessages() is the whole wait.
  base::test::TaskEnvironment* task_environment() = delete;

  WelcomeView* Open(WelcomeStep first,
                    WelcomeView::Mode mode = WelcomeView::Mode::kWelcome) {
    view_ = contents_->AddChildView(
        std::make_unique<WelcomeView>(&model_, mode, first));
    widget_->LayoutRootViewIfNecessary();
    return view_;
  }

  views::View* choices() { return view_->choices_for_testing(); }

  FakeWelcomeModel model_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<WelcomeView> view_ = nullptr;
};

TEST_F(WelcomeViewTest, ContinueWalksTheStepsInOrderAndBackReturns) {
  WelcomeView* view = Open(WelcomeStep::kSetup);
  EXPECT_FALSE(view->back_button_for_testing()->GetEnabled());

  const std::vector<WelcomeStep> expected = {
      WelcomeStep::kLogins, WelcomeStep::kSearch, WelcomeStep::kDefaultBrowser,
      WelcomeStep::kDone};
  for (WelcomeStep step : expected) {
    Click(view->continue_button_for_testing());
    EXPECT_EQ(step, view->step());
  }
  // Each step reached is recorded, so a quit resumes there.
  EXPECT_EQ(expected, model_.reached());

  Click(view->back_button_for_testing());
  EXPECT_EQ(WelcomeStep::kDefaultBrowser, view->step());
  // Going back is not reaching a step.
  EXPECT_EQ(expected.size(), model_.reached().size());
}

TEST_F(WelcomeViewTest, ContinuingPastLoginsImportsOnceAndOnlyThere) {
  WelcomeView* view = Open(WelcomeStep::kSetup);
  Click(view->continue_button_for_testing());
  EXPECT_EQ(0, model_.apply_count());
  Click(view->continue_button_for_testing());
  EXPECT_EQ(1, model_.apply_count());
  Click(view->continue_button_for_testing());
  Click(view->continue_button_for_testing());
  EXPECT_EQ(1, model_.apply_count());
}

TEST_F(WelcomeViewTest, SkippingTheFirstStepStartsFreshWithExampleSpaces) {
  WelcomeView* view = Open(WelcomeStep::kSetup);
  ASSERT_TRUE(model_.chosen_source().has_value());
  Click(view->skip_button_for_testing());
  EXPECT_FALSE(model_.chosen_source().has_value());
  EXPECT_EQ(WelcomeStep::kLogins, view->step());

  // The logins step offers the example spaces, not the found source's.
  EXPECT_TRUE(Shows(choices(), u"Personal"));
  EXPECT_TRUE(Shows(choices(), u"Work"));
  EXPECT_FALSE(Shows(choices(), u"Research"));
  EXPECT_TRUE(
      Shows(choices(), u"Example spaces. Rename or remove them any time."));
}

TEST_F(WelcomeViewTest, SkippingLoginsImportsWhatTheFirstStepChose) {
  WelcomeView* view = Open(WelcomeStep::kLogins);
  Click(view->skip_button_for_testing());
  EXPECT_EQ(1, model_.apply_count());
  EXPECT_EQ(WelcomeStep::kSearch, view->step());
}

TEST_F(WelcomeViewTest, SkippingLoginsAfterStartingFreshMakesNoSpaces) {
  model_.ChooseSource(std::nullopt);
  WelcomeView* view = Open(WelcomeStep::kLogins);
  Click(view->skip_button_for_testing());
  EXPECT_EQ(0, model_.apply_count());
  EXPECT_EQ(WelcomeStep::kSearch, view->step());
}

TEST_F(WelcomeViewTest, SkipSetupFinishesFromAnyStepButTheLast) {
  WelcomeView* view = Open(WelcomeStep::kSearch);
  ASSERT_TRUE(view->skip_setup_for_testing()->GetVisible());
  // The space bar: Return clicks a focused control on some platforms only.
  view->skip_setup_for_testing()->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_SPACE, ui::EF_NONE));
  EXPECT_EQ(1, model_.finish_count());
  EXPECT_EQ(0, model_.apply_count());
}

TEST_F(WelcomeViewTest, TheLastStepHasNoSkipAndStartsBrowsing) {
  WelcomeView* view = Open(WelcomeStep::kDone);
  EXPECT_FALSE(view->skip_button_for_testing()->GetVisible());
  EXPECT_FALSE(view->skip_setup_for_testing()->GetVisible());
  EXPECT_EQ(u"Start browsing", view->continue_button_for_testing()->GetText());
  Click(view->continue_button_for_testing());
  EXPECT_EQ(1, model_.finish_count());
}

TEST_F(WelcomeViewTest, SyncIsAStepOnlyOnceItExists) {
  WelcomeView* view = Open(WelcomeStep::kSetup);
  EXPECT_EQ(u"1 of 5", view->position_label_for_testing()->GetText());
  EXPECT_EQ(
      std::vector<WelcomeStep>(
          {WelcomeStep::kSetup, WelcomeStep::kLogins, WelcomeStep::kSearch,
           WelcomeStep::kDefaultBrowser, WelcomeStep::kDone}),
      view->Steps());

  model_.SetSyncAvailable(true);
  RunPendingMessages();
  EXPECT_EQ(u"1 of 6", view->position_label_for_testing()->GetText());
  Open(WelcomeStep::kDefaultBrowser);
  Click(view_->continue_button_for_testing());
  EXPECT_EQ(WelcomeStep::kSync, view_->step());
}

TEST_F(WelcomeViewTest, AStepThatIsNotOnTheWayOpensTheFirst) {
  // The pref can name the sync step from a build that had it.
  WelcomeView* view = Open(WelcomeStep::kSync);
  EXPECT_EQ(WelcomeStep::kSetup, view->step());
}

TEST_F(WelcomeViewTest, TheDefaultBrowserButtonTurnsIntoAConfirmation) {
  WelcomeView* view = Open(WelcomeStep::kDefaultBrowser);
  views::LabelButton* make =
      ButtonReading(choices(), u"Make Kyuzen my default browser");
  ASSERT_TRUE(make);
  EXPECT_FALSE(Shows(choices(), u"Kyuzen is your default browser"));

  Click(make);
  RunPendingMessages();
  EXPECT_FALSE(ButtonReading(choices(), u"Make Kyuzen my default browser"));
  EXPECT_TRUE(Shows(choices(), u"Kyuzen is your default browser"));
  // Saying yes to macOS is not a step forward.
  EXPECT_EQ(WelcomeStep::kDefaultBrowser, view->step());
}

TEST_F(WelcomeViewTest, AModelChangeRedrawsAtTheNextTurnNotInsideIt) {
  Open(WelcomeStep::kSetup);
  const std::u16string looking = u"Looking for Zen and Arc on this Mac…";
  model_.SetSearching(true);
  // Redrawing inside the notification would delete the control that caused
  // it while it still runs its click.
  EXPECT_FALSE(Shows(view_, looking));
  RunPendingMessages();
  EXPECT_TRUE(Shows(view_, looking));
}

TEST_F(WelcomeViewTest, ChoosingStartFreshSurvivesItsOwnClick) {
  Open(WelcomeStep::kSetup);
  views::LabelButton* fresh = ButtonReading(choices(), u"Start fresh");
  ASSERT_TRUE(fresh);
  Click(fresh);
  // The radio is still alive and checked after its click returned.
  EXPECT_TRUE(static_cast<views::RadioButton*>(fresh)->GetChecked());
  EXPECT_FALSE(model_.chosen_source().has_value());
  RunPendingMessages();
  EXPECT_TRUE(Shows(view_, u"To start with"));
}

TEST_F(WelcomeViewTest, TheImportCommandOpensOneStepWithImport) {
  WelcomeView* view = Open(WelcomeStep::kSetup, WelcomeView::Mode::kImportOnly);
  EXPECT_EQ(std::vector<WelcomeStep>({WelcomeStep::kSetup}), view->Steps());
  EXPECT_FALSE(view->back_button_for_testing()->GetVisible());
  EXPECT_FALSE(view->skip_setup_for_testing()->GetVisible());
  EXPECT_EQ(u"Cancel", view->skip_button_for_testing()->GetText());
  EXPECT_EQ(u"Import", view->continue_button_for_testing()->GetText());
  EXPECT_TRUE(Shows(view, u"Import from Zen or Arc"));
  // Starting fresh is what a first launch offers, not an import.
  EXPECT_FALSE(ButtonReading(choices(), u"Start fresh"));

  Click(view->continue_button_for_testing());
  EXPECT_EQ(1, model_.apply_count());
  EXPECT_EQ(1, model_.finish_count());
}

TEST_F(WelcomeViewTest, ImportWaitsForSomethingToImport) {
  model_.SetSources({});
  WelcomeView* view = Open(WelcomeStep::kSetup, WelcomeView::Mode::kImportOnly);
  EXPECT_FALSE(view->continue_button_for_testing()->GetEnabled());
  Click(view->skip_button_for_testing());
  EXPECT_EQ(0, model_.apply_count());
  EXPECT_EQ(1, model_.finish_count());
}

}  // namespace
}  // namespace arcium
