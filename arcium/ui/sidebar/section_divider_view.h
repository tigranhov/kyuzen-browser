// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
class Separator;
}  // namespace views

namespace arcium {

// Hairline between Pinned and Today. Hovering reveals "Clear", which archives
// and closes every Today tab, and beside it "Archived", which opens the list
// of what Clear and the automatic sweep have taken.
class SectionDividerView : public views::View {
  METADATA_HEADER(SectionDividerView, views::View)

 public:
  // `on_archive` may be null, and then there is no archive button at all —
  // which is what an off-the-record window gets, because it has no archive
  // and never will. Absent rather than present-and-disabled on purpose: a
  // control that can never do anything reads as a bug, and this stage has
  // already removed one for that reason.
  SectionDividerView(base::RepeatingClosure on_clear,
                     base::RepeatingClosure on_archive);
  SectionDividerView(const SectionDividerView&) = delete;
  SectionDividerView& operator=(const SectionDividerView&) = delete;
  ~SectionDividerView() override;

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // The archive button, or null when this divider was built without one. The
  // anchor the archive list attaches to, and what a test asks to find out
  // whether the affordance is there at all.
  views::LabelButton* archive_button() { return archive_; }

 private:
  // Reveals or hides the hover buttons together. They are one affordance:
  // showing only the one the pointer happens to be nearest would make the
  // divider flicker as it crossed between them.
  void SetButtonsVisible(bool visible);

  raw_ptr<views::Separator> line_ = nullptr;
  raw_ptr<views::LabelButton> clear_ = nullptr;
  raw_ptr<views::LabelButton> archive_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_
