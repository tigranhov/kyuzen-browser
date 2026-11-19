// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_COMMAND_BOX_ROW_H_
#define ARCIUM_UI_BROWSER_COMMAND_BOX_ROW_H_

#include <string>
#include "arcium/ui/browser/suggestion_source.h"
#include "base/functional/callback.h"

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace ui {
class ImageModel;
}

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace arcium {

// One answer in the command box: an icon saying what kind of thing it is,
// what it is and where it goes, and at the right what Enter does with it --
// "Switch to tab" for a tab already open, a command's shortcut as keycaps,
// and a return mark on the row Enter would take.
class CommandBoxRow : public views::View {
  METADATA_HEADER(CommandBoxRow, views::View)

 public:
  // Run when the reader clicks this row. `shortcut` is a command's key
  // combination as the platform writes it ("⌘T"), or empty.
  CommandBoxRow(const SuggestionRow& row,
                const std::u16string& shortcut,
                base::RepeatingClosure on_chosen);
  CommandBoxRow(const CommandBoxRow&) = delete;
  CommandBoxRow& operator=(const CommandBoxRow&) = delete;
  ~CommandBoxRow() override;

  void SetSelected(bool selected);
  // The site's own icon, once the profile has answered; until then, and when
  // it has none, the row keeps the glyph it was built with.
  void SetIcon(const ui::ImageModel& icon);

  views::ImageView* icon_for_testing() { return icon_; }
  // What the row says at its right edge -- "Switch to tab" or a command's
  // keycap -- or null when it says nothing there.
  views::Label* aside_for_testing() { return aside_; }
  views::Label* enter_mark_for_testing() { return enter_mark_; }

  // views::View:
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  void UpdateBackground();

  base::RepeatingClosure on_chosen_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> aside_ = nullptr;
  raw_ptr<views::Label> enter_mark_ = nullptr;
  bool selected_ = false;
  bool hovered_ = false;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_COMMAND_BOX_ROW_H_
