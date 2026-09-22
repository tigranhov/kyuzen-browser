// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>

#include "arcium/common/arcium_features.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/suggestion_source.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_session.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "content/public/browser/web_contents_observer.h"
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

class CommandBox;
class PeekController;
class SplitController;
class SplitDropView;
class SuggestionSource;
class TabSearchService;
class SidebarView;
class SpaceSwitcher;

// Owns the sidebar inside one BrowserView and answers the layout hooks.
// Created by BrowserView::InitViews when the sidebar feature is on.
class BrowserSidebarController : public SidebarModel::Observer,
                                 public RowDragSession::Observer,
                                 public content::WebContentsObserver {
 public:
  static std::unique_ptr<BrowserSidebarController> MaybeCreate(
      BrowserView* browser_view);

  BrowserSidebarController(const BrowserSidebarController&) = delete;
  BrowserSidebarController& operator=(const BrowserSidebarController&) = delete;
  ~BrowserSidebarController() override;

  SidebarView* view() { return view_; }
  int width() const;

  // The real SidebarTabModel this window's sidebar draws from, so a test can
  // call its forwarding methods directly instead of the free functions they
  // forward to. Test-only.
  SidebarTabModel* model_for_testing() { return model_.get(); }

  // The height macOS should treat as this window's title bar, so the traffic
  // lights land on the nav row's centre line rather than hard against the top
  // of the window. See metrics::kTitlebarHeight for why the number is what it
  // is; this exists so the frame hook stays a call and carries no arithmetic.
  int TitlebarHeight() const;

  // Layout hooks. See the patch inventory in the Stage 1 plan.
  void AdjustLayoutParams(BrowserLayoutParams& params);
  void LayoutSidebar(const gfx::Rect& host_bounds);
  bool IsPositionInWindowCaption(const gfx::Point& point_in_browser_view) const;

  // True inside the thin band along the top of the window that moves it.
  // Part of the answer above; separate because it is about the window's own
  // top edge rather than about anything in the sidebar.
  bool IsWindowTopGrabBand(const gfx::Point& point_in_browser_view) const;

  void ToggleVisibility();

  // Moves the toolbar's LocationBarView into the URL pill. Called once from
  // BrowserView::InitViews after the toolbar exists (patch 0050).
  void HostLocationBar();

  // Moves the toolbar's strip of pinned extension buttons into the row above
  // the favourites. Called from the same place, for the same reason: the
  // toolbar this browser never lays out is where they would otherwise sit.
  void HostExtensionsContainer();

  // Cmd+T, Cmd+L and a click on the pill: the floating box over the page.
  // `initial_text` fills the field and selects it, which is what Cmd+L means.
  void ShowCommandBox(std::optional<std::u16string> initial_text);
  // The same, shaped for the callbacks that carry no text.
  void ShowCommandBoxWithNoText();

  // The window's peek, or null when the feature is off. A link that leaves a
  // pinned entry's home asks this whether it can be shown over the page.
  PeekController* peek() { return peek_.get(); }

  // The window's split view: who may share a screen, and how one ends.
  SplitController* split() { return split_.get(); }

  CommandBox* command_box_for_testing() { return command_box_.get(); }
  SuggestionSource* suggestion_source_for_testing() {
    return suggestion_source_.get();
  }

  // SidebarModel::Observer:
  void OnSidebarModelChanged() override;

  // RowDragSession::Observer: raises the split target over the page while a
  // sidebar row is being dragged, and takes it away again afterwards.
  void OnRowDragInFlightChanged() override;

  // content::WebContentsObserver. The pill's warning follows the page rather
  // than the navigation, because a page can turn insecure while it sits there.
  void DidChangeVisibleSecurityState() override;
  void PrimaryPageChanged(content::Page& page) override;

 private:
  explicit BrowserSidebarController(BrowserView* browser_view);

  void UpdateContentCorners();
  // Where the page is drawn, in this window's coordinates. Empty before the
  // window has a contents container.
  gfx::Rect PageArea() const;
  // The same, but both panes of a split rather than the one in front.
  gfx::Rect WholePageArea() const;
  void ShowSplitDropTarget();
  void TakeAwaySplitDropTarget();
  void OnSplitDrop(RowDragData payload, bool right);
  void ExecuteCommand(int command_id);
  void UpdateNavButtons();
  void MaybeScheduleSnapshot();
  void MaybeShowCommandBoxForDebugging();
  void WriteSnapshot(const base::FilePath& path);
  // The pill's own buttons.
  void OpenExtensionsMenu();
  void CopyCurrentUrl();
  void ShowSiteInfo();
  // Points the pill at the tab on screen: its address, and whether its
  // connection is one to warn about.
  void UpdatePillForActiveTab();
  void UpdatePillSecurity();

  void OnCommandBoxAccepted(SuggestionRow row);
  // A command row: an IDC_ command, or one of box_commands.h's own.
  void RunBoxCommand(int command_id);
  void OnCommandBoxClosed(views::Widget::ClosedReason reason);
  void DestroyCommandBox();
  // Switches to an open tab showing `url`, in whatever space holds it.
  // False when there is none, which is what keeps the caller's fallback
  // honest rather than silent.
  bool ActivateTabWithUrl(const GURL& url);

  raw_ptr<BrowserView> browser_view_;
  // Which space this window shows. Declared before `model_` and
  // `archive_service_` so it is destroyed after both: each holds a bare
  // pointer to it and removes itself from it in its own destructor. The
  // sidebar view reaches it only through `model_`, and ~BrowserView removes
  // its child views before it destroys this controller.
  std::unique_ptr<SpaceSwitcher> space_switcher_;
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
  // Null while the peek feature is off. The destructor resets it before
  // anything else, because it holds a view in the BrowserView and a page in
  // the strip and both must still be whole when it lets go of them.
  std::unique_ptr<PeekController> peek_;
  // After `space_switcher_` and `model_` by declaration order, because it
  // holds a pointer to each.
  std::unique_ptr<SplitController> split_;
  // A child of the BrowserView, owned by it, and alive only while a row is
  // being dragged. Null the rest of the time, which is the whole of its
  // idle cost.
  raw_ptr<SplitDropView> split_drop_ = nullptr;
  base::ScopedObservation<RowDragSession, RowDragSession::Observer>
      drag_observation_{this};
  bool visible_ = true;
  int caption_button_width_ = -1;
  float applied_corner_radius_ = -1.f;
  raw_ptr<ContentsContainerView> last_container_ = nullptr;

  // Searches this window's tabs, the profile's entries and the archive. It
  // observes nothing and holds no per-tab state, so one that is never asked a
  // question does no work; it lives here because the strip's lifetime bounds
  // it, as it bounds the model beside it.
  std::unique_ptr<TabSearchService> tab_search_;
  // Built when the box opens and destroyed when it closes, so a browser
  // sitting idle carries no autocomplete providers and no timers.
  std::unique_ptr<SuggestionSource> suggestion_source_;
  // The delegate must outlive its widget; both are torn down together after
  // the widget reports it closed.
  std::unique_ptr<CommandBox> command_box_;
  std::unique_ptr<views::Widget> command_box_widget_;

  base::WeakPtrFactory<BrowserSidebarController> weak_factory_{this};
};

// Hook target for IDC_NEW_TAB (patch 0090). Returns false when `browser` has
// no sidebar, in which case the caller opens a plain new tab.
bool HandleNewTabCommand(Browser* browser);

// Hook target for IDC_FOCUS_LOCATION (patch 0090). Opens the same box Cmd+T
// opens, holding the whole address. Returns false when `browser` has no
// sidebar, in which case Chromium focuses its own address bar.
bool HandleFocusLocationCommand(Browser* browser);

// Hook target for the extensions container's construction (patch 0210). A
// window with an Arcium sidebar wants the pinned buttons and not the menu
// button beside them, which is what auto-hide mode means; every other window
// gets Chromium's normal mode. Returns the int value of
// ExtensionsToolbarDesktop::DisplayMode, so this header carries no dependency
// on //chrome/browser/ui/views.
int ExtensionsDisplayMode(Browser* browser);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_
