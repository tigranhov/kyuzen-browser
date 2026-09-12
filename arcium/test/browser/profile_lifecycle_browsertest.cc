// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/performance_manager/public/mojom/lifecycle.mojom.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"

namespace arcium::test {
namespace {

using ProfileLifecycleTest = ProfileBrowserTest;

// Chromium throws a background tab away under memory pressure and builds a
// new contents for it; without the hook that contents is in the default
// partition, and the tab comes back logged out.
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest,
                       ADiscardedTabComesBackInItsProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();

  // A tab on screen is never discarded; leave it for another one.
  switcher()->SwitchTo(model()->default_space_id());

  resource_coordinator::TabLifecycleUnitExternal* unit =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
          strip()->GetWebContentsAt(index));
  ASSERT_TRUE(unit);
  ASSERT_TRUE(unit->DiscardTab(mojom::LifecycleUnitDiscardReason::PROACTIVE));

  content::WebContents* replacement = strip()->GetWebContentsAt(index);
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(replacement));

  content::TestNavigationObserver observer(replacement);
  strip()->ActivateTabAt(index);
  observer.Wait();
  EXPECT_EQ("who=work", ReadCookie(replacement));
}

}  // namespace
}  // namespace arcium::test
