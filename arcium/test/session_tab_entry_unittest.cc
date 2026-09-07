// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/session_tab_entry.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/command_storage_manager_delegate.h"
#include "components/sessions/core/session_command.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// A real CommandStorageManager is used rather than a fake: the production code
// hands it a command built by Chromium's own factory, and the only honest way
// to check the encoding is to read back what Chromium put in the queue. The
// save timer is never started, so nothing is written.
class NoSaveDelegate : public sessions::CommandStorageManagerDelegate {
 public:
  bool ShouldUseDelayedSave() override { return true; }
  void OnErrorWritingSessionCommands() override {}
};

std::string PayloadOf(const sessions::SessionCommand& command) {
  const base::span<const uint8_t> contents = command.contents();
  return std::string(reinterpret_cast<const char*>(contents.data()),
                     contents.size());
}

class SessionTabEntryTest : public BrowserWithTestWindowTest {
 public:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    os_crypt_async_ = os_crypt_async::GetTestOSCryptAsyncForTesting(true);
    command_storage_manager_ =
        std::make_unique<sessions::CommandStorageManager>(
            sessions::CommandStorageManager::SessionType::kSessionRestore,
            temp_dir_.GetPath(), &delegate_, os_crypt_async_.get(),
            sessions::CommandStorageManager::CreateDefaultBackendTaskRunner());
  }

  void TearDown() override {
    command_storage_manager_.reset();
    task_environment()->RunUntilIdle();
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  ArciumProfileState* state() {
    return ArciumProfileState::GetForBrowserContext(profile());
  }

  content::WebContents* ContentsAt(int index) {
    return browser()->tab_strip_model()->GetWebContentsAt(index);
  }

  tabs::TabInterface* TabAt(int index) {
    return browser()->tab_strip_model()->GetTabAtIndex(index);
  }

  tabs::TabHandle HandleAt(int index) {
    return browser()->tab_strip_model()->GetTabAtIndex(index)->GetHandle();
  }

  size_t pending_count() {
    return command_storage_manager_->pending_commands().size();
  }

  base::ScopedTempDir temp_dir_;
  NoSaveDelegate delegate_;
  std::unique_ptr<os_crypt_async::OSCryptAsync> os_crypt_async_;
  std::unique_ptr<sessions::CommandStorageManager> command_storage_manager_;
};

// The whole point of the task: a key naming an entry the model still holds
// brings the entry back warm, bound to the tab session restore just made.
TEST_F(SessionTabEntryTest, AKeyNamingALiveEntryBindsTheRestoredTab) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");

  StashRestoredEntryId(ContentsAt(0), {{kEntryIdExtraDataKey, id.value()}});
  BindStashedEntryId(ContentsAt(0));

  ASSERT_TRUE(state()->binding()->TabForEntry(id).has_value());
  EXPECT_EQ(HandleAt(0), *state()->binding()->TabForEntry(id));
  EXPECT_TRUE(
      IsClaimedByEntry(*state()->model(), *state()->binding(), HandleAt(0)));
}

// The degradation the commit message promises: an id the model no longer has
// is not an error, it is a Today tab. Binding it anyway is the invisible-tab
// bug this stage has already shipped once.
TEST_F(SessionTabEntryTest, AKeyNamingNoEntryLeavesTheTabInToday) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId absent = EntryId::Generate();

  StashRestoredEntryId(ContentsAt(0), {{kEntryIdExtraDataKey, absent.value()}});
  BindStashedEntryId(ContentsAt(0));

  EXPECT_FALSE(state()->binding()->IsBound(HandleAt(0)));
  EXPECT_FALSE(
      IsClaimedByEntry(*state()->model(), *state()->binding(), HandleAt(0)));
}

