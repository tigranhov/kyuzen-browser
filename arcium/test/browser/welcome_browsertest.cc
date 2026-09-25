// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>
#include <vector>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/import/arc_reader.h"
#include "arcium/browser/import/import_plan.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/profile_defaults.h"
#include "arcium/common/arcium_features.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/welcome_controller.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_view.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_launcher.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/test_event.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/link.h"
#include "ui/views/test/button_test_api.h"
#include "url/gurl.h"

namespace arcium::test {
namespace {

std::string ArcFixture() {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string text;
  CHECK(base::ReadFileToString(
      root.AppendASCII("arcium/test/data/import/arc-sidebar.json"), &text));
  return text;
}

// A browser on a fresh profile that is allowed its welcome, with a made-up
// home folder holding an Arc sidebar and nothing else, so no test ever reads
// the machine's own browsers. Every test here passes --no-first-run, as all
// browser tests do; --arcium-welcome lets the welcome follow its own rule
// anyway.
class WelcomeTest : public ProfileBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ProfileBrowserTest::SetUpCommandLine(command_line);
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(home_.CreateUniqueTempDir());
      const base::FilePath arc = home_.GetPath()
                                     .AppendASCII("Library")
                                     .AppendASCII("Application Support")
                                     .AppendASCII("Arc");
      ASSERT_TRUE(base::CreateDirectory(arc));
      ASSERT_TRUE(base::WriteFile(arc.AppendASCII("StorableSidebar.json"),
                                  ArcFixture()));
    }
    command_line->AppendSwitchPath(features::kImportHomeSwitch,
                                   home_.GetPath());
    if (WelcomeAllowed()) {
      command_line->AppendSwitch(features::kWelcomeSwitch);
    }
  }

 protected:
  // Whether this launch lets the welcome show. The PRE_ step that builds an
  // older install without one says no.
  virtual bool WelcomeAllowed() const { return true; }

  BrowserSidebarController* sidebar() {
    return BrowserView::GetBrowserViewForBrowser(browser())->arcium_sidebar();
  }

  // The card, once the model has loaded and the welcome has decided.
  WelcomeController* WaitForWelcome() {
    EXPECT_TRUE(base::test::RunUntil([&] { return sidebar()->welcome(); }))
        << "the welcome never showed";
    return sidebar()->welcome();
  }

  void WaitForSources(WelcomeController* welcome) {
    ASSERT_TRUE(base::test::RunUntil([&] { return !welcome->searching(); }));
  }

  static void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(ui::test::TestEvent());
  }

  int StoredStep() {
    return browser()->GetProfile()->GetPrefs()->GetInteger(kWelcomeStepPref);
  }

  base::ScopedTempDir home_;
};

IN_PROC_BROWSER_TEST_F(WelcomeTest, AFreshInstallShowsTheCardOverThePage) {
  WelcomeController* welcome = WaitForWelcome();
  ASSERT_TRUE(welcome);
  WelcomeView* card = welcome->view_for_testing();
  EXPECT_TRUE(card->GetVisible());
  EXPECT_EQ(WelcomeStep::kSetup, card->step());
  EXPECT_FALSE(card->bounds().IsEmpty());
  // Over the page, not in the sidebar's column.
  EXPECT_GE(card->x(), sidebar()->width());
}

IN_PROC_BROWSER_TEST_F(WelcomeTest, ItFindsArcInTheHomeFolderItIsGiven) {
  WelcomeController* welcome = WaitForWelcome();
  ASSERT_TRUE(welcome);
  WaitForSources(welcome);
  ASSERT_EQ(1u, welcome->sources().size());
  EXPECT_EQ(u"Arc", welcome->sources()[0].name);
  // What was found comes chosen.
  EXPECT_EQ(0u, welcome->chosen_source());
}

IN_PROC_BROWSER_TEST_F(WelcomeTest, ImportingFillsTheSidebarAndLoadsNothing) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(ArcFixture());
  ASSERT_TRUE(plan);
  std::set<GURL> imported;
  for (const ImportEntry& entry : plan->entries) {
    imported.insert(entry.url);
  }
  ASSERT_FALSE(imported.empty());

  WelcomeController* welcome = WaitForWelcome();
  ASSERT_TRUE(welcome);
  WaitForSources(welcome);
  WelcomeView* card = welcome->view_for_testing();
  Click(card->continue_button_for_testing());
  ASSERT_EQ(WelcomeStep::kLogins, card->step());
  Click(card->continue_button_for_testing());
  ASSERT_EQ(WelcomeStep::kSearch, card->step());
  base::RunLoop().RunUntilIdle();

  // Every space came across, and the window is on the first.
  std::vector<std::string> names;
  for (const Space& space : model()->spaces()) {
    names.push_back(base::UTF16ToUTF8(space.name));
  }
  for (const ImportSpace& space : plan->spaces) {
    EXPECT_TRUE(std::ranges::find(names, space.name) != names.end())
        << space.name << " did not arrive";
  }
  const Space* on_screen = model()->GetSpace(switcher()->active_space());
  ASSERT_TRUE(on_screen);
  EXPECT_EQ(plan->spaces.front().name, base::UTF16ToUTF8(on_screen->name));
  // The empty space a fresh install starts with is gone.
  EXPECT_TRUE(std::ranges::find(names, "Space") == names.end());

  size_t entries = 0;
  for (const TabEntry& entry : model()->entries()) {
    entries += imported.contains(entry.url) ? 1 : 0;
  }
  EXPECT_GT(entries, 0u);
  // Nothing loaded: no tab is showing, or on its way to, an imported page.
  for (int i = 0; i < strip()->count(); ++i) {
    content::WebContents* contents = strip()->GetWebContentsAt(i);
    EXPECT_FALSE(imported.contains(contents->GetVisibleURL()))
        << contents->GetVisibleURL() << " was loaded by the import";
  }
}

