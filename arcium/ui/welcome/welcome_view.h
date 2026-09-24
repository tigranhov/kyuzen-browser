// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_WELCOME_WELCOME_VIEW_H_
#define ARCIUM_UI_WELCOME_WELCOME_VIEW_H_

#include <vector>

#include "arcium/ui/welcome/welcome_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
class Link;
class MdTextButton;
}  // namespace views

namespace arcium {

// The welcome: one card over the page area, one step at a time, with the
// sidebar left in view beside it. Back, Skip and Continue in the footer, a
// row of dots between them, and "Skip setup" under the card. Every step can
// be skipped; the last has only Back and "Start browsing".
//
// The command box opens the same card for one step only, to import from Zen
// or Arc at any time.
//
// It draws what the model says and hands every choice straight back; a
// change to the model redraws the step on screen at the next turn of the
// run loop, never inside the click that caused it, which would delete the
// control still handling that click.
class WelcomeView : public views::View, public WelcomeModel::Observer {
  METADATA_HEADER(WelcomeView, views::View)

 public:
  enum class Mode { kWelcome, kImportOnly };

  WelcomeView(WelcomeModel* model, Mode mode, WelcomeStep first);
  WelcomeView(const WelcomeView&) = delete;
  WelcomeView& operator=(const WelcomeView&) = delete;
  ~WelcomeView() override;

  WelcomeStep step() const { return step_; }
  // The steps this card walks through, in order: sync only once it exists,
  // and the first step alone when importing from the command box.
  std::vector<WelcomeStep> Steps() const;

  views::MdTextButton* back_button_for_testing() { return back_; }
  views::MdTextButton* skip_button_for_testing() { return skip_; }
  views::MdTextButton* continue_button_for_testing() { return continue_; }
  views::Link* skip_setup_for_testing() { return skip_setup_; }
  views::Label* position_label_for_testing() { return position_; }
  // The step's own choices, inside the view that scrolls them.
  views::View* choices_for_testing();
  views::View* panel_for_testing() { return panel_; }
  // Redraws now what a model change would redraw at the next turn.
  void RebuildNowForTesting() { Rebuild(); }

  // WelcomeModel::Observer:
  void OnWelcomeChanged() override;

  // views::View:
  void Layout(PassKey) override;

 private:
  class Dots;

  void BuildCard();
  void Rebuild();
  void GoTo(WelcomeStep step);
  void OnBack();
  void OnSkip();
  void OnContinue();
  void OnSkipSetup();
  size_t IndexOf(WelcomeStep step) const;

  const raw_ptr<WelcomeModel> model_;
  const Mode mode_;
  WelcomeStep step_;
  bool rebuild_pending_ = false;

  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::Label> eyebrow_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> subtitle_ = nullptr;
  raw_ptr<views::View> choices_ = nullptr;
  raw_ptr<views::View> divider_ = nullptr;
  raw_ptr<views::View> panel_box_ = nullptr;
  raw_ptr<views::Label> panel_title_ = nullptr;
  raw_ptr<views::Label> panel_subtitle_ = nullptr;
  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Label> panel_note_ = nullptr;
  raw_ptr<views::MdTextButton> back_ = nullptr;
  raw_ptr<Dots> dots_ = nullptr;
  raw_ptr<views::Label> position_ = nullptr;
  raw_ptr<views::MdTextButton> skip_ = nullptr;
  raw_ptr<views::MdTextButton> continue_ = nullptr;
  raw_ptr<views::Link> skip_setup_ = nullptr;

  base::ScopedObservation<WelcomeModel, WelcomeModel::Observer> observation_{
      this};
  base::WeakPtrFactory<WelcomeView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_WELCOME_WELCOME_VIEW_H_
