// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_LOOSE_PAGE_H_
#define ARCIUM_BROWSER_LOOSE_PAGE_H_

namespace content {
class WebContents;
}

namespace arcium {

// A loose page is a real tab in the window's one strip that the sidebar does
// not draw: a peek over a pinned tab, or the page in the small window a link
// from another application opens in. Zen's Glance has the same shape -- an
// ordinary tab, hidden from the list and drawn somewhere else -- and for the
// same reason: only a tab gets the password manager, extension identity and
// dialogs a page needs.
//
// The marker is what makes it loose. SpaceOfTab answers no space for a marked
// tab, so everything that asks "is this tab in space S" -- the sidebar's
// rows, the space-scoped tab commands, tab search, the archive -- passes over
// it without learning a rule of its own.
enum class LoosePageKind {
  kNone,
  kPeek,
  kOutsideLink,
};

LoosePageKind LoosePageKindOf(const content::WebContents* web_contents);
bool IsLoosePage(const content::WebContents* web_contents);

// Set before the tab is inserted, so nothing listening to insertion ever sees
// it unmarked. kNone promotes it to an ordinary tab.
void SetLoosePageKind(content::WebContents* web_contents, LoosePageKind kind);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_LOOSE_PAGE_H_
