// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PROFILE_REOPEN_H_
#define ARCIUM_UI_BROWSER_PROFILE_REOPEN_H_

#include "arcium/browser/model/entry_id.h"

class TabStripModel;

namespace arcium {

// Puts the tab at `index` on `profile`'s storage. A tab's storage is fixed
// when its contents is created, so this makes a new contents at the same
// address, with the same history, and swaps it into the same tab -- the way
// Chromium replaces a tab it has discarded. The tab keeps its place, its
// handle, its pinned or favourite entry, its space tag and its key; the page
// is logged in as `profile` from now on, and anything it had unsaved is
// lost, since no beforeunload runs.
//
// The tab on screen loads again at once. A background tab comes back
// unloaded and loads when it is clicked, as a restored tab does.
//
// Does nothing off the record, where profiles do not apply.
void ReopenTabInProfile(TabStripModel* strip,
                        int index,
                        const ProfileId& profile);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PROFILE_REOPEN_H_
