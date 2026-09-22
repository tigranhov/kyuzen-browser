// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_COMMAND_BOX_H_
#define ARCIUM_UI_BROWSER_COMMAND_BOX_H_

#include <string>
#include <vector>

#include "arcium/ui/browser/suggestion_source.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class BrowserView;

namespace views {
class Textfield;
class View;
}  // namespace views

namespace arcium {

class CommandBoxRow;

// Cmd+T, Cmd+L and a click on the address pill all open this: a floating
// field over the page that answers while you type. A BubbleDialogDelegate
// (not a View): the owner creates the Widget and keeps this alive as long as
// the Widget exists.
class CommandBox : public views::BubbleDialogDelegate,
                   public views::TextfieldController {
 public:
  // What the window does with the row the reader chose.
  using OpenCallback = base::OnceCallback<void(SuggestionRow)>;
  // The open tabs that could share the screen with the page on it, asked for
  // when "Split the screen" is taken. The window knows which those are; the
  // box only draws them.
  using PartnerRowsCallback =
      base::RepeatingCallback<std::vector<SuggestionRow>()>;

  CommandBox(BrowserView* browser_view,
             SuggestionSource* source,
             OpenCallback on_open,
             PartnerRowsCallback partner_rows = PartnerRowsCallback());
  CommandBox(const CommandBox&) = delete;
  CommandBox& operator=(const CommandBox&) = delete;
  ~CommandBox() override;

  // Call once the Widget is shown.
  void FocusField();
  void SetText(const std::u16string& text, bool select_all);

  // What the box is answering. Normally anything the reader types; in
  // kSplitPartner only the open tabs that could share the screen, because
  // splitting is the box's one command that has to name something else
  // before it can act.
  enum class Mode { kAnything, kSplitPartner };
  Mode mode_for_testing() const { return mode_; }

  size_t row_count_for_testing() const { return rows_.size(); }
  const SuggestionRow& row_for_testing(size_t index) const {
    return rows_[index];
  }
  size_t selected_row_for_testing() const { return selected_; }
  CommandBoxRow* row_view_for_testing(size_t index) const {
    return row_views_[index];
  }
  std::u16string text_for_testing() const;
  size_t selected_length_for_testing() const;
  void SetRowsChangedClosureForTesting(base::RepeatingClosure closure);

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::BubbleDialogDelegate:
  gfx::Rect GetAnchorRect() const override;

 private:
  void OnRows(std::vector<SuggestionRow> rows);
  // Asks which tab to share the screen with: the field is cleared, the rows
  // become the open tabs, and the box stays open.
  void EnterSplitPartnerMode();
  // Back to answering anything, with whatever the field held before.
  void LeaveSplitPartnerMode();
  // The partner rows the typed text names, by title.
  void ShowMatchingPartners();
  void RebuildRowViews();
  void Move(int delta);
  void TakeRowAt(size_t index);
  void TakeSelectedRow();
  void Take(SuggestionRow chosen);

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<SuggestionSource> source_;
  OpenCallback on_open_;
  PartnerRowsCallback partner_rows_;
  Mode mode_ = Mode::kAnything;
  // Every tab that could share the screen, asked for once when the mode is
  // entered: the strip cannot change while the box has the keyboard, and
  // re-asking on every keystroke would renumber the rows under a click.
  std::vector<SuggestionRow> partners_;
  raw_ptr<views::Textfield> field_ = nullptr;
  raw_ptr<views::View> row_container_ = nullptr;
  std::vector<SuggestionRow> rows_;
  std::vector<raw_ptr<CommandBoxRow>> row_views_;
  size_t selected_ = 0;
  base::RepeatingClosure rows_changed_for_testing_;

  base::WeakPtrFactory<CommandBox> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_COMMAND_BOX_H_
