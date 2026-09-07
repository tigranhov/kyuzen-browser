// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_binding.h"

#include "arcium/browser/model/entry_id.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
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

// The changed callback is what carries "the set of warm entries just moved"
// out of this target, which cannot reach the session service itself. One call
// per real change: Bind releases both sides first, and reporting that as three
// changes would cost three session command rebuilds.
TEST_F(TabBindingTest, AChangeIsReportedOncePerBind) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  TabBinding binding;
  int changes = 0;
  binding.SetChangedCallback(
      base::BindLambdaForTesting([&changes] { ++changes; }));

  binding.Bind(EntryId::Generate(), HandleAt(0));
  EXPECT_EQ(1, changes);

  // Displaces the first entry and the first tab's binding at once; still one
  // change, not three.
  binding.Bind(EntryId::Generate(), HandleAt(0));
  EXPECT_EQ(2, changes);
}

// An unbind that unbinds nothing is not a change. Tab closes come through
// here for every Today tab, which is most of them.
TEST_F(TabBindingTest, AnUnbindThatChangesNothingIsNotReported) {
  AddTab(browser(), GURL("https://a.example/"));
  TabBinding binding;
  int changes = 0;
  binding.SetChangedCallback(
      base::BindLambdaForTesting([&changes] { ++changes; }));

  binding.UnbindTab(HandleAt(0));
  binding.UnbindEntry(EntryId::Generate());

  EXPECT_EQ(0, changes);
}

// Session restore binds every warm entry in a row, during startup, and
// Chromium rewrites the session file when the restore finishes.
TEST_F(TabBindingTest, SuppressionSilencesTheChangeCallback) {
  AddTab(browser(), GURL("https://a.example/"));
  TabBinding binding;
  int changes = 0;
  binding.SetChangedCallback(
      base::BindLambdaForTesting([&changes] { ++changes; }));

  {
    TabBinding::ScopedChangeSuppression suppress(&binding);
    binding.Bind(EntryId::Generate(), HandleAt(0));
    EXPECT_EQ(0, changes);
  }
  // And the suppression ends with its scope, rather than latching off.
  binding.Bind(EntryId::Generate(), HandleAt(0));
  EXPECT_EQ(1, changes);
}

}  // namespace
}  // namespace arcium
