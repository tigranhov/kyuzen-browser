// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/home_boundary_throttle.h"

#include <memory>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/common/arcium_features.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/mock_navigation_throttle_registry.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace arcium {

namespace {

using ::testing::Return;

constexpr char kHomeUrl[] = "https://mail.google.com/mail/u/0";
constexpr char kElsewhereUrl[] = "https://news.ycombinator.com/";

class HomeBoundaryThrottleTest : public BrowserWithTestWindowTest {
 protected:
  // Opens one tab and binds it to a pinned entry whose stored URL is
  // `kHomeUrl`, which is the state the boundary is about.
  void AddPinnedTab() {
    AddTab(browser(), GURL(kHomeUrl));
    ArciumProfileState* state =
        ArciumProfileState::GetForBrowserContext(profile());
    const EntryId id = state->model()->AddEntryForTesting(
        EntryKind::kPinned, GURL(kHomeUrl), u"Mail");
    state->binding()->Bind(
        id, browser()->tab_strip_model()->GetTabAtIndex(0)->GetHandle());
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetWebContentsAt(0);
  }

  // A handle for a navigation to `url` in tab 0, shaped like a link click
  // unless `link_click` says otherwise, and carrying a user gesture unless
  // `has_user_gesture` says otherwise. The redirect chain is set because
  // OpenURLParams::FromNavigationHandle pops its last entry.
  std::unique_ptr<content::MockNavigationHandle> MakeHandle(
      const char* url,
      bool link_click = true,
      bool has_user_gesture = true) {
    auto handle = std::make_unique<content::MockNavigationHandle>(contents());
    handle->set_url(GURL(url));
    handle->set_is_in_primary_main_frame(true);
    handle->set_redirect_chain({GURL(url)});
    // NavigationControllerImpl::LoadURLWithParams DCHECKs that the
    // initiator origin is set. A real link click always has one -- the
    // origin of the page the link lives on, i.e. the tab's current page.
    handle->set_initiator_origin(
        url::Origin::Create(contents()->GetLastCommittedURL()));
    ON_CALL(*handle, WasInitiatedByLinkClick())
        .WillByDefault(Return(link_click));
    EXPECT_CALL(*handle, WasInitiatedByLinkClick())
        .WillRepeatedly(Return(link_click));
    // A real link click always carries a user gesture. Without this,
    // OpenURLParams::FromNavigationHandle's user_gesture comes back false
    // and the divert's WebContents::OpenURL is silently eaten by the popup
    // blocker (WindowOpenDisposition::NEW_FOREGROUND_TAB is popup-blocking
    // eligible, and ShouldBlockPopup() rejects a gesture-less open outright).
    ON_CALL(*handle, HasUserGesture()).WillByDefault(Return(has_user_gesture));
    EXPECT_CALL(*handle, HasUserGesture())
        .WillRepeatedly(Return(has_user_gesture));
    return handle;
  }
};

TEST_F(HomeBoundaryThrottleTest, ABoundTabGetsAThrottle) {
  AddPinnedTab();
  auto handle = MakeHandle(kElsewhereUrl);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);

  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);

  EXPECT_EQ(1u, registry.throttles().size());
}

TEST_F(HomeBoundaryThrottleTest, AnUnboundTabGetsNoThrottle) {
  // A Today tab is bound to no entry, so the boundary is opt-in and a
  // diverted tab does not inherit one. The profile state is constructed
  // (as it would be by a sidebar a sibling window already opened) so this
  // test exercises EntryForTab's nullopt, not the earlier no-state guard
  // that it would otherwise pass through vacuously. It cannot reach
  // GetEntry's null check: EntryForTab returning nullopt short-circuits
  // MaybeCreateAndAdd before GetEntry is ever called.
  AddTab(browser(), GURL(kHomeUrl));
  ArciumProfileState::GetForBrowserContext(profile());
  auto handle = MakeHandle(kElsewhereUrl);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);

  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);

  EXPECT_TRUE(registry.throttles().empty());
}

TEST_F(HomeBoundaryThrottleTest, ASubframeNavigationGetsNoThrottle) {
  AddPinnedTab();
  auto handle = MakeHandle(kElsewhereUrl);
  handle->set_is_in_primary_main_frame(false);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);

  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);

  EXPECT_TRUE(registry.throttles().empty());
}

TEST_F(HomeBoundaryThrottleTest, AReloadGetsNoThrottle) {
  AddPinnedTab();
  auto handle = MakeHandle(kElsewhereUrl);
  handle->set_reload_type(content::ReloadType::NORMAL);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);

  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);

  EXPECT_TRUE(registry.throttles().empty());
}

