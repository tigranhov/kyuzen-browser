// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SESSION_REBUILD_NUDGE_H_
#define ARCIUM_UI_BROWSER_SESSION_REBUILD_NUDGE_H_

class Profile;

namespace arcium {

class TabBinding;

// Teaches `binding` to ask `profile`'s SessionService for a command rebuild
// whenever the set of warm entries changes.
//
// Without this the entry id would reach the session file only by accident.
// AppendTabEntryCommand runs on a rebuild and nowhere else, and a rebuild is
// automatic only every kWritesPerReset = 250 commands — so pinning a tab and
// quitting a minute later would write nothing and the entry would come back
// cold. That degradation is correct but it would be the common case, which
// reads as a broken feature.
//
// One writer plus this nudge, deliberately, rather than a second write path:
// SessionService::AddTabExtraData is a silent no-op during a rebuild (the
// window is not in windows_tracking_ until after its tabs are walked), and two
// paths over one key is how the two come to disagree.
//
// The rebuild is posted and coalesced, never run inline: the callback fires
// from tab-strip callbacks, closing twenty tabs fires twenty times, and
// ScheduleResetCommands walks every window's every tab.
//
// Idempotent per profile, and safe to call from every window: the coalescing
// state is one object on the profile, and the closure outlives no window
// because it is bound to that object rather than to a controller.
void InstallSessionRebuildNudge(Profile* profile, TabBinding* binding);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SESSION_REBUILD_NUDGE_H_
