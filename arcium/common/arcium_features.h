// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_COMMON_ARCIUM_FEATURES_H_
#define ARCIUM_COMMON_ARCIUM_FEATURES_H_

#include "base/feature_list.h"
#include "base/time/clock.h"
#include "base/time/time.h"

namespace arcium::features {

// The sidebar-first window layout. When disabled the window is stock Chromium.
BASE_DECLARE_FEATURE(kArciumSidebar);

// The pinned / favourite home boundary: a cross-host link click in an entry's
// tab opens a new tab instead of navigating the entry away from its home.
// Enabled by default; --disable-features=ArciumHomeBoundary turns it off for
// a side-by-side comparison against plain Chromium behaviour.
BASE_DECLARE_FEATURE(kArciumHomeBoundary);

// Command line switch that turns the sidebar off for one run, for debugging.
inline constexpr char kNoSidebarSwitch[] = "arcium-no-sidebar";

// Debugging: --arcium-snapshot=<file.png> paints the first browser window's
// Views (not the page) to a PNG a few seconds after it opens, then continues.
// Screen capture needs a macOS permission that automated runs do not have.
inline constexpr char kSnapshotSwitch[] = "arcium-snapshot";
// Seconds to wait before the snapshot (default 4), so a script can set up
// tabs over DevTools first.
inline constexpr char kSnapshotDelaySwitch[] = "arcium-snapshot-delay";

// Debugging: --arcium-quick-entry opens the Cmd+T quick entry once the window
// is up, so the bubble can be exercised where synthetic key presses are not
// available (macOS withholds them from automated sessions).
inline constexpr char kQuickEntrySwitch[] = "arcium-quick-entry";

// Debugging: --arcium-fake-clock-offset=13h moves the clock the *archive
// service* reads that far forward, so a Today tab looks idle without anyone
// waiting half a day for it. The value is a base::TimeDeltaFromString
// duration ("13h", "2d", "1h30m").
//
// Deliberately not base::Time::Now() for the process. Moving that would move
// it under the model store's writes, under the archive rows' own timestamps
// and under session restore's last-active times — every record the archive
// acceptance pass exists to check. Only the one comparison that asks "has
// this tab been idle long enough" is offset; see ArchiveService's `clock`.
inline constexpr char kFakeClockOffsetSwitch[] = "arcium-fake-clock-offset";

// The offset the switch asks for, or zero when it is absent, unparseable,
// negative or infinite: the switch means "pretend this much time has passed",
// and the other three are not that. Read from the command line on each call,
// which happens once per window.
base::TimeDelta FakeClockOffset();

// A clock reading `offset` ahead of base::Time::Now(). With a zero offset it
// is base::Time::Now() exactly, so the window that owns one need not know
// whether the switch was given. Stateless, and safe to read from the sequence
// that owns it.
class OffsetClock : public base::Clock {
 public:
  explicit OffsetClock(base::TimeDelta offset);
  ~OffsetClock() override;

  // base::Clock:
  base::Time Now() const override;

 private:
  const base::TimeDelta offset_;
};

// True when the sidebar layout should be used for normal tabbed windows.
bool IsSidebarEnabled();

// True when a link click that leaves an entry's home should open a new tab.
bool IsHomeBoundaryEnabled();

// macOS immersive fullscreen moves top chrome into a separate overlay window.
// Arcium hides the tab strip and toolbar, so that overlay would be zero-sized,
// which is unsupported (it DCHECKs). Plain fullscreen keeps the sidebar and
// gives the page the rest of the screen.
bool UsesImmersiveFullscreen();

}  // namespace arcium::features

#endif  // ARCIUM_COMMON_ARCIUM_FEATURES_H_
