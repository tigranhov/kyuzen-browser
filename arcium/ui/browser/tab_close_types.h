// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_TAB_CLOSE_TYPES_H_
#define ARCIUM_UI_BROWSER_TAB_CLOSE_TYPES_H_

#include <cstdint>

#include "chrome/browser/ui/tabs/tab_enums.h"

namespace arcium {

// How Arcium closes a tab, in one place. There were three copies of this
// before: one on SidebarTabModel whose own comment said it lived there so two
// copies could not drift apart, and one in ArchiveService's anonymous
// namespace that had already drifted. The consequence was visible — pressing
// Clear set SetClosedByUserGesture on a profile with no archive and not on one
// with an archive, so one button meant two things.
//
// The two values differ for a reason that is now stated rather than implied,
// and they sit beside each other so a change to one is read against the other.

// Every close that happens because the user asked for it: the row's close
// button, unpinning a tab away, and Clear on both of its paths. Historical so
// Cmd+Shift+T brings it back, and a user gesture because it was one.
inline constexpr uint32_t kUserCloseTypes =
    TabCloseTypes::CLOSE_USER_GESTURE |
    TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB;

// The automatic idle sweep. Still historical — a tab archived while the user
// was elsewhere is exactly the tab they will want back — but no gesture closed
// it, and claiming one would put a tab the user never touched into the
// bookkeeping that records what they did.
inline constexpr uint32_t kSweepCloseTypes =
    TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB;

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_TAB_CLOSE_TYPES_H_
