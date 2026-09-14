// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_HOSTED_LOCATION_BAR_H_
#define ARCIUM_UI_BROWSER_HOSTED_LOCATION_BAR_H_

namespace views {
class View;
}

namespace arcium {

// True while `bar` sits inside the sidebar's address pill.
//
// The pill draws the address itself, so the bar behind it draws nothing. It
// is still there, and still drawn, for two reasons it cannot delegate:
// Chrome's password and page information bubbles find their anchor through
// it, and a permission request is a chip it owns outright.
bool IsLocationBarHosted(const views::View* bar);

// Called when a hosted bar has just laid out. Hides every child the pill
// draws itself, leaves the permission chip alone, and tells the pill whether
// that chip has something to say.
void OnHostedLocationBarLaidOut(views::View* bar);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_HOSTED_LOCATION_BAR_H_
