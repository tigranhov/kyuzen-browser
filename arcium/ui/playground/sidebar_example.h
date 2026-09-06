// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_SIDEBAR_EXAMPLE_H_
#define ARCIUM_UI_PLAYGROUND_SIDEBAR_EXAMPLE_H_

#include <memory>

#include "arcium/ui/playground/fake_sidebar_model.h"
#include "ui/views/examples/example_base.h"

namespace arcium {

// Hosts SidebarView next to a white "page" rectangle, on a fake model seeded
// with a realistic set of tabs.
class SidebarExample : public views::examples::ExampleBase {
 public:
  SidebarExample();
  ~SidebarExample() override;

  // ExampleBase:
  void CreateExampleView(views::View* container) override;

 private:
  std::unique_ptr<FakeSidebarModel> model_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_SIDEBAR_EXAMPLE_H_
