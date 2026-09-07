// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/rename_field.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield.h"

namespace arcium {

RenameField::RenameField(const std::u16string& initial,
                         FinishedCallback finished)
    : finished_(std::move(finished)) {
  set_controller(this);
  SetText(initial);
  SelectAll(/*reversed=*/false);
}

RenameField::~RenameField() = default;

bool RenameField::HandleKeyEvent(views::Textfield* sender,
                                 const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    Finish(/*commit=*/true);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    Finish(/*commit=*/false);
    return true;
  }
  return false;
}

void RenameField::OnBlur() {
  views::Textfield::OnBlur();
  // Clicking away abandons. It cannot commit: the user never said yes, and a
  // half-typed name should not outlive the click that interrupted it.
  Finish(/*commit=*/false);
}

void RenameField::Finish(bool commit) {
  if (done_ || !finished_) {
    return;
  }
  done_ = true;
  // Whoever handles this removes the field, and that is this object. Running
  // it from a task means the deletion never happens inside the key or focus
  // event this view is still on the stack of.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(finished_), commit, std::u16string(GetText())));
}

BEGIN_METADATA(RenameField)
END_METADATA

}  // namespace arcium
