// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_BROWSER_UPDATES_H_
#define ARCIUM_UI_BROWSER_BROWSER_UPDATES_H_

#include "base/memory/weak_ptr.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace arcium {

class UpdateStatus;

// Starts the browser's one updater once startup is over, and lets go of the
// setting's store at shutdown. Added to Chromium's startup parts by patch
// 0248.
class BrowserUpdates : public ChromeBrowserMainExtraParts {
 public:
  BrowserUpdates();
  BrowserUpdates(const BrowserUpdates&) = delete;
  BrowserUpdates& operator=(const BrowserUpdates&) = delete;
  ~BrowserUpdates() override;

  // What the updater knows. Null until it is running, which in a build that
  // carries no Sparkle -- every build but a release -- is never.
  static UpdateStatus* Get();

  // ChromeBrowserMainExtraParts:
  void PostBrowserStart() override;
  void PostMainMessageLoopRun() override;

 private:
  void LoadFramework();
  void OnFrameworkLoaded(bool loaded);

  base::WeakPtrFactory<BrowserUpdates> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_BROWSER_UPDATES_H_
