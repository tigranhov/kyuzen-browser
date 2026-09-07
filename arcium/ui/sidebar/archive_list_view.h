// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_ARCHIVE_LIST_VIEW_H_
#define ARCIUM_UI_SIDEBAR_ARCHIVE_LIST_VIEW_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

namespace views {
class Button;
class Label;
class ScrollView;
class View;
}  // namespace views

namespace arcium {

// The archived tabs of the active space, newest first, in a bubble anchored
// to the section divider. Clicking a row reopens the page and drops the row.
//
// Deliberately plain, and scoped as throwaway: Stage 6's library replaces it.
// It exists because auto-archive with nowhere to look reads as tab loss, and
// a feature that looks like it eats tabs gets switched off.
//
// A BubbleDialogDelegate rather than a BubbleDialogDelegateView, matching
// QuickEntryBubble: upstream has closed the latter to new subclasses (its
// friend list says so in as many words). So this is not a views::View, and
// its owner creates the Widget with views::BubbleDialogDelegate::CreateBubble
// and keeps this delegate alive for as long as that Widget exists.
//
// The rows are asked for once, here, rather than followed: the archive grows
// without bound and nothing in the sidebar needs it until it is looked at. So
// the list is a snapshot, and it maintains itself against its own clicks
// rather than re-reading.
class ArchiveListView : public views::BubbleDialogDelegate {
 public:
  // `anchor` and `model` must outlive this object.
  ArchiveListView(views::View* anchor, SidebarModel* model);
  ArchiveListView(const ArchiveListView&) = delete;
  ArchiveListView& operator=(const ArchiveListView&) = delete;
  ~ArchiveListView() override;

  // Rows built so far, for tests. Empty until the read comes back.
  size_t row_count_for_testing() const { return rows_.size(); }
  views::Button* row_at_for_testing(size_t index) { return rows_[index]; }
  bool is_empty_message_showing_for_testing() const;

 private:
  // The read's reply. Bound through `weak_factory_`, so a bubble the user
  // dismissed while SQLite was still seeking is simply not told: the delegate
  // is gone with its Widget by then, and the reply lands on nothing.
  void OnRowsRead(std::vector<ArchivedRow> rows);
  void Rebuild();
  // Reopens `row`'s page and takes it out of `rows_`. Not a re-read: the
  // delete is posted to a background sequence, so asking the archive again
  // would race it and could hand back the row that was just clicked.
  void OnRowClicked(ArchivedRow row);

  raw_ptr<SidebarModel> model_;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<views::Label> empty_ = nullptr;
  // Model-side rows, in the order they are drawn. The child views are rebuilt
  // from this, never read back out of.
  std::vector<ArchivedRow> archived_;
  std::vector<raw_ptr<views::Button>> rows_;
  base::WeakPtrFactory<ArchiveListView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_ARCHIVE_LIST_VIEW_H_
