// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/tab_commands.h"

#include <algorithm>
#include <functional>
#include <vector>

#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/tab_close_types.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "components/tabs/public/tab_interface.h"

namespace arcium {

namespace {

TabStripUserGestureDetails KeyboardGesture() {
  return TabStripUserGestureDetails(
      TabStripUserGestureDetails::GestureType::kKeyboard);
}

// Steps through the space's open tabs in the order the sidebar draws them,
// wrapping at either end. An active tab that is not among them -- another
// space's tab the switcher is about to adopt -- lands on the space's first.
bool SelectRelative(TabStripModel* strip,
                    const std::vector<int>& order,
                    int delta) {
  if (order.empty()) {
    return true;
  }
  const auto it = std::ranges::find(order, strip->active_index());
  int target = order.front();
  if (it != order.end()) {
    const int size = static_cast<int>(order.size());
    const int position = static_cast<int>(it - order.begin());
    target = order[(position + delta % size + size) % size];
  }
  strip->ActivateTabAt(target, KeyboardGesture());
  return true;
}

// Cmd+1..8 and Cmd+9 count the space's open tabs, not its sidebar rows: a
// cold row has no tab to activate, and where it sits depends on which
// folders are collapsed, which only the view knows. A digit past the space's
// last open tab is still answered, by doing nothing, because letting it fall
// through would have Chromium count the whole strip and land in another
// space.
bool SelectNth(TabStripModel* strip, const std::vector<int>& order, int n) {
  if (n >= 0 && n < static_cast<int>(order.size())) {
    strip->ActivateTabAt(order[n], KeyboardGesture());
  }
  return true;
}

// Moves the active Today tab one place among its own space's Today tabs,
// stepping over every tab of another space rather than into its slot. A
// pinned or favourite tab stays put: its place is its entry's, and the strip
// does not decide what the sidebar draws there.
bool MoveTodayTab(TabStripModel* strip,
                  const SpaceSwitcher& switcher,
                  int delta) {
  const int active = strip->active_index();
  if (active == TabStripModel::kNoTab || !switcher.IsInActiveSpace(active) ||
      switcher.IsClaimedByEntryAt(active)) {
    return true;
  }
  for (int index = active + delta; index >= 0 && index < strip->count();
       index += delta) {
    if (switcher.IsInActiveSpace(index) &&
        !switcher.IsClaimedByEntryAt(index)) {
      strip->MoveWebContentsAt(active, index, /*select_after_move=*/true);
      return true;
    }
  }
  return true;
}

// Chromium's own close whenever the space keeps another open tab; the
// space's last one gets its blank tab first, then closes. The rule itself is
// SpaceSwitcher::OpenBlankTabBeforeClosing, shared with every close the
// sidebar makes.
bool CloseActiveTab(TabStripModel* strip, SpaceSwitcher& switcher) {
  const int active = strip->active_index();
  if (active == TabStripModel::kNoTab) {
    return false;
  }
  tabs::TabInterface* closing = strip->GetTabAtIndex(active);
  if (!switcher.OpenBlankTabBeforeClosing({active})) {
    return false;
  }
  const int index = strip->GetIndexOfTab(closing);
  if (index != TabStripModel::kNoTab) {
    // A page that asks before unloading can be kept by the user. The space
    // then has both the old tab and the blank one, and both are its own, so
    // the window still shows nothing from another space.
    strip->CloseWebContentsAt(index, kUserCloseTypes);
  }
  return true;
}

// Closes the active space's Today tabs other than the active one, and for
// close-to-the-right only those after it in `order`, the sidebar's order --
// what the user sees, where every Today tab sits below the pinned and
// favourite entries. The strip's order would be wrong: a pinned tab keeps
// whatever slot it had when it was pinned, among the Today tabs, so from an
// entry's tab close-to-the-right means every Today tab of the space. Other
// spaces' tabs are not on screen, and an entry's tab is spared the way
// Chromium spares a pinned tab.
bool CloseTodayTabs(TabStripModel* strip,
                    const SpaceSwitcher& switcher,
                    const std::vector<int>& order,
                    bool only_to_the_right) {
  const int active = strip->active_index();
  if (active == TabStripModel::kNoTab) {
    return true;
  }
  auto first = order.begin();
  if (only_to_the_right) {
    // An active tab that is not the space's own has none of it to its right.
    first = std::ranges::find(order, active);
    if (first != order.end()) {
      ++first;
    }
  }
  std::vector<int> closing;
  for (auto it = first; it != order.end(); ++it) {
    if (*it != active && !switcher.IsClaimedByEntryAt(*it)) {
      closing.push_back(*it);
    }
  }
  // Backwards by strip index, so a close never shifts an index still to be
  // closed.
  std::ranges::sort(closing, std::greater<>());
  for (int index : closing) {
    strip->CloseWebContentsAt(index, kUserCloseTypes);
  }
  return true;
}

}  // namespace

bool HandleTabCommand(Browser* browser, int command_id) {
  TabStripModel* strip = browser ? browser->tab_strip_model() : nullptr;
  SpaceSwitcher* switcher =
      strip ? SpaceSwitcher::FromTabStripModel(strip) : nullptr;
  // No sidebar, no spaces, and every one of these is Chromium's own. The
  // same shape HandleNewTabCommand has, for the same reason.
  if (!switcher) {
    return false;
  }
  const std::vector<int> order =
      switcher->OpenTabsInSidebarOrder(switcher->active_space());
  switch (command_id) {
    // Ctrl+Tab sends the cycle commands on a Mac and select-next elsewhere;
    // both step through the space the same way, never in most-recently-used
    // order, since that order runs across every space.
    case IDC_SELECT_NEXT_TAB:
    case IDC_CYCLE_TO_NEXT_TAB:
      return SelectRelative(strip, order, 1);
    case IDC_SELECT_PREVIOUS_TAB:
    case IDC_CYCLE_TO_PREV_TAB:
      return SelectRelative(strip, order, -1);
    case IDC_SELECT_TAB_0:
    case IDC_SELECT_TAB_1:
    case IDC_SELECT_TAB_2:
    case IDC_SELECT_TAB_3:
    case IDC_SELECT_TAB_4:
    case IDC_SELECT_TAB_5:
    case IDC_SELECT_TAB_6:
    case IDC_SELECT_TAB_7:
      return SelectNth(strip, order, command_id - IDC_SELECT_TAB_0);
    case IDC_SELECT_LAST_TAB:
      return SelectNth(strip, order, static_cast<int>(order.size()) - 1);
    case IDC_MOVE_TAB_NEXT:
      return MoveTodayTab(strip, *switcher, 1);
    case IDC_MOVE_TAB_PREVIOUS:
      return MoveTodayTab(strip, *switcher, -1);
    case IDC_CLOSE_TAB:
      return CloseActiveTab(strip, *switcher);
    case IDC_WINDOW_CLOSE_OTHER_TABS:
      return CloseTodayTabs(strip, *switcher, order,
                            /*only_to_the_right=*/false);
    case IDC_WINDOW_CLOSE_TABS_TO_RIGHT:
      return CloseTodayTabs(strip, *switcher, order,
                            /*only_to_the_right=*/true);
    default:
      return false;
  }
}

}  // namespace arcium
