// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PROFILE_ACTIONS_H_
#define ARCIUM_UI_BROWSER_PROFILE_ACTIONS_H_

#include "arcium/browser/model/entry_id.h"
// So one include gives a caller all three of the menu's operations.
#include "arcium/browser/profile_data.h"

namespace content {
class BrowserContext;
}

namespace arcium {

// Puts `space` on `profile` and reopens every open tab of that space, in
// every window, so each one is logged in as the new profile. Does nothing
// for an unknown space or profile, for a space already on `profile`, or off
// the record.
void MoveSpaceToProfile(content::BrowserContext* context,
                        SpaceId space,
                        ProfileId profile);

// Erases `profile`: its spaces move to Default, which reopens their tabs
// logged out, it is removed from the model, and its stored logins and site
// data are deleted. A profile nothing opened this session goes at once,
// cache and all; one that was opened is emptied now and its folder is
// removed at a later launch, when Chrome's own partition cleanup runs.
// Refuses Default.
void DeleteArciumProfile(content::BrowserContext* context, ProfileId profile);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PROFILE_ACTIONS_H_
