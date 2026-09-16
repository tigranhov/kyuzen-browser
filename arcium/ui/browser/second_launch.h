// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SECOND_LAUNCH_H_
#define ARCIUM_UI_BROWSER_SECOND_LAUNCH_H_

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace arcium {

// A launch of a browser that is already running: opening the application a
// second time, or running it again from a terminal. Chromium's answer is a
// second window; Arcium's is the window it already has, because one window
// holding one strip is the shape the sidebar, the spaces and the archive are
// all built on, and a second window is a second copy of all of it.
//
// Raises that window, and opens any addresses on the command line as tabs in
// it -- in the space a routing rule names, the same as an address typed into
// the box.
//
// True when Arcium took the launch, and then the caller does nothing more.
// False -- no window with a sidebar yet, or a command line asking for
// something other than ordinary browsing -- and the caller does what Chromium
// always has.
bool OpenSecondLaunchInExistingWindow(const base::CommandLine& command_line,
                                      const base::FilePath& cur_dir);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SECOND_LAUNCH_H_
