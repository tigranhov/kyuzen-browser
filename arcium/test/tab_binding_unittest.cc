// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_binding.h"

#include "arcium/browser/model/entry_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class TabBindingTest : public BrowserWithTestWindowTest {
 protected:
  tabs::TabHandle HandleAt(int index) {
    return browser()->tab_strip_model()->GetTabAtIndex(index)->GetHandle();
  }
};

TEST_F(TabBindingTest, BindingIsVisibleFromBothSides) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));

  ASSERT_TRUE(binding.TabForEntry(id).has_value());
  EXPECT_EQ(HandleAt(0), *binding.TabForEntry(id));
  ASSERT_TRUE(binding.EntryForTab(HandleAt(0)).has_value());
  EXPECT_EQ(id, *binding.EntryForTab(HandleAt(0)));
  EXPECT_TRUE(binding.IsBound(HandleAt(0)));
}

TEST_F(TabBindingTest, AnUnboundEntryHasNoTab) {
  TabBinding binding;
  EXPECT_FALSE(binding.TabForEntry(EntryId::Generate()).has_value());
}

TEST_F(TabBindingTest, UnbindEntryClearsBothDirections) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));
  binding.UnbindEntry(id);
  EXPECT_FALSE(binding.TabForEntry(id).has_value());
  EXPECT_FALSE(binding.EntryForTab(HandleAt(0)).has_value());
}

TEST_F(TabBindingTest, UnbindTabClearsBothDirections) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));
  binding.UnbindTab(HandleAt(0));
  EXPECT_FALSE(binding.TabForEntry(id).has_value());
}

TEST_F(TabBindingTest, RebindingAnEntryReleasesItsOldTab) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));
  binding.Bind(id, HandleAt(1));
  EXPECT_EQ(HandleAt(1), *binding.TabForEntry(id));
  EXPECT_FALSE(binding.EntryForTab(HandleAt(0)).has_value());
}

TEST_F(TabBindingTest, BindingATabAlreadyBoundReleasesTheOldEntry) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId first = EntryId::Generate();
  const EntryId second = EntryId::Generate();
  TabBinding binding;
  binding.Bind(first, HandleAt(0));
  binding.Bind(second, HandleAt(0));
  EXPECT_FALSE(binding.TabForEntry(first).has_value());
  EXPECT_EQ(HandleAt(0), *binding.TabForEntry(second));
}

TEST_F(TabBindingTest, AClosedTabsHandleResolvesToNothing) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  const tabs::TabHandle handle = HandleAt(0);
  TabBinding binding;
  binding.Bind(id, handle);
  browser()->tab_strip_model()->CloseWebContentsAt(0, 0);
  // The map may still hold the handle; what matters is that it no longer
  // resolves to a tab, so callers see the entry as cold.
  const std::optional<tabs::TabHandle> bound = binding.TabForEntry(id);
  EXPECT_TRUE(!bound.has_value() || bound->Get() == nullptr);
}

}  // namespace
}  // namespace arcium
