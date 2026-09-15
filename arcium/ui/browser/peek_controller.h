// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PEEK_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_PEEK_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/rect.h"

class BrowserView;

namespace content {
class WebContents;
struct OpenURLParams;
}  // namespace content

namespace arcium {

class PeekView;

// One window's peek (R4.4): a link that left a pinned or favourite tab's
// home, shown over that tab instead of in a new one. The page is a loose
// page -- a real tab the sidebar does not draw -- so opening it as a tab is
// only a promotion. At most one at a time.
class PeekController : public content::WebContentsObserver,
                       public TabStripModelObserver {
 public:
  explicit PeekController(BrowserView* browser_view);
  PeekController(const PeekController&) = delete;
  PeekController& operator=(const PeekController&) = delete;
  ~PeekController() override;

  // Opens `params` in a peek over `source`'s tab, in the space and storage of
  // that tab, replacing any peek already open. False when this window cannot
  // show one, and then the caller opens a tab as it did before peeks.
  bool Show(content::WebContents* source, const content::OpenURLParams& params);
  // Dismisses the peek and closes its page.
  void Close();
  // Turns the peek into an ordinary tab in its space and puts it on screen.
  void OpenAsTab();

  // Places the peek over `page_area`, in the BrowserView's coordinates.
  // Called on every window layout.
  void Layout(const gfx::Rect& page_area);

  bool is_showing() const { return view_ != nullptr; }
  PeekView* view_for_testing() { return view_; }
  content::WebContents* page_for_testing() { return web_contents(); }

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  // Removes the view and stops watching the page; what happens to the page's
  // tab is the caller's decision.
  void TearDown();

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<PeekView> view_ = nullptr;
  gfx::Rect page_area_;

  base::WeakPtrFactory<PeekController> weak_factory_{this};
};

// The home boundary's destination: shows `params` in a peek over `source`'s
// tab when that tab is in a window with a sidebar. False otherwise -- no tab,
// a window without the sidebar, a unit test's window that is not a
// BrowserView -- and then the caller opens a tab.
bool ShowPeekForNavigation(content::WebContents* source,
                           const content::OpenURLParams& params);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PEEK_CONTROLLER_H_
