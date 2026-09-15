// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/command_box.h"
#include "arcium/ui/sidebar/extensions_row_view.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/history/core/browser/history_service.h"
#include "extensions/common/extension.h"
#include "net/dns/mock_host_resolver.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/widget/widget.h"

namespace arcium::test {

SidebarUiTest::SidebarUiTest()
    : no_reveal_animation_(UrlPillView::DisableRevealAnimationForTesting()) {}

SidebarUiTest::~SidebarUiTest() = default;

void SidebarUiTest::SetUpOnMainThread() {
  InProcessBrowserTest::SetUpOnMainThread();
  host_resolver()->AddRule("*", "127.0.0.1");
}

BrowserSidebarController* SidebarUiTest::Controller() {
  return BrowserView::GetBrowserViewForBrowser(browser())->arcium_sidebar();
}

UrlPillView* SidebarUiTest::Pill() {
  return Controller()->view()->url_pill();
}

ExtensionsRowView* SidebarUiTest::Row() {
  return Controller()->view()->extensions_row();
}

ExtensionsToolbarDesktop* SidebarUiTest::Container() {
  return BrowserView::GetBrowserViewForBrowser(browser())
      ->toolbar()
      ->extensions_container();
}

CommandBox* SidebarUiTest::Box() {
  return Controller()->command_box_for_testing();
}

void SidebarUiTest::OpenBox() {
  Controller()->ShowCommandBox(std::nullopt);
  CHECK(Box());
}

void SidebarUiTest::Type(const std::u16string& text) {
  Box()->SetText(text, /*select_all=*/false);
}

void SidebarUiTest::PressEnter() {
  SendKeyToBox(ui::VKEY_RETURN);
}

void SidebarUiTest::PressEscape() {
  SendKeyToBox(ui::VKEY_ESCAPE);
}

void SidebarUiTest::SendKeyToBox(int key_code) {
  CommandBox* box = Box();
  CHECK(box);
  ui::KeyEvent event(ui::EventType::kKeyPressed,
                     static_cast<ui::KeyboardCode>(key_code), ui::EF_NONE);
  box->HandleKeyEvent(nullptr, event);
  RunLoopUntilIdle();
}

void SidebarUiTest::ClickPillBackground() {
  ui::test::EventGenerator generator(Pill()->GetWidget()->GetNativeWindow());
  generator.MoveMouseTo(Pill()->GetBoundsInScreen().CenterPoint());
  generator.ClickLeftButton();
  RunLoopUntilIdle();
}

void SidebarUiTest::WaitForRows() {
  base::RunLoop loop;
  Box()->SetRowsChangedClosureForTesting(loop.QuitClosure());
  loop.Run();
  Box()->SetRowsChangedClosureForTesting(base::RepeatingClosure());
}

bool SidebarUiTest::AnyRowIsAnOpenTab() {
  CommandBox* box = Box();
  for (size_t i = 0; i < box->row_count_for_testing(); ++i) {
    if (box->row_for_testing(i).is_open_tab) {
      return true;
    }
  }
  return false;
}

void SidebarUiTest::WaitForRowThatIsAnOpenTab() {
  // Bounded: a query that never offers an open tab used to hang here until
  // the launcher killed the process, which reads as a crash and says nothing
  // about what went wrong.
  constexpr int kRounds = 20;
  for (int round = 0; round < kRounds; ++round) {
    if (AnyRowIsAnOpenTab()) {
      return;
    }
    WaitForRows();
  }
  ASSERT_TRUE(AnyRowIsAnOpenTab())
      << "no row was a tab already open after " << kRounds << " answers";
}

void SidebarUiTest::WaitForHistory(const GURL& url) {
  // A page is in history when the service says so, not when it finished
  // loading: the write is asynchronous and the box reads it.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  CHECK(history);
  while (true) {
    base::RunLoop loop;
    bool found = false;
    base::CancelableTaskTracker tracker;
    history->QueryURL(url,
                      base::BindOnce(
                          [](bool* found, base::OnceClosure done,
                             history::QueryURLResult result) {
                            *found = result.success;
                            std::move(done).Run();
                          },
                          &found, loop.QuitClosure()),
                      &tracker);
    loop.Run();
    if (found) {
      return;
    }
    RunLoopUntilIdle();
  }
}

void SidebarUiTest::PinEntryWithUrl(const GURL& url,
                                    const std::u16string& title) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(browser()->GetProfile());
  CHECK(state);
  state->model()->AddEntryForTesting(EntryKind::kPinned, url, title);
  RunLoopUntilIdle();
}

std::string SidebarUiTest::LoadTestExtension() {
  base::ScopedAllowBlockingForTesting allow_blocking;
  if (!extensions_dir_.IsValid()) {
    CHECK(extensions_dir_.CreateUniqueTempDir());
  }
  const std::string name = base::StrCat(
      {"arcium-pinned-", base::NumberToString(++extensions_written_)});
  const base::FilePath dir = extensions_dir_.GetPath().AppendASCII(name);
  CHECK(base::CreateDirectory(dir));
  // A button of its own is the whole point: without an action there is
  // nothing to pin.
  CHECK(base::WriteFile(
      dir.AppendASCII("manifest.json"),
      base::StrCat({"{\"manifest_version\": 3, \"name\": \"", name,
                    "\", \"version\": \"1.0\", \"action\": {\"default_popup\": "
                    "\"popup.html\"}}"})));
  CHECK(base::WriteFile(dir.AppendASCII("popup.html"),
                        "<!doctype html><title>popup</title><p>hello"));
  extensions::ChromeTestExtensionLoader loader(browser()->GetProfile());
  scoped_refptr<const extensions::Extension> extension =
      loader.LoadExtension(dir);
  CHECK(extension);
  return extension->id();
}

void SidebarUiTest::PinExtension(const std::string& id) {
  ToolbarActionsModel::Get(browser()->GetProfile())
      ->SetActionVisibility(id, true);
}

void SidebarUiTest::UnpinExtension(const std::string& id) {
  ToolbarActionsModel::Get(browser()->GetProfile())
      ->SetActionVisibility(id, false);
}

void SidebarUiTest::RunLoopUntilIdle() {
  base::RunLoop().RunUntilIdle();
}

}  // namespace arcium::test
