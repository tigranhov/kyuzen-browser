// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The command box half of BrowserSidebarController: opening the box, what
// happens to the row the reader chooses, and the two key presses that ask for
// it. Split out of browser_sidebar_controller.cc when the box learned to run
// commands, because that file was at the size where it stops being readable.

#include <memory>
#include <utility>

#include "arcium/ui/browser/box_commands.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/command_box.h"
#include "arcium/ui/browser/page_in_space.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/split_controller.h"
#include "arcium/ui/browser/tab_search_service.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace arcium {

void BrowserSidebarController::ShowCommandBoxWithNoText() {
  ShowCommandBox(std::nullopt);
}

void BrowserSidebarController::ShowCommandBox(
    std::optional<std::u16string> initial_text) {
  if (command_box_widget_) {
    command_box_->FocusField();
    if (initial_text) {
      command_box_->SetText(*initial_text, /*select_all=*/true);
    }
    return;
  }
  suggestion_source_ = std::make_unique<SuggestionSource>(
      browser_view_->GetProfile(), tab_search_.get());
  command_box_ = std::make_unique<CommandBox>(
      browser_view_, suggestion_source_.get(),
      base::BindOnce(&BrowserSidebarController::OnCommandBoxAccepted,
                     weak_factory_.GetWeakPtr()),
      // Unretained rather than weak: a callback that returns something
      // cannot be bound to a weak pointer, and this controller owns the box
      // and outlives it.
      base::BindRepeating(&BrowserSidebarController::SplitPartnerRows,
                          base::Unretained(this)));
  command_box_widget_ = views::BubbleDialogDelegate::CreateBubble(
      command_box_.get(),
      base::BindOnce(&BrowserSidebarController::OnCommandBoxClosed,
                     weak_factory_.GetWeakPtr()));
  command_box_widget_->Show();
  if (initial_text) {
    command_box_->SetText(*initial_text, /*select_all=*/true);
  }
  command_box_->FocusField();
}

void BrowserSidebarController::OnCommandBoxAccepted(SuggestionRow row) {
  if (row.split_with_tab_index) {
    // A tab chosen in the box's second stage. Posted for the same reason
    // every command is: the box is still closing, and splitting moves the
    // focus into a page.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&BrowserSidebarController::SplitWithTabAt,
                       weak_factory_.GetWeakPtr(), *row.split_with_tab_index));
    return;
  }
  if (row.command_id) {
    // Once the box has finished closing, so a command that moves focus --
    // Find -- takes it from the page rather than from the box on its way out.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&BrowserSidebarController::RunBoxCommand,
                                  weak_factory_.GetWeakPtr(), *row.command_id));
    return;
  }
  if (!row.destination.is_valid()) {
    return;
  }
  // A tab already open is switched to rather than loaded a second time, in
  // whatever space is holding it.
  if (row.is_open_tab && ActivateTabWithUrl(row.destination)) {
    return;
  }
  OpenUrlInRoutedSpace(browser_view_->browser(), row.destination);
}

std::vector<SuggestionRow> BrowserSidebarController::SplitPartnerRows() {
  std::vector<SuggestionRow> partners;
  TabStripModel* const strip = browser_view_->browser()->tab_strip_model();
  const int active = strip->active_index();
  if (!split_ || active < 0) {
    return partners;
  }
  for (int i = 0; i < strip->count(); ++i) {
    if (!split_->CanSplit(active, i)) {
      continue;
    }
    SuggestionRow row;
    row.title = strip->GetTabAtIndex(i)->GetContents()->GetTitle();
    row.subtitle = u"Split with this tab";
    row.split_with_tab_index = i;
    partners.push_back(std::move(row));
  }
  return partners;
}

void BrowserSidebarController::SplitWithTabAt(int tab_index) {
  if (split_) {
    split_->SplitWithActive(tab_index);
  }
}

void BrowserSidebarController::RunBoxCommand(int command_id) {
  switch (command_id) {
    case kBoxCommandNewSpace:
      model_->AddSpace(u"New space");
      return;
    case kBoxCommandToggleSidebar:
      ToggleVisibility();
      return;
    case kBoxCommandImport:
      ShowImport();
      return;
    case kBoxCommandSiteSearch:
      // Chromium's own page for keywords. Written out rather than built from
      // chrome/common's constants, which this target does not depend on.
      chrome::AddSelectedTabWithURL(browser_view_->browser(),
                                    GURL("chrome://settings/searchEngines"),
                                    ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
      return;
    default:
      ExecuteCommand(command_id);
  }
}

bool BrowserSidebarController::ActivateTabWithUrl(const GURL& url) {
  TabStripModel* strip = browser_view_->browser()->tab_strip_model();
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents || contents->GetLastCommittedURL() != url) {
      continue;
    }
    // The tab may live in a space this window is not showing; going to the
    // tab without going to its space would show it under the wrong sidebar.
    if (space_switcher_) {
      space_switcher_->SwitchTo(space_switcher_->SpaceOfTabAt(i));
    }
    strip->ActivateTabAt(i);
    return true;
  }
  return false;
}

void BrowserSidebarController::OnCommandBoxClosed(
    views::Widget::ClosedReason reason) {
  // Runs synchronously from the close; free them once the stack unwinds.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserSidebarController::DestroyCommandBox,
                                weak_factory_.GetWeakPtr()));
}

void BrowserSidebarController::DestroyCommandBox() {
  command_box_widget_.reset();
  command_box_.reset();
  // With the box goes everything it was asking on behalf of.
  suggestion_source_.reset();
}

bool HandleFocusLocationCommand(Browser* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->arcium_sidebar()) {
    return false;
  }
  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();
  // The whole address, not the domain the pill shows: this key exists to
  // replace or edit what is there, and half an address is neither.
  std::u16string text;
  if (contents && contents->GetLastCommittedURL().is_valid()) {
    text = base::UTF8ToUTF16(contents->GetLastCommittedURL().spec());
  }
  browser_view->arcium_sidebar()->ShowCommandBox(std::move(text));
  return true;
}

bool HandleNewTabCommand(Browser* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->arcium_sidebar()) {
    return false;
  }
  browser_view->arcium_sidebar()->ShowCommandBox(std::nullopt);
  return true;
}

}  // namespace arcium
