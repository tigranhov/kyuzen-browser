// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_LAST_SIDEBAR_WINDOW_H_
#define ARCIUM_UI_BROWSER_LAST_SIDEBAR_WINDOW_H_

class BrowserView;

namespace arcium {

// The window the reader was last in that has a sidebar. That is the window a
// link from outside the browser belongs beside, the window a second launch
// raises, and the window whose space "the space on screen" means. Activation
// order is the right order for all three. Null before any such window exists,
// which is what a cold launch looks like.
BrowserView* LastActiveSidebarWindow();

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_LAST_SIDEBAR_WINDOW_H_
