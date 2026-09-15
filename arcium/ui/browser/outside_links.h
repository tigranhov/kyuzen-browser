// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_OUTSIDE_LINKS_H_
#define ARCIUM_UI_BROWSER_OUTSIDE_LINKS_H_

#include <vector>

class GURL;

namespace arcium {

// Links another application handed the running browser (R4.3): a click in
// Slack, Mail or a terminal while Arcium is the default browser. Opens one
// small window per link, in the space a routing rule names or the space on
// screen.
//
// True when Arcium took them, and then the caller does nothing more. False --
// the feature off, no window with a sidebar yet, or anything that is not an
// http or https page -- and the caller opens them the way Chromium always
// has, as tabs. A cold launch therefore still opens a tab, because no window
// exists when the links arrive.
bool OpenOutsideLinks(const std::vector<GURL>& urls);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_OUTSIDE_LINKS_H_
