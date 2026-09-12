// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/browser/profile_browsertest_base.h"

#include <memory>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace arcium::test {

namespace {

std::unique_ptr<net::test_server::HttpResponse> ServeProfilePage(
    const net::test_server::HttpRequest& request) {
  if (!request.relative_url.starts_with("/arcium/")) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(
      "<!doctype html><title>profile</title>"
      "<a id=\"same\" href=\"/arcium/page?same\">same</a> "
      "<a id=\"blank\" target=\"_blank\" href=\"/arcium/page?blank\">blank</a>");
  return response;
}

}  // namespace

ProfileBrowserTest::ProfileBrowserTest() = default;
ProfileBrowserTest::~ProfileBrowserTest() = default;

void ProfileBrowserTest::SetUpOnMainThread() {
  InProcessBrowserTest::SetUpOnMainThread();
  host_resolver()->AddRule("*", "127.0.0.1");
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeProfilePage));
  ASSERT_TRUE(embedded_test_server()->Start());
  // Every test mutates the model; a load landing afterwards would replace
  // what the test had put there.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return state()->model_load_finished(); }));
}

ArciumProfileState* ProfileBrowserTest::state() {
  return ArciumProfileState::GetForBrowserContext(browser()->GetProfile());
}

ArciumModel* ProfileBrowserTest::model() {
  return state()->model();
}

SpaceSwitcher* ProfileBrowserTest::switcher() {
  return SpaceSwitcher::FromTabStripModel(browser()->tab_strip_model());
}

TabStripModel* ProfileBrowserTest::strip() {
  return browser()->tab_strip_model();
}

content::WebContents* ProfileBrowserTest::active() {
  return strip()->GetActiveWebContents();
}

SpaceId ProfileBrowserTest::AddSpaceOnNewProfile(const std::u16string& name,
                                                 ProfileId* profile_out) {
  *profile_out = model()->AddProfile(name, /*color=*/1);
  const SpaceId space = model()->AddSpace(name, *profile_out);
  switcher()->SwitchTo(space);
  return space;
}

GURL ProfileBrowserTest::PageUrl(std::string_view host,
                                 std::string_view query) {
  return embedded_test_server()->GetURL(
      std::string(host), base::StrCat({"/arcium/page?", query}));
}

std::string ProfileBrowserTest::PartitionOf(content::WebContents* contents) {
  return PartitionDomainOfTab(contents);
}

void ProfileBrowserTest::SetCookie(content::WebContents* contents,
                                   std::string_view value,
                                   bool session_only) {
  ASSERT_TRUE(content::ExecJs(
      contents, base::StrCat({"document.cookie = 'who=", value,
                              session_only ? "'" : "; max-age=86400'"})));
}

std::string ProfileBrowserTest::ReadCookie(content::WebContents* contents) {
  return content::EvalJs(contents, "document.cookie").ExtractString();
}

content::WebContents* ProfileBrowserTest::FindTab(const GURL& url) {
  for (int i = 0; i < strip()->count(); ++i) {
    if (strip()->GetWebContentsAt(i)->GetVisibleURL() == url) {
      return strip()->GetWebContentsAt(i);
    }
  }
  return nullptr;
}

void ProfileBrowserTest::RestoreSessionAtNextLaunch() {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
}

void ProfileBrowserTest::FlushSessionAndModel() {
  // A tab's space and profile reach the session file only on a rebuild,
  // which the sidebar asks for through a posted, coalesced nudge; the model
  // file is written by its own store, which flushes when the profile goes.
  base::RunLoop().RunUntilIdle();
}

}  // namespace arcium::test
