// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The welcome's place in the window: when it shows, and the command box's
// import, which shows its first step alone.

#include <memory>
#include <optional>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/profile_defaults.h"
#include "arcium/common/arcium_features.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/welcome_controller.h"
#include "arcium/ui/browser/welcome_sources.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_view.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_switches.h"
#include "components/prefs/pref_service.h"

namespace arcium {

void BrowserSidebarController::MaybeShowWelcome() {
  Profile* profile = browser_view_->GetProfile();
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  // Chromium's own first-run switch keeps it away, as it keeps Chromium's
  // first-run pages away, unless it is asked for by name.
  if (profile->IsOffTheRecord() ||
      (command_line.HasSwitch(switches::kNoFirstRun) &&
       !command_line.HasSwitch(features::kWelcomeSwitch))) {
    return;
  }
  ArciumProfileState* state = ArciumProfileState::GetForBrowserContext(profile);
  if (!state) {
    return;
  }
  // Whether this is a fresh install is known only once the model has looked
  // for its file, which is after this window is built.
  state->RunWhenModelLoaded(
      base::BindOnce(&BrowserSidebarController::OnModelLoadedForWelcome,
                     weak_factory_.GetWeakPtr()));
}

void BrowserSidebarController::OnModelLoadedForWelcome() {
  Profile* profile = browser_view_->GetProfile();
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(profile);
  if (!state || welcome_) {
    return;
  }
  const std::optional<WelcomeStep> first =
      WelcomeResumeStep(profile->GetPrefs()->GetInteger(kWelcomeStepPref),
                        state->model_file_was_absent());
  if (first) {
    ShowWelcome(/*import_only=*/false, *first);
  }
}

void BrowserSidebarController::ShowImport() {
  // The welcome's own first step is this, and more; one card at a time.
  if (!welcome_) {
    ShowWelcome(/*import_only=*/true, WelcomeStep::kSetup);
  }
}

void BrowserSidebarController::ShowWelcome(bool import_only,
                                           WelcomeStep first) {
  welcome_ = std::make_unique<WelcomeController>(
      browser_view_, space_switcher_.get(),
      import_only ? WelcomeView::Mode::kImportOnly
                  : WelcomeView::Mode::kWelcome,
      first,
      base::BindOnce(&BrowserSidebarController::CloseWelcome,
                     weak_factory_.GetWeakPtr()));
  welcome_->Layout(WholePageArea());
}

void BrowserSidebarController::CloseWelcome() {
  welcome_.reset();
}

}  // namespace arcium
