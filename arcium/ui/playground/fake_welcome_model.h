// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_FAKE_WELCOME_MODEL_H_
#define ARCIUM_UI_PLAYGROUND_FAKE_WELCOME_MODEL_H_

#include <optional>
#include <set>
#include <vector>

#include "arcium/ui/welcome/welcome_model.h"
#include "base/observer_list.h"

namespace arcium {

// The welcome's model with nothing behind it: Zen found on "this Mac" with
// four made-up spaces, five engines, and a default-browser answer that
// arrives as soon as it is asked for. It records what the card asked of it,
// so the view tests can check the card's side of each step, and the
// playground hosts the card on it.
class FakeWelcomeModel : public WelcomeModel {
 public:
  FakeWelcomeModel();
  FakeWelcomeModel(const FakeWelcomeModel&) = delete;
  FakeWelcomeModel& operator=(const FakeWelcomeModel&) = delete;
  ~FakeWelcomeModel() override;

  // Knobs.
  void SetSearching(bool searching);
  void SetSources(std::vector<WelcomeSource> sources);
  void SetSyncAvailable(bool available);
  void SetDefaultBrowser(bool is_default);

  // What the card asked for.
  int apply_count() const { return apply_count_; }
  int finish_count() const { return finish_count_; }
  int pick_file_count() const { return pick_file_count_; }
  const std::vector<WelcomeStep>& reached() const { return reached_; }

  // WelcomeModel:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  bool searching() const override;
  const std::vector<WelcomeSource>& sources() const override;
  std::optional<size_t> chosen_source() const override;
  void ChooseSource(std::optional<size_t> index) override;
  void ChooseProfile(size_t index) override;
  bool kind_chosen(WelcomeKind kind) const override;
  void SetKindChosen(WelcomeKind kind, bool chosen) override;
  void PickFile() override;
  std::vector<WelcomeSpace> spaces() const override;
  void SetSeparateLogins(size_t space, bool separate) override;
  void ApplyImport() override;
  std::vector<std::u16string> engines() const override;
  size_t chosen_engine() const override;
  void ChooseEngine(size_t index) override;
  bool is_default_browser() const override;
  void MakeDefaultBrowser() override;
  bool sync_available() const override;
  std::optional<WelcomeSpace> arrived() const override;
  void StepReached(WelcomeStep step) override;
  void Finish() override;

 private:
  void Notify();

  base::ObserverList<Observer> observers_;
  bool searching_ = false;
  std::vector<WelcomeSource> sources_;
  std::optional<size_t> chosen_;
  std::set<WelcomeKind> left_behind_;
  std::vector<WelcomeSpace> spaces_;
  std::vector<WelcomeSpace> examples_;
  size_t engine_ = 0;
  bool default_browser_ = false;
  bool sync_available_ = false;
  bool applied_ = false;
  int apply_count_ = 0;
  int finish_count_ = 0;
  int pick_file_count_ = 0;
  std::vector<WelcomeStep> reached_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_FAKE_WELCOME_MODEL_H_
