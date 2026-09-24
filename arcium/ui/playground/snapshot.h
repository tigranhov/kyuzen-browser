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
// copies the window's composited frame a moment after it is shown, writes the
// part covering `view` as a PNG, and exits. If the compositor hands over no
// frame, it paints the views offscreen at 2x instead, which leaves out
// anything drawn on a layer of its own. Screen capture needs a macOS
// permission; neither does, and they produce the images in docs/screens/.
inline constexpr char kSnapshotSwitch[] = "snapshot";
// --snapshot-size=<width>x<height>: the window size for the snapshot, 1100x720
// when absent.
inline constexpr char kSnapshotSizeSwitch[] = "snapshot-size";

// Schedules the snapshot if the switch is present. No-op otherwise.
void MaybeScheduleSnapshot(views::View* view);

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_SNAPSHOT_H_