// EntryId::FromString returns an invalid id for anything that is not a
// lowercase UUID, so a truncated or hand-edited session file lands here.
TEST_F(SessionTabEntryTest, AMalformedKeyBindsNothing) {
  AddTab(browser(), GURL("https://a.example/"));

  StashRestoredEntryId(ContentsAt(0),
                       {{kEntryIdExtraDataKey, "not-a-uuid-at-all"}});
  BindStashedEntryId(ContentsAt(0));

  EXPECT_FALSE(state()->binding()->IsBound(HandleAt(0)));
}

// Every tab Chromium restores comes through here, and almost none of them
// carry the key.
TEST_F(SessionTabEntryTest, AnAbsentKeyBindsNothing) {
  AddTab(browser(), GURL("https://a.example/"));

  StashRestoredEntryId(ContentsAt(0), {{"some.other.key", "value"}});
  BindStashedEntryId(ContentsAt(0));

  EXPECT_FALSE(state()->binding()->IsBound(HandleAt(0)));
}

// The stash is a one-shot. Left behind, it would rebind a tab the user has
// since detached from its entry, at whatever later moment something else
// calls in.
TEST_F(SessionTabEntryTest, TheStashIsClearedAfterBinding) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");

  StashRestoredEntryId(ContentsAt(0), {{kEntryIdExtraDataKey, id.value()}});
  BindStashedEntryId(ContentsAt(0));
  ASSERT_TRUE(state()->binding()->IsBound(HandleAt(0)));

  state()->binding()->UnbindTab(HandleAt(0));
  BindStashedEntryId(ContentsAt(0));

  EXPECT_FALSE(state()->binding()->IsBound(HandleAt(0)));
}

// The path that did not bind must clear the stash too: the model finishes
// loading after the first window is interactive, so an entry can appear
// moments later, and a kept stash would bind a tab the user has been using
// as a Today tab.
TEST_F(SessionTabEntryTest, TheStashIsClearedWhenTheEntryWasMissing) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  const TabEntry saved = *state()->model()->GetEntry(id);
  state()->model()->RemoveEntry(id);

  StashRestoredEntryId(ContentsAt(0), {{kEntryIdExtraDataKey, id.value()}});
  BindStashedEntryId(ContentsAt(0));
  ASSERT_FALSE(state()->binding()->IsBound(HandleAt(0)));

  // The id names a real entry again — which is what a model load arriving
  // after the window is interactive looks like. The stale stash must not
  // resurrect and steal a tab the user has been treating as a Today tab.
  state()->model()->ReplaceAll(state()->model()->spaces(), {}, {saved});
  ASSERT_TRUE(state()->model()->GetEntry(id));
  BindStashedEntryId(ContentsAt(0));

  EXPECT_FALSE(state()->binding()->IsBound(HandleAt(0)));
}

// A malformed key stores nothing at all, so a later call finds no stash even
// though one was offered.
TEST_F(SessionTabEntryTest, AMalformedKeyLeavesNoStash) {
  AddTab(browser(), GURL("https://a.example/"));

  StashRestoredEntryId(ContentsAt(0), {{kEntryIdExtraDataKey, "nonsense"}});
  BindStashedEntryId(ContentsAt(0));
  BindStashedEntryId(ContentsAt(0));

  EXPECT_FALSE(state()->binding()->IsBound(HandleAt(0)));
}

// The write half. One command, carrying the key and the entry's own id, for
// the tab id the session file will use.
TEST_F(SessionTabEntryTest, ABoundTabAppendsExactlyOneExtraDataCommand) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  state()->binding()->Bind(id, HandleAt(0));

  const SessionID tab_id = SessionID::NewUnique();
  AppendTabEntryCommand(command_storage_manager_.get(), tab_id, ContentsAt(0));

  ASSERT_EQ(1u, pending_count());
  const std::string payload =
      PayloadOf(*command_storage_manager_->pending_commands()[0]);
  EXPECT_NE(std::string::npos, payload.find(kEntryIdExtraDataKey));
  EXPECT_NE(std::string::npos, payload.find(id.value()));
}

