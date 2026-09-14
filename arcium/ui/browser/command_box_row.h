// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_COMMAND_BOX_ROW_H_
#define ARCIUM_UI_BROWSER_COMMAND_BOX_ROW_H_

#include "arcium/ui/browser/suggestion_source.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace arcium {

// One answer in the command box: what it is, where it goes, and whether it is
// a tab already open somewhere in this window.
class CommandBoxRow : public views::View {
  METADATA_HEADER(CommandBoxRow, views::View)

 public:
  explicit CommandBoxRow(const SuggestionRow& row);
  CommandBoxRow(const CommandBoxRow&) = delete;
  CommandBoxRow& operator=(const CommandBoxRow&) = delete;
  ~CommandBoxRow() override;

  void SetSelected(bool selected);

  // views::View:
  void OnThemeChanged() override;

 private:
  bool selected_ = false;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_COMMAND_BOX_ROW_H_
