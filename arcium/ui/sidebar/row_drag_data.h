// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_ROW_DRAG_DATA_H_
#define ARCIUM_UI_SIDEBAR_ROW_DRAG_DATA_H_

#include <optional>

#include "arcium/browser/model/entry_id.h"

namespace ui {
class ClipboardFormatType;
class OSExchangeData;
}  // namespace ui

namespace arcium {

// What a sidebar drag carries: which row started it, named the way the model
// names it.
//
// Never a pointer, and never a view. A drag runs a nested loop, and while it
// runs the model can change from another window on the same profile and every
// list rebuilds — row views are pooled by position, so the view the drag
// started on may be drawing a different row by the time it ends, or be gone.
// An id survives that; an address does not.
struct RowDragData {
  // Set when the dragged row was backed by a persistent entry.
  EntryId entry_id;
  // Set when what was dragged was a folder header. Never set together with
  // `entry_id`: a payload names exactly one thing, and Read() refuses one
  // that names two.
  FolderId folder_id;
  // The dragged row's index in the tab strip. -1 for a cold entry and for a
  // folder, neither of which has a tab.
  int tab_index = -1;
  // Set when what was dragged is a whole split, by the grip between its
  // halves, rather than one half of it. The pair is named by one of its
  // halves as above. A half dragged on its own leaves its split wherever it
  // is dropped; a pair keeps it.
  bool split_pair = false;

  // The custom clipboard format the payload rides in, registered once for the
  // process. Private to Arcium: nothing outside the sidebar writes it, so a
  // drag from anywhere else fails Read() and is refused.
  static const ui::ClipboardFormatType& Format();

  void Write(ui::OSExchangeData* data) const;
  // std::nullopt when `data` carries no Arcium row, or carries one naming
  // neither an entry nor a tab.
  static std::optional<RowDragData> Read(const ui::OSExchangeData& data);

  // An entry outlives its tab, so it is commanded by id; a Today tab has no
  // entry and is commanded by index. Exactly one of these is true for a
  // payload that Read() returned.
  bool is_entry() const { return entry_id.is_valid(); }
  bool is_folder() const { return folder_id.is_valid(); }
  bool is_tab() const {
    return !entry_id.is_valid() && !folder_id.is_valid() && tab_index >= 0;
  }
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_ROW_DRAG_DATA_H_
