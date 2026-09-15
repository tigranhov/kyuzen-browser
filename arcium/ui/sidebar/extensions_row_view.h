// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_EXTENSIONS_ROW_VIEW_H_
#define ARCIUM_UI_SIDEBAR_EXTENSIONS_ROW_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace arcium {

// The pinned extensions, above the favourites. It draws none of them itself:
// Chromium already builds a strip of extension buttons, and this is where
// that strip is put and how it is laid out -- in lines that wrap, because the
// strip's own layout is one line that drops whatever does not fit, and a
// sidebar this narrow holds about seven.
//
// It names no extension type, because this target must not depend on
// //chrome: it hosts a view and positions that view's children.
class ExtensionsRowView : public views::View, public views::ViewObserver {
  METADATA_HEADER(ExtensionsRowView, views::View)

 public:
  ExtensionsRowView();
  ExtensionsRowView(const ExtensionsRowView&) = delete;
  ExtensionsRowView& operator=(const ExtensionsRowView&) = delete;
  ~ExtensionsRowView() override;

  // Takes the strip of buttons. Whatever it holds, this row lays out.
  views::View* SetHostedView(std::unique_ptr<views::View> view);
  bool has_hosted_view() const { return hosted_ != nullptr; }

  // One button of the strip to leave out: it is neither placed in the row nor
  // counted towards its height. The strip carries a button for opening the
  // extensions menu and the pill has that button already, and the strip's own
  // layout keeps deciding when its button shows -- so the row steps around it
  // rather than hiding it, which that layout would undo. Named by the host,
  // because this target must not know what an extensions menu button is.
  void SetSkippedButton(views::View* button);

  // views::View:
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // views::ViewObserver. Pinning and unpinning add and remove buttons, and
  // the row's height follows them.
  void OnChildViewAdded(views::View* observed, views::View* child) override;
  void OnChildViewRemoved(views::View* observed, views::View* child) override;
  void OnViewVisibilityChanged(views::View* observed,
                               views::View* starting_view,
                               bool visible) override;

 private:
  int ButtonsPerLine(int width) const;
  int VisibleButtonCount() const;

  raw_ptr<views::View> hosted_ = nullptr;
  raw_ptr<views::View> skipped_ = nullptr;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_EXTENSIONS_ROW_VIEW_H_
