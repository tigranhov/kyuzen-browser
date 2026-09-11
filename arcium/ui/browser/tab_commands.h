// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_TAB_COMMANDS_H_
#define ARCIUM_UI_BROWSER_TAB_COMMANDS_H_

class Browser;

namespace arcium {

// The strip-wide tab commands -- next and previous, the Ctrl+Tab cycle,
// Cmd+1..8 and Cmd+9, moving a tab, closing it, close-others and
// close-to-the-right -- answered within the space the window is showing
// rather than across the whole strip, which holds every space's tabs.
//
// True when Arcium handled `command_id` and Chromium must not; false for a
// command Arcium does not own, for a window without a sidebar (it has no
// spaces), and for a close Chromium's own close already does right.
bool HandleTabCommand(Browser* browser, int command_id);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_TAB_COMMANDS_H_
