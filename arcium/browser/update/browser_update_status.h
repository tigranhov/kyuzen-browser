// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_BROWSER_UPDATE_STATUS_H_
#define ARCIUM_BROWSER_UPDATE_BROWSER_UPDATE_STATUS_H_

namespace arcium {

class UpdateStatus;

// The browser's one updater, for the About page to read. Null until it is
// running, which in a build that carries no Sparkle -- every build but a
// release -- is never. UI thread only.
UpdateStatus* GetBrowserUpdateStatus();

// Set once when the updater starts. Tests set a fake and clear it after.
void SetBrowserUpdateStatus(UpdateStatus* status);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_BROWSER_UPDATE_STATUS_H_