IN_PROC_BROWSER_TEST_F(WelcomeTest, ChoosingAnEngineMakesItTheDefault) {
  WelcomeController* welcome = WaitForWelcome();
  ASSERT_TRUE(welcome);
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return service->loaded(); }));
  const std::vector<std::u16string> engines = welcome->engines();
  ASSERT_GE(engines.size(), 2u);

  const size_t other = welcome->chosen_engine() == 0 ? 1 : 0;
  welcome->ChooseEngine(other);
  ASSERT_TRUE(service->GetDefaultSearchProvider());
  EXPECT_EQ(engines[other], service->GetDefaultSearchProvider()->short_name());
  EXPECT_EQ(other, welcome->chosen_engine());
}

IN_PROC_BROWSER_TEST_F(WelcomeTest, PRE_FinishingEndsItForGood) {
  WelcomeController* welcome = WaitForWelcome();
  ASSERT_TRUE(welcome);
  welcome->view_for_testing()->skip_setup_for_testing()->OnKeyPressed(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_SPACE, ui::EF_NONE));
  ASSERT_TRUE(base::test::RunUntil([&] { return !sidebar()->welcome(); }));
  EXPECT_EQ(kWelcomeDone, StoredStep());
}

IN_PROC_BROWSER_TEST_F(WelcomeTest, FinishingEndsItForGood) {
  // Skipping made nothing, so there may be no model file at all; the stored
  // step is what says this is not a fresh install.
  // The base waits for the model to load, and the welcome decides then.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(sidebar()->welcome());
}

IN_PROC_BROWSER_TEST_F(WelcomeTest, PRE_AQuitBringsItBackAtTheStepReached) {
  WelcomeController* welcome = WaitForWelcome();
  ASSERT_TRUE(welcome);
  WaitForSources(welcome);
  WelcomeView* card = welcome->view_for_testing();
  Click(card->continue_button_for_testing());
  Click(card->continue_button_for_testing());
  ASSERT_EQ(WelcomeStep::kSearch, card->step());
  EXPECT_EQ(static_cast<int>(WelcomeStep::kSearch) + 1, StoredStep());
  FlushSessionAndModel();
}

IN_PROC_BROWSER_TEST_F(WelcomeTest, AQuitBringsItBackAtTheStepReached) {
  WelcomeController* welcome = WaitForWelcome();
  ASSERT_TRUE(welcome);
  EXPECT_EQ(WelcomeStep::kSearch, welcome->view_for_testing()->step());
}

// A setup in use, where the welcome never shows but the box's import does.
class WelcomeImportTest : public WelcomeTest {
 protected:
  bool WelcomeAllowed() const override { return false; }
};

IN_PROC_BROWSER_TEST_F(WelcomeImportTest, TheBoxsImportLandsOnWhatArrived) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(ArcFixture());
  ASSERT_TRUE(plan);
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(sidebar()->welcome());
  // The space in use holds something, so it stays; the window has to be
  // taken to what arrived rather than falling there when a space goes.
  const SpaceId in_use = switcher()->active_space();
  model()->AddEntry(in_use, EntryKind::kPinned, GURL("https://kept.example/"),
                    u"Kept");

  sidebar()->ShowImport();
  WelcomeController* welcome = sidebar()->welcome();
  ASSERT_TRUE(welcome);
  WelcomeView* card = welcome->view_for_testing();
  EXPECT_EQ(std::vector<WelcomeStep>({WelcomeStep::kSetup}), card->Steps());
  WaitForSources(welcome);
  Click(card->continue_button_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&] { return !sidebar()->welcome(); }))
      << "Import should close the card";

  EXPECT_TRUE(model()->GetSpace(in_use)) << "a space in use was removed";
  const Space* on_screen = model()->GetSpace(switcher()->active_space());
  ASSERT_TRUE(on_screen);
  EXPECT_EQ(plan->spaces.front().name, base::UTF16ToUTF8(on_screen->name));
  // The box's import is not the welcome, and leaves its progress alone.
  EXPECT_EQ(kWelcomeNotStarted, StoredStep());
}

// An install from before the welcome: a model on disk and no step stored.
class WelcomeUpdateTest : public WelcomeTest {
 protected:
  bool WelcomeAllowed() const override { return !content::IsPreTest(); }
};

IN_PROC_BROWSER_TEST_F(WelcomeUpdateTest, PRE_SomeoneUpdatingNeverSeesIt) {
  // Something to write, so the model file exists at the next launch.
  model()->AddSpace(u"Reading");
  FlushSessionAndModel();
  EXPECT_FALSE(sidebar()->welcome());
}

IN_PROC_BROWSER_TEST_F(WelcomeUpdateTest, SomeoneUpdatingNeverSeesIt) {
  // The base waits for the model to load, and the welcome decides then.
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(state()->model_file_was_absent());
  EXPECT_FALSE(sidebar()->welcome());
  EXPECT_EQ(kWelcomeNotStarted, StoredStep());
}

}  // namespace
}  // namespace arcium::test