// Most tabs are Today tabs and must leave the session file alone.
//
// A live entry claiming the *other* tab, so the profile state exists and has
// something to say — just not about this tab. Without it the writer returns
// at its no-state guard and the test proves nothing.
TEST_F(SessionTabEntryTest, AnUnboundTabAppendsNothing) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  const EntryId other = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://b.example/"), u"B");
  state()->binding()->Bind(other, HandleAt(1));

  AppendTabEntryCommand(command_storage_manager_.get(), SessionID::NewUnique(),
                        ContentsAt(0));

  EXPECT_EQ(0u, pending_count());
}

// The stale binding IsClaimedByEntry exists for: bound, but the model dropped
// the entry. Writing the key here would resurrect a dead entry's claim on the
// next restart.
TEST_F(SessionTabEntryTest, ATabBoundToAVanishedEntryAppendsNothing) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  // Bound behind the model's back, exactly as ReplaceAll leaves it.
  state()->binding()->Bind(id, HandleAt(0));
  state()->model()->RemoveEntry(id);
  state()->binding()->Bind(id, HandleAt(0));
  ASSERT_TRUE(state()->binding()->IsBound(HandleAt(0)));

  AppendTabEntryCommand(command_storage_manager_.get(), SessionID::NewUnique(),
                        ContentsAt(0));

  EXPECT_EQ(0u, pending_count());
}

// The in-session close. BrowserLiveTabContext::GetExtraDataForTab builds the
// closed tab's extra_data from scratch, so a key the session file already
// carries is not reused; without a contribution here the reopened tab arrives
// with no id at all and becomes an unclaimed Today row while its entry stays
// cold — two sidebar rows for one page.
TEST_F(SessionTabEntryTest, AClaimedTabContributesItsEntryIdToExtraData) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  state()->binding()->Bind(id, HandleAt(0));

  std::map<std::string, std::string> extra_data;
  PopulateTabEntryExtraData(TabAt(0), &extra_data);

  ASSERT_TRUE(extra_data.contains(kEntryIdExtraDataKey));
  EXPECT_EQ(id.value(), extra_data[kEntryIdExtraDataKey]);
}

// The two halves have to agree on the encoding, and nothing else checks that
// they do: the write half is exercised through a session command and the read
// half through a hand-built map. Here one feeds the other.
TEST_F(SessionTabEntryTest, TheExtraDataItWritesIsTheExtraDataItReads) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  state()->binding()->Bind(id, HandleAt(0));

  std::map<std::string, std::string> extra_data;
  PopulateTabEntryExtraData(TabAt(0), &extra_data);

  // The reopened tab is a different tab, which is the whole difficulty: the
  // entry has to follow the id, not the handle.
  StashRestoredEntryId(ContentsAt(1), extra_data);
  BindStashedEntryId(ContentsAt(1));

  ASSERT_TRUE(state()->binding()->TabForEntry(id).has_value());
  EXPECT_EQ(HandleAt(1), *state()->binding()->TabForEntry(id));
}

// Closing a Today tab must leave the map exactly as upstream left it. Same
// staging as AnUnboundTabAppendsNothing, and for the same reason: an entry on
// the other tab, so the writer gets past its no-state guard and has to decide
// about this tab on the merits.
TEST_F(SessionTabEntryTest, AnUnboundTabContributesNoExtraData) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  const EntryId other = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://b.example/"), u"B");
  state()->binding()->Bind(other, HandleAt(1));

  std::map<std::string, std::string> extra_data;
  PopulateTabEntryExtraData(TabAt(0), &extra_data);

  EXPECT_FALSE(extra_data.contains(kEntryIdExtraDataKey));
}

