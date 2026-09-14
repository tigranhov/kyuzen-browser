// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/command_box.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "arcium/ui/browser/command_box_row.h"
#include "base/functional/bind.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {
constexpr int kWidth = 560;
constexpr int kFieldHeight = 40;
}  // namespace

CommandBox::CommandBox(BrowserView* browser_view,
                       SuggestionSource* source,
                       OpenCallback on_open)
    : views::BubbleDialogDelegate(/*anchor_view=*/nullptr,
                                  views::BubbleBorder::Arrow::NONE),
      browser_view_(browser_view),
      source_(source),
      on_open_(std::move(on_open)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetShowCloseButton(false);
  set_margins(gfx::Insets(12));
  set_close_on_deactivate(true);
  set_parent_window(browser_view_->GetWidget()->GetNativeView());

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  auto field = std::make_unique<views::Textfield>();
  field->set_controller(this);
  field->SetPlaceholderText(u"Search or enter address");
  field->SetPreferredSize(gfx::Size(kWidth, kFieldHeight));
  field_ = contents->AddChildView(std::move(field));

  auto rows = std::make_unique<views::View>();
  rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  row_container_ = contents->AddChildView(std::move(rows));

  SetContentsView(std::move(contents));
}

CommandBox::~CommandBox() = default;

void CommandBox::FocusField() {
  field_->RequestFocus();
}

void CommandBox::SetText(const std::u16string& text, bool select_all) {
  field_->SetText(text);
  if (select_all) {
    field_->SelectAll(/*reversed=*/false);
  }
  ContentsChanged(field_, text);
}

std::u16string CommandBox::text_for_testing() const {
  return std::u16string(field_->GetText());
}

void CommandBox::SetRowsChangedClosureForTesting(
    base::RepeatingClosure closure) {
  rows_changed_for_testing_ = std::move(closure);
}

void CommandBox::ContentsChanged(views::Textfield* sender,
                                 const std::u16string& new_contents) {
  source_->Start(new_contents, base::BindRepeating(&CommandBox::OnRows,
                                                   weak_factory_.GetWeakPtr()));
}

void CommandBox::OnRows(std::vector<SuggestionRow> rows) {
  rows_ = std::move(rows);
  // The first row is the one Enter takes, so a new set of answers resets the
  // selection: keeping an index into a list that just changed underneath it
  // would send Enter somewhere the reader never looked at.
  selected_ = 0;
  RebuildRowViews();
  SizeToContents();
  if (rows_changed_for_testing_) {
    rows_changed_for_testing_.Run();
  }
}

void CommandBox::RebuildRowViews() {
  row_views_.clear();
  row_container_->RemoveAllChildViews();
  for (size_t i = 0; i < rows_.size(); ++i) {
    auto* view =
        row_container_->AddChildView(std::make_unique<CommandBoxRow>(rows_[i]));
    view->SetSelected(i == selected_);
    row_views_.push_back(view);
  }
}

void CommandBox::Move(int delta) {
  if (rows_.empty()) {
    return;
  }
  const int count = static_cast<int>(rows_.size());
  const int next = (static_cast<int>(selected_) + delta + count) % count;
  selected_ = static_cast<size_t>(next);
  for (size_t i = 0; i < row_views_.size(); ++i) {
    row_views_[i]->SetSelected(i == selected_);
  }
}

void CommandBox::TakeSelectedRow() {
  SuggestionRow chosen;
  if (selected_ < rows_.size()) {
    chosen = rows_[selected_];
  } else {
    // No answers back yet. A full address typed and sent straight away must
    // still go somewhere, so it is classified the way the old box did it.
    const std::u16string text(field_->GetText());
    if (text.empty()) {
      return;
    }
    AutocompleteMatch match;
    AutocompleteClassifierFactory::GetForProfile(browser_view_->GetProfile())
        ->Classify(text, /*in_keyword_mode=*/false,
                   /*allow_exact_keyword_match=*/false,
                   ::metrics::OmniboxEventProto::INVALID_SPEC, &match,
                   /*alternate_nav_url=*/nullptr);
    if (!match.destination_url.is_valid()) {
      return;
    }
    chosen.destination = match.destination_url;
    chosen.title = text;
  }
  GetWidget()->Close();
  if (on_open_) {
    std::move(on_open_).Run(std::move(chosen));
  }
}

bool CommandBox::HandleKeyEvent(views::Textfield* sender,
                                const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_DOWN:
      Move(1);
      return true;
    case ui::VKEY_UP:
      Move(-1);
      return true;
    case ui::VKEY_RETURN:
      TakeSelectedRow();
      return true;
    case ui::VKEY_ESCAPE:
      GetWidget()->Close();
      return true;
    default:
      return false;
  }
}

gfx::Rect CommandBox::GetAnchorRect() const {
  // Centre horizontally over the contents area, a fifth of the way down.
  ContentsContainerView* container =
      browser_view_->GetActiveContentsContainerView();
  const gfx::Rect contents = container ? container->GetBoundsInScreen()
                                       : browser_view_->GetBoundsInScreen();
  const int x = contents.x() + (contents.width() - kWidth) / 2;
  const int y = contents.y() + contents.height() / 5;
  return gfx::Rect(x, y, kWidth, 1);
}

}  // namespace arcium
