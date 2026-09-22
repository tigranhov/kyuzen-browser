// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_ABOUT_VERSION_UPDATER_H_
#define ARCIUM_BROWSER_UPDATE_ABOUT_VERSION_UPDATER_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "chrome/browser/ui/webui/help/version_updater.h"

namespace arcium {

class UpdateStatus;

// Answers Chromium's About page from Kyuzen's updater instead of Google's.
// The page makes one of these each time it opens and asks it to check, so a
// check here is Sparkle's quiet one: the page shows the answer, and a window
// of Sparkle's appears only when there is a version to offer.
class AboutVersionUpdater : public VersionUpdater {
 public:
  // `status` may be null, when this build carries no updater.
  explicit AboutVersionUpdater(UpdateStatus* status);
  AboutVersionUpdater(const AboutVersionUpdater&) = delete;
  AboutVersionUpdater& operator=(const AboutVersionUpdater&) = delete;
  ~AboutVersionUpdater() override;

  // VersionUpdater:
  void CheckForUpdate(StatusCallback status_callback,
                      PromoteCallback promote_callback) override;
#if BUILDFLAG(IS_MAC)
  // Promoting is installing Google's updater for every user of the machine,
  // which Kyuzen has no use for. The page never offers it, because the
  // promote callback is never run.
  void PromoteUpdater() override {}
#endif

 private:
  void Report();

  raw_ptr<UpdateStatus> status_;
  StatusCallback status_callback_;
  base::CallbackListSubscription subscription_;
  // Between asking for a look and Sparkle saying it has started one, the
  // updater still reads idle, which must not show as up to date.
  bool look_pending_ = false;
};

// What VersionUpdater::Create returns on macOS (patch 0260).
std::unique_ptr<VersionUpdater> MakeAboutVersionUpdater();

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_ABOUT_VERSION_UPDATER_H_
