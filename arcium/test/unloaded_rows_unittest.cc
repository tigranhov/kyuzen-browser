// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class UnloadedRowsTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // A background tab as session restore leaves it: history, not loaded.
  // Appended, so the tab AddTab made stays at index 0 and stays active.
  tabs::TabInterface* AppendRestoredTab(const GURL& url) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    std::vector<std::unique_ptr<content::NavigationEntry>> entries;
    entries.push_back(content::NavigationEntry::Create());
    entries.back()->SetURL(url);
    contents->GetController().Restore(0, content::RestoreType::kRestored,
                                      &entries);
    strip()->AppendWebContents(std::move(contents), /*foreground=*/false);
    return strip()->GetTabAtIndex(strip()->count() - 1);
  }

  std::unique_ptr<SidebarTabModel> MakeModel() {
    return std::make_unique<SidebarTabModel>(strip(), &arcium_model_,
                                             &binding_);
  }

  static std::optional<SidebarRow> RowAt(const SidebarTabModel& model,
                                         int tab_index) {
    for (const SidebarRow& row : model.rows()) {
      if (row.tab_index == tab_index) {
        return row;
      }
    }
    return std::nullopt;
  }

  static std::optional<SidebarRow> RowFor(const SidebarTabModel& model,
                                          EntryId id) {
    for (const SidebarRow& row : model.rows()) {
      if (row.entry_id == id) {
        return row;
      }
    }
    return std::nullopt;
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
};

TEST(SidebarRowTest, NeedsLoadIsColdOrUnloaded) {
  SidebarRow row;
  EXPECT_FALSE(row.needs_load());
  row.is_cold = true;
  EXPECT_TRUE(row.needs_load());
  row.is_cold = false;
  row.is_unloaded = true;
  EXPECT_TRUE(row.needs_load());
}

TEST_F(UnloadedRowsTest, ARestoredTodayTabThatHasNotLoadedIsUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  AppendRestoredTab(GURL("https://later.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  ASSERT_TRUE(RowAt(*model, 0));
  ASSERT_TRUE(RowAt(*model, 1));
  EXPECT_FALSE(RowAt(*model, 0)->is_unloaded);
  EXPECT_TRUE(RowAt(*model, 1)->is_unloaded);
  EXPECT_FALSE(RowAt(*model, 1)->is_cold);
}

TEST_F(UnloadedRowsTest, ATabThatStartsLoadingIsNoLongerUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  AppendRestoredTab(GURL("https://later.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  strip()->GetWebContentsAt(1)->GetController().LoadIfNecessary();
  EXPECT_FALSE(RowAt(*model, 1)->is_unloaded);
}

TEST_F(UnloadedRowsTest, ADiscardedTabIsUnloaded) {
  AddTab(browser(), GURL("https://two.example/"));
  AddTab(browser(), GURL("https://one.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  strip()->GetWebContentsAt(1)->SetWasDiscarded(true);
  EXPECT_TRUE(RowAt(*model, 1)->is_unloaded);
}

// The row on screen is the one the user is looking at; whatever Chromium's
// flags say for an instant, it is never drawn as waiting for a click.
TEST_F(UnloadedRowsTest, TheActiveRowIsNeverUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  strip()->GetWebContentsAt(0)->SetWasDiscarded(true);
  EXPECT_TRUE(RowAt(*model, 0)->is_active);
  EXPECT_FALSE(RowAt(*model, 0)->is_unloaded);
}

// A pin whose tab came back from the session but has not loaded: it has a
// tab, so it is not cold, and the tab has no page, so it is unloaded.
TEST_F(UnloadedRowsTest, APinWhoseTabHasNotLoadedIsUnloadedNotCold) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  tabs::TabInterface* tab = AppendRestoredTab(GURL("https://pinned.example/"));
  const EntryId id = arcium_model_.AddEntry(
      arcium_model_.spaces().front().id, EntryKind::kPinned,
      GURL("https://pinned.example/"), u"Pinned");
  binding_.Bind(id, tab->GetHandle());
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  ASSERT_TRUE(RowFor(*model, id));
  EXPECT_FALSE(RowFor(*model, id)->is_cold);
  EXPECT_TRUE(RowFor(*model, id)->is_unloaded);
}

// A pin with no tab at all is cold, and cold only.
TEST_F(UnloadedRowsTest, AColdPinIsNotAlsoUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  const EntryId id = arcium_model_.AddEntry(
      arcium_model_.spaces().front().id, EntryKind::kPinned,
      GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  ASSERT_TRUE(RowFor(*model, id));
  EXPECT_TRUE(RowFor(*model, id)->is_cold);
  EXPECT_FALSE(RowFor(*model, id)->is_unloaded);
}

}  // namespace
}  // namespace arcium
