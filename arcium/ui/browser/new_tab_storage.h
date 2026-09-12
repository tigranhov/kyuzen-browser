// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_NEW_TAB_STORAGE_H_
#define ARCIUM_UI_BROWSER_NEW_TAB_STORAGE_H_

#include "arcium/browser/model/entry_id.h"
#include "base/memory/scoped_refptr.h"

class BrowserWindowInterface;
class GURL;

namespace content {
class SiteInstance;
class WebContents;
}  // namespace content

namespace arcium {

class ArciumModel;
class SpaceSwitcher;
class TabBinding;

// Which space, and so which storage, a tab Chromium is about to create
// belongs to. The same rule SpaceSwitcher::TagInsertedTabs applies when a
// tab is inserted -- the tab it was opened from, else the space on screen --
// applied at creation instead, because a tab's storage is fixed then and can
// never change afterwards. Invalid in a window without a sidebar.
SpaceId SpaceForNewTab(const ArciumModel& model,
                       const TabBinding& binding,
                       const SpaceSwitcher* switcher,
                       content::WebContents* source);

// The hook in CreateTargetContents (patch 0181): `chromium_choice` unless
// the new tab's space is on a profile with storage of its own.
scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    BrowserWindowInterface* browser,
    content::WebContents* source,
    const GURL& url,
    scoped_refptr<content::SiteInstance> chromium_choice);

// The rest of the same hook, once the contents exists: tags it with the
// space its storage was chosen for, so the space Arcium records and the
// storage the tab uses cannot disagree. TagInsertedTabs never overwrites a
// tag, so this one stands.
void TagNewTab(BrowserWindowInterface* browser,
               content::WebContents* source,
               content::WebContents* contents);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_NEW_TAB_STORAGE_H_
