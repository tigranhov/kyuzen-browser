// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_SESSION_TAB_ENTRY_H_
#define ARCIUM_BROWSER_SESSION_TAB_ENTRY_H_

#include <map>
#include <string>

#include "components/sessions/core/session_id.h"

namespace content {
class WebContents;
}

namespace sessions {
class CommandStorageManager;
}

namespace arcium {

// Carries the entry id of a pinned or favourite tab across a restart, so the
// entry comes back warm rather than opening a second copy of its page when
// clicked.
//
// It cannot ride on SessionID: SessionTabHelper assigns SessionID::NewUnique()
// in its constructor unconditionally, and SessionIdGenerator's
// SetHighestRestoredID — the API that would make restored ids survive — has no
// production caller. Chromium's per-tab session extra_data does survive, so
// the id travels there.
inline constexpr char kEntryIdExtraDataKey[] = "arcium.entry_id";

// Restore, first half. Called while the restored WebContents is still a
// unique_ptr with no place in any tab strip, so there is no tabs::TabHandle to
// bind yet; the id is parked on the WebContents instead. A missing or
// malformed key parks nothing.
void StashRestoredEntryId(content::WebContents* web_contents,
                          const std::map<std::string, std::string>& extra_data);

// Restore, second half. Called once the tab is in the strip and a handle
// exists. Binds only when the model still holds the stashed entry — an id
// naming nothing means the tab is a Today tab, which is a correct outcome and
// not an error. Clears the stash on every path, so a tab cannot be rebound
// later by a leftover.
void BindStashedEntryId(content::WebContents* web_contents);

// Save. Appends the entry id for `web_contents` as a rebuild command, or
// nothing when no live entry claims the tab.
//
// The sink is passed rather than the SessionService because
// SessionService::AddTabExtraData early-returns unless the window is already
// in windows_tracking_, and on a command rebuild — the path this exists for —
// the window is only added after the loop over its tabs. Appending the
// command directly is what every other per-tab command in that loop does.
void AppendTabEntryCommand(
    sessions::CommandStorageManager* command_storage_manager,
    SessionID tab_id,
    content::WebContents* web_contents);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_SESSION_TAB_ENTRY_H_
