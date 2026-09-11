// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_

#include <memory>
#include <string>

#include "arcium/common/arcium_features.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"

class Browser;
class BrowserView;
class ContentsContainerView;
struct BrowserLayoutParams;

namespace base {
class FilePath;
}

namespace views {
class Widget;
}

namespace arcium {

class QuickEntryBubble;
class SidebarView;

// Owns the sidebar inside one BrowserView and answers the layout hooks.
// Created by BrowserView::InitViews when the sidebar feature is on.
class BrowserSidebarController : public SidebarModel::Observer {
 public:
  static std::unique_ptr<BrowserSidebarController> MaybeCreate(
      BrowserView* browser_view);

  BrowserSidebarController(const BrowserSidebarController&) = delete;
  BrowserSidebarController& operator=(const BrowserSidebarController&) = delete;
  ~BrowserSidebarController() override;

  SidebarView* view() { return view_; }
  int width() const;

  // The height macOS should treat as this window's title bar, so the traffic
  // lights land on the nav row's centre line rather than hard against the top
  // of the window. See metrics::kTitlebarHeight for why the number is what it
  // is; this exists so the frame hook stays a call and carries no arithmetic.
  int TitlebarHeight() const;

  // Layout hooks. See the patch inventory in the Stage 1 plan.
  void AdjustLayoutParams(BrowserLayoutParams& params);
  void LayoutSidebar(const gfx::Rect& host_bounds);
  bool IsPositionInWindowCaption(const gfx::Point& point_in_browser_view) const;

  void ToggleVisibility();

  // Moves the toolbar's LocationBarView into the URL pill. Called once from
  // BrowserView::InitViews after the toolbar exists (patch 0050).
  void HostLocationBar();

  // Cmd+T: the floating entry over the page.
  void ShowQuickEntry();

  // SidebarModel::Observer:
  void OnSidebarModelChanged() override;

 private:
  explicit BrowserSidebarController(BrowserView* browser_view);

  void UpdateContentCorners();
  void ExecuteCommand(int command_id);
  void UpdateNavButtons();
  void MaybeScheduleSnapshot();
  void MaybeShowQuickEntryForDebugging();
  void WriteSnapshot(const base::FilePath& path);
  void OnQuickEntrySubmitted(const std::u16string& text);
  void OnQuickEntryClosed(views::Widget::ClosedReason reason);
  void DestroyQuickEntry();

  raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<SidebarTabModel> model_;
  // What --arcium-fake-clock-offset offsets, and the only thing it does: the
  // clock the archive sweep asks whether a tab has been idle long enough.
  // Zero offset without the switch, which is base::Time::Now(). Declared
  // before `archive_service_`, which holds a pointer to it.
  features::OffsetClock archive_clock_{features::FakeClockOffset()};
  // One per window, because a tab is in exactly one strip. Null off the
  // record: an archive row outlives the window that wrote it, which is the one
  // thing incognito must not do. `model_` holds a bare pointer to it, which
  // the destructor clears before this is freed.
  std::unique_ptr<ArchiveService> archive_service_;
  raw_ptr<SidebarView> view_ = nullptr;
  bool visible_ = true;
  int caption_button_width_ = -1;
  float applied_corner_radius_ = -1.f;
  raw_ptr<ContentsContainerView> last_container_ = nullptr;

  // The delegate must outlive its widget; both are torn down together after
  // the widget reports it closed.
  std::unique_ptr<QuickEntryBubble> quick_entry_;
  std::unique_ptr<views::Widget> quick_entry_widget_;

  base::WeakPtrFactory<BrowserSidebarController> weak_factory_{this};
};

// Hook target for IDC_NEW_TAB (patch 0090). Returns false when `browser` has
// no sidebar, in which case the caller opens a plain new tab.
bool HandleNewTabCommand(Browser* browser);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_