TEST_F(HomeBoundaryThrottleTest,
       ADisabledFeatureGetsNoThrottleEvenForABoundTab) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(features::kArciumHomeBoundary);
  AddPinnedTab();
  auto handle = MakeHandle(kElsewhereUrl);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);

  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);

  EXPECT_TRUE(registry.throttles().empty());
}

TEST_F(HomeBoundaryThrottleTest, ACrossHostLinkClickOpensATabAndIsCancelled) {
  AddPinnedTab();
  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  auto handle = MakeHandle(kElsewhereUrl);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);
  ASSERT_EQ(1u, registry.throttles().size());

  const content::NavigationThrottle::ThrottleCheckResult result =
      registry.throttles()[0]->WillStartRequest();

  EXPECT_EQ(content::NavigationThrottle::CANCEL_AND_IGNORE, result.action());
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  // The new tab is the clicked URL, and the entry's own tab -- the one the
  // feature exists to protect -- still shows what it showed before the
  // click, exactly as A2.6.1 checks by hand.
  EXPECT_EQ(GURL(kElsewhereUrl),
            browser()->tab_strip_model()->GetWebContentsAt(1)->GetVisibleURL());
  EXPECT_EQ(GURL(kHomeUrl), contents()->GetLastCommittedURL());
}

TEST_F(HomeBoundaryThrottleTest, ASameHostLinkClickProceeds) {
  AddPinnedTab();
  auto handle = MakeHandle("https://mail.google.com/mail/u/1");
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);
  ASSERT_EQ(1u, registry.throttles().size());

  const content::NavigationThrottle::ThrottleCheckResult result =
      registry.throttles()[0]->WillStartRequest();

  EXPECT_EQ(content::NavigationThrottle::PROCEED, result.action());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

TEST_F(HomeBoundaryThrottleTest, ACrossHostNavigationThatIsNotALinkClickStays) {
  // The whole policy on which navigations count. A redirect, a
  // location.href assignment, a form POST and a typed URL all arrive with
  // this bit false, which is why sign-in survives the boundary.
  AddPinnedTab();
  auto handle = MakeHandle(kElsewhereUrl, /*link_click=*/false);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);
  ASSERT_EQ(1u, registry.throttles().size());

  const content::NavigationThrottle::ThrottleCheckResult result =
      registry.throttles()[0]->WillStartRequest();

  EXPECT_EQ(content::NavigationThrottle::PROCEED, result.action());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

TEST_F(HomeBoundaryThrottleTest, AScriptedLinkClickWithoutAUserGestureStays) {
  // A programmatic anchorElement.click() is still a link click by Blink's
  // classification -- the anchor sets a triggering event -- but it carries
  // no user gesture. Diverting it would cancel the navigation and hand the
  // open to the popup blocker, which drops a gesture-less
  // NEW_FOREGROUND_TAB outright: the visible result would be a navigation
  // that simply never happens, not one that stays in the tab.
  AddPinnedTab();
  auto handle = MakeHandle(kElsewhereUrl, /*link_click=*/true,
                           /*has_user_gesture=*/false);
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);
  ASSERT_EQ(1u, registry.throttles().size());

  const content::NavigationThrottle::ThrottleCheckResult result =
      registry.throttles()[0]->WillStartRequest();

  EXPECT_EQ(content::NavigationThrottle::PROCEED, result.action());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

TEST_F(HomeBoundaryThrottleTest, ATraversalGetsNoDiversion) {
  // A back/forward traversal replays a link click's transition type, so it
  // still passes WasInitiatedByLinkClick() -- this combines
  // PAGE_TRANSITION_LINK with PAGE_TRANSITION_FORWARD_BACK the way a real
  // traversal of a link-click history entry does. The traversal guard in
  // WillStartRequest is the one that has to catch it.
  AddPinnedTab();
  auto handle = MakeHandle(kElsewhereUrl);
  handle->set_page_transition(ui::PageTransitionFromInt(
      ui::PAGE_TRANSITION_LINK | ui::PAGE_TRANSITION_FORWARD_BACK));
  content::MockNavigationThrottleRegistry registry(
      handle.get(),
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  HomeBoundaryThrottle::MaybeCreateAndAdd(registry);
  ASSERT_EQ(1u, registry.throttles().size());

  const content::NavigationThrottle::ThrottleCheckResult result =
      registry.throttles()[0]->WillStartRequest();

  EXPECT_EQ(content::NavigationThrottle::PROCEED, result.action());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

}  // namespace

}  // namespace arcium
