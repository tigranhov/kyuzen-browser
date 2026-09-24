// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_WELCOME_WELCOME_STEPS_H_
#define ARCIUM_UI_WELCOME_WELCOME_STEPS_H_

#include <memory>
#include <string>

#include "ui/views/view.h"

namespace arcium {

class WelcomeModel;

namespace welcome {

// One step's words and views: the choices on the left, and the picture of
// what they mean in the panel on the right.
struct StepContent {
  StepContent();
  StepContent(StepContent&&);
  StepContent& operator=(StepContent&&);
  ~StepContent();

  std::u16string title;
  std::u16string subtitle;
  std::unique_ptr<views::View> choices;
  std::u16string panel_title;
  std::u16string panel_subtitle;
  std::unique_ptr<views::View> panel;
  // A line at the foot of the panel.
  std::u16string panel_note;
};

// Each builder reads the model as it is now and wires every control to it.
// `import_only` is the command box's way in: the first step alone, with no
// Start fresh, since nobody asks to import in order to import nothing.
StepContent BuildSetupStep(WelcomeModel& model, bool import_only);
StepContent BuildLoginsStep(WelcomeModel& model);
StepContent BuildSearchStep(WelcomeModel& model);
StepContent BuildDefaultBrowserStep(WelcomeModel& model);
StepContent BuildSyncStep(WelcomeModel& model);
StepContent BuildDoneStep(WelcomeModel& model);

}  // namespace welcome
}  // namespace arcium

#endif  // ARCIUM_UI_WELCOME_WELCOME_STEPS_H_
