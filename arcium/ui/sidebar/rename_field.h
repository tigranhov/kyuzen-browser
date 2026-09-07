// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_RENAME_FIELD_H_
#define ARCIUM_UI_SIDEBAR_RENAME_FIELD_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"

namespace arcium {

// The plain views::Textfield a row shows in place of its label while it is
// being renamed. Enter commits, Escape and focus loss abandon, and exactly
// one of those happens once.
//
// The finish callback almost always destroys this view — committing a title
// changes the model, which rebuilds the list — so it is posted as a task
// rather than run from inside the event that triggered it. Bind it to
// something that outlives a rebuild, or to a weak pointer.
class RenameField : public views::Textfield, public views::TextfieldController {
  METADATA_HEADER(RenameField, views::Textfield)

 public:
  // Called with the text and whether it should be kept.
  using FinishedCallback =
      base::OnceCallback<void(bool commit, const std::u16string& text)>;

  RenameField(const std::u16string& initial, FinishedCallback finished);
  RenameField(const RenameField&) = delete;
  RenameField& operator=(const RenameField&) = delete;
  ~RenameField() override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::Textfield / View:
  void OnBlur() override;

 private:
  void Finish(bool commit);

  FinishedCallback finished_;
  bool done_ = false;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_RENAME_FIELD_H_
