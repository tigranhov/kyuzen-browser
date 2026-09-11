// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/tab_selection.h"

#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

namespace arcium {

std::optional<int> NextSelectedIndexInSpace(TabStripModel* tab_strip_model,
                                            std::optional<int> chromium_choice,
                                            int removed_index,
                                            int removed_count) {
  SpaceSwitcher* switcher =
      tab_strip_model ? SpaceSwitcher::FromTabStripModel(tab_strip_model)
                      : nullptr;
  // No sidebar, or the strip is about to be empty and Chromium has already
  // said there is nothing to select.
  if (!switcher || !chromium_choice.has_value()) {
    return chromium_choice;
  }
  // Chromium answers in post-removal indices and the strip has not moved
  // yet, so shift back past the removed block to ask which tab it means.
  const auto to_current = [removed_index, removed_count](int after) {
    return after >= removed_index ? after + removed_count : after;
  };
  const auto to_after = [removed_index, removed_count](int current) {
    return current > removed_index ? current - removed_count : current;
  };
  if (switcher->IsInActiveSpace(to_current(*chromium_choice))) {
    return chromium_choice;
  }
  // The nearest open tab of the space, looking right first: the direction
  // the strip closes towards, and the one a list reads in.
  const int count = tab_strip_model->count();
  for (int index = removed_index + removed_count; index < count; ++index) {
    if (switcher->IsInActiveSpace(index)) {
      return to_after(index);
    }
  }
  for (int index = removed_index - 1; index >= 0; --index) {
    if (switcher->IsInActiveSpace(index)) {
      return to_after(index);
    }
  }
  // The space has no other open tab. Every close Arcium itself makes opens a
  // blank tab first, so this is a close it did not see, such as a page
  // closing itself. Keep Chromium's answer; the switcher adopts the space of
  // whichever tab it lands on.
  return chromium_choice;
}

}  // namespace arcium
