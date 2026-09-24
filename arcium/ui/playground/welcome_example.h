// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_WELCOME_EXAMPLE_H_
#define ARCIUM_UI_PLAYGROUND_WELCOME_EXAMPLE_H_

#include <memory>

#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/playground/fake_welcome_model.h"
#include "ui/views/examples/example_base.h"

namespace arcium {

// Command line switches for the welcome example:
//   --welcome             host it alone, so a snapshot shows it
//   --welcome-step=<n>    open at step n, counting from 1
//   --welcome-fresh       with Start fresh chosen rather than Zen
//   --welcome-imported    as though the import has run
//   --welcome-default     as though Kyuzen is already the default browser
//   --welcome-import-only the command box's one-step import
inline constexpr char kWelcomeSwitch[] = "welcome";

// Hosts the welcome card beside the sidebar, the way the browser shows it, on
// fake models with made-up content.
class WelcomeExample : public views::examples::ExampleBase {
 public:
  WelcomeExample();
  ~WelcomeExample() override;

  // ExampleBase:
  void CreateExampleView(views::View* container) override;

 private:
  std::unique_ptr<FakeSidebarModel> sidebar_model_;
  std::unique_ptr<FakeWelcomeModel> welcome_model_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_WELCOME_EXAMPLE_H_
