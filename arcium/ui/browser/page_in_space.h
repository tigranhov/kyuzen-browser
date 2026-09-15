// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PAGE_IN_SPACE_H_
#define ARCIUM_UI_BROWSER_PAGE_IN_SPACE_H_

#include "arcium/browser/loose_page.h"
#include "arcium/browser/model/entry_id.h"

class Browser;
class GURL;

namespace content {
class WebContents;
struct OpenURLParams;
}  // namespace content

namespace arcium {

// Opens `params.url` in a new tab that belongs to `space`: built with that
// space's storage, tagged with it, marked `kind`, and inserted at the end of
// the strip without being activated. Marked before insertion, so nothing that
// listens to insertion ever sees a loose page unmarked. Returns the page, or
// null when the window has no Arcium state or no such space.
//
// Chromium's own navigator is not used because it picks storage from the
// space on screen, and a routed link or a peek belongs to a space that may
// not be.
content::WebContents* OpenPageInSpace(Browser* browser,
                                      SpaceId space,
                                      const content::OpenURLParams& params,
                                      LoosePageKind kind);

// Makes the page an ordinary tab and puts it on screen: clears its loose
// marker, moves it to the end of the strip, switches the window to its space
// and activates it. The page keeps its history, scroll and logins, because it
// never stopped being a tab.
void BringPageToScreen(Browser* browser, content::WebContents* contents);

// Opens `url` as a new tab in the space a routing rule sends it to, switching
// the window there, or in the space on screen when no rule covers it. For the
// tabs the reader asks for -- a row chosen in the command box -- and never for
// one a page opens, which keeps its opener's space and storage.
void OpenUrlInRoutedSpace(Browser* browser, const GURL& url);

// Closes the page's tab. A page with a beforeunload handler may ask first.
void ClosePage(Browser* browser, content::WebContents* contents);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PAGE_IN_SPACE_H_