// The stale binding again: bound, but the model has dropped the entry. Its id
// must not be written down, here or anywhere.
TEST_F(SessionTabEntryTest, ATabBoundToAVanishedEntryContributesNoExtraData) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  state()->binding()->Bind(id, HandleAt(0));
  state()->model()->RemoveEntry(id);
  // Bound behind the model's back, exactly as ReplaceAll leaves it.
  state()->binding()->Bind(id, HandleAt(0));
  ASSERT_TRUE(state()->binding()->IsBound(HandleAt(0)));

  std::map<std::string, std::string> extra_data;
  PopulateTabEntryExtraData(TabAt(0), &extra_data);

  EXPECT_FALSE(extra_data.contains(kEntryIdExtraDataKey));
}

// Both session-side writers run for every tab of every window on a command
// rebuild and on every tab close. Reaching for the profile state constructs
// it, and construction posts an archive open and a model load — so the
// session path would be doing that work on any profile that had not already
// been through the sidebar. It happens to be benign today only because the
// sidebar controller always gets there first; that is an ordering, not a
// guarantee.
TEST_F(SessionTabEntryTest, TheSessionPathConstructsNoProfileState) {
  AddTab(browser(), GURL("https://a.example/"));
  ASSERT_EQ(nullptr,
            ArciumProfileState::GetForBrowserContextIfExists(profile()));

  AppendTabEntryCommand(command_storage_manager_.get(), SessionID::NewUnique(),
                        ContentsAt(0));
  std::map<std::string, std::string> extra_data;
  PopulateTabEntryExtraData(TabAt(0), &extra_data);

  EXPECT_EQ(nullptr,
            ArciumProfileState::GetForBrowserContextIfExists(profile()));
  EXPECT_EQ(0u, pending_count());
  EXPECT_TRUE(extra_data.empty());
}

// Incognito is correct by construction — its own empty model, its own
// binding, no store — but this is the one path where a restore could
// plausibly write regular-profile state, and writing incognito browsing state
// into the regular profile is a bug this stage has shipped once already. So
// it gets a test rather than an argument.
TEST_F(SessionTabEntryTest, AnIncognitoTabNeitherBindsNorWritesRegularState) {
  // A live pinned entry on the REGULAR profile, named by the incognito tab's
  // extra_data. Nothing incognito may reach it.
  const EntryId id = state()->model()->AddEntry(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");

  Profile* otr = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ASSERT_TRUE(otr->IsOffTheRecord());
  std::unique_ptr<Browser> otr_browser =
      CreateBrowser(otr, Browser::TYPE_NORMAL, /*hosted_app=*/false);
  AddTab(otr_browser.get(), GURL("https://a.example/"));
  content::WebContents* otr_contents =
      otr_browser->tab_strip_model()->GetWebContentsAt(0);
  tabs::TabInterface* otr_tab =
      otr_browser->tab_strip_model()->GetTabAtIndex(0);

  StashRestoredEntryId(otr_contents, {{kEntryIdExtraDataKey, id.value()}});
  BindStashedEntryId(otr_contents);

  // The regular profile's entry stays cold, and the incognito tab is a Today
  // tab in its own window.
  EXPECT_FALSE(state()->binding()->TabForEntry(id).has_value());
  ArciumProfileState* otr_state = ArciumProfileState::GetForBrowserContext(otr);
  EXPECT_NE(state(), otr_state);
  EXPECT_EQ(nullptr, otr_state->store());
  EXPECT_FALSE(otr_state->binding()->IsBound(otr_tab->GetHandle()));

  // And nothing incognito is written down anywhere it could outlive the
  // window.
  std::map<std::string, std::string> extra_data;
  PopulateTabEntryExtraData(otr_tab, &extra_data);
  AppendTabEntryCommand(command_storage_manager_.get(), SessionID::NewUnique(),
                        otr_contents);

  EXPECT_TRUE(extra_data.empty());
  EXPECT_EQ(0u, pending_count());

  otr_browser->tab_strip_model()->CloseAllTabs();
}

}  // namespace
}  // namespace arcium
