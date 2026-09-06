// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_SNAPSHOT_H_
#define ARCIUM_UI_PLAYGROUND_SNAPSHOT_H_

namespace views {
class View;
}

namespace arcium {

// Command line switch: --snapshot=<path.png>. When present, the example
// paints `view` offscreen at 2x a moment after it is shown, writes the PNG,
// and exits. Screen capture needs a macOS permission; this does not, and it
// produces the images in docs/screens/.
inline constexpr char kSnapshotSwitch[] = "snapshot";

// Schedules the snapshot if the switch is present. No-op otherwise.
void MaybeScheduleSnapshot(views::View* view);

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_SNAPSHOT_H_
