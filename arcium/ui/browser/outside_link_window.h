// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_OUTSIDE_LINK_WINDOW_H_
#define ARCIUM_UI_BROWSER_OUTSIDE_LINK_WINDOW_H_

#include <memory>

#include "arcium/browser/model/entry_id.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class GURL;

namespace views {
class Widget;
class WidgetDelegate;
class WebView;
}  // namespace views

namespace arcium {

// The small window a link from another application opens in (R4.3): the
// page's domain, one button that moves the page into a space, and the page
// itself. 480 by 640 at the top right of the screen the main window is on,
// cascaded so a second link does not land on the first.
//
// The page is a loose page in the main window's strip -- a real tab the
// sidebar does not draw -- so the button is a promotion rather than a move,
// and the page keeps its history, its scroll and its logins.
//
// Not a views::WidgetDelegateView: that class's constructor is private to a
// closed friend list upstream, so the window is the delegate and a plain
// View is its contents.
class OutsideLinkWindow : public views::WidgetObserver,
                          public content::WebContentsObserver {
 public:
  // Opens `url` in `space`'s storage and shows it. Does nothing, and opens
  // nothing, when the window has no such space.
  static void Open(Browser* browser, SpaceId space, const GURL& url);

  // How many are on screen. For tests, and for the cascade.
  static size_t CountForTesting();
  static OutsideLinkWindow* LastForTesting();

  OutsideLinkWindow(const OutsideLinkWindow&) = delete;
  OutsideLinkWindow& operator=(const OutsideLinkWindow&) = delete;
  ~OutsideLinkWindow() override;

  // Promotes the page into its space, brings the main window forward and
  // closes this window without closing the page.
  void OpenInSpace();
  // Closes the window, and with it the page.
  void Close();

  views::Widget* widget_for_testing() { return widget_.get(); }
  content::WebContents* page_for_testing() { return web_contents(); }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

 private:
  OutsideLinkWindow(Browser* browser,
                    content::WebContents* page,
                    const std::u16string& space_name,
                    const GURL& url,
                    size_t cascade_index);

  // Removes this window from the list of open ones, which owns it. Posted,
  // never called straight from a view or a widget callback.
  void DeleteSoon();

  base::WeakPtr<Browser> browser_;
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  // Set by OpenInSpace: the page is a tab in the main window now, and
  // closing this window must leave it there.
  bool promoted_ = false;
  bool closing_ = false;

  base::WeakPtrFactory<OutsideLinkWindow> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_OUTSIDE_LINK_WINDOW_H_
