// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standalone host for the sidebar components, on the Views examples runner.
// Mirrors ui/views/examples/vector_icon_viewer.cc: a plain executable works
// on every platform, including unbundled on macOS.

#include <memory>
#include <utility>

#include "arcium/ui/playground/sidebar_example.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/test/test_timeouts.h"
#include "ui/color/color_provider_manager.h"
#include "ui/views/examples/example_base.h"
#include "ui/views/examples/examples_main_proc.h"

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  // ExamplesMainProc uses base::test::TaskEnvironment, which needs these.
  TestTimeouts::Initialize();
  base::AtExitManager at_exit;

  // The browser registers this through a hook (patch 0080); here directly.
  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&arcium::AddArciumColorMixer));

  views::examples::ExampleVector examples;
  examples.push_back(std::make_unique<arcium::SidebarExample>());
  return static_cast<int>(views::examples::ExamplesMainProc(
      /*under_test=*/false, std::move(examples)));
}
