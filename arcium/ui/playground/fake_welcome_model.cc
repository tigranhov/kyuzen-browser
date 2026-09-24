// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/fake_welcome_model.h"

#include <utility>

namespace arcium {
namespace {

WelcomeSpace Space(std::u16string icon,
                   std::u16string name,
                   size_t pinned,
                   std::u16string origin,
                   bool separate) {
  WelcomeSpace space;
  space.icon = std::move(icon);
  space.name = std::move(name);
  space.pinned = pinned;
  space.origin = std::move(origin);
  space.separate_logins = separate;
  space.had_separate_logins = separate;
  return space;
}

}  // namespace

FakeWelcomeModel::FakeWelcomeModel() {
  WelcomeSource zen;
  zen.kind = WelcomeSource::Kind::kZen;
  zen.name = u"Zen";
  zen.detail = u"Found on this Mac";
  zen.spaces = 4;
  zen.pinned = 38;
  zen.favorites = 6;
  zen.folders = 4;
  sources_ = {zen};
  chosen_ = 0;
  spaces_ = {Space(u"", u"Personal", 12, u"from Zen", true),
             Space(u"", u"Work", 14, u"from Zen", true),
             Space(u"", u"Research", 8, u"from Zen", false),
             Space(u"", u"Weekend", 4, u"from Zen", false)};
  spaces_[1].rows = {{u"Team board", false},
                     {u"Project Alder", false},
                     {u"Design notes", false},
                     {u"Resources", true}};
  examples_ = {Space(u"", u"Personal", 0, u"Example", false),
               Space(u"", u"Work", 0, u"Example", false)};
}

FakeWelcomeModel::~FakeWelcomeModel() = default;

void FakeWelcomeModel::SetSearching(bool searching) {
  searching_ = searching;
  Notify();
}

void FakeWelcomeModel::SetSources(std::vector<WelcomeSource> sources) {
  sources_ = std::move(sources);
  chosen_ = sources_.empty() ? std::nullopt : std::optional<size_t>(0);
  Notify();
}

void FakeWelcomeModel::SetSyncAvailable(bool available) {
  sync_available_ = available;
  Notify();
}

void FakeWelcomeModel::SetDefaultBrowser(bool is_default) {
  default_browser_ = is_default;
  Notify();
}

void FakeWelcomeModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FakeWelcomeModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool FakeWelcomeModel::searching() const {
  return searching_;
}

const std::vector<WelcomeSource>& FakeWelcomeModel::sources() const {
  return sources_;
}

std::optional<size_t> FakeWelcomeModel::chosen_source() const {
  return chosen_;
}

void FakeWelcomeModel::ChooseSource(std::optional<size_t> index) {
  chosen_ = index;
  Notify();
}

void FakeWelcomeModel::ChooseProfile(size_t index) {
  if (chosen_) {
    sources_[*chosen_].profile = index;
  }
  Notify();
}

bool FakeWelcomeModel::kind_chosen(WelcomeKind kind) const {
  return !left_behind_.contains(kind);
}

void FakeWelcomeModel::SetKindChosen(WelcomeKind kind, bool chosen) {
  if (chosen) {
    left_behind_.erase(kind);
  } else {
    left_behind_.insert(kind);
  }
  Notify();
}

void FakeWelcomeModel::PickFile() {
  ++pick_file_count_;
}

std::vector<WelcomeSpace> FakeWelcomeModel::spaces() const {
  return chosen_ ? spaces_ : examples_;
}

void FakeWelcomeModel::SetSeparateLogins(size_t space, bool separate) {
  std::vector<WelcomeSpace>& list = chosen_ ? spaces_ : examples_;
  if (space < list.size()) {
    list[space].separate_logins = separate;
  }
  Notify();
}

void FakeWelcomeModel::ApplyImport() {
  ++apply_count_;
  applied_ = chosen_.has_value();
  Notify();
}

std::vector<std::u16string> FakeWelcomeModel::engines() const {
  return {u"Google", u"DuckDuckGo", u"Bing", u"Kagi", u"Ecosia"};
}

size_t FakeWelcomeModel::chosen_engine() const {
  return engine_;
}

void FakeWelcomeModel::ChooseEngine(size_t index) {
  engine_ = index;
  Notify();
}

bool FakeWelcomeModel::is_default_browser() const {
  return default_browser_;
}

void FakeWelcomeModel::MakeDefaultBrowser() {
  // macOS asks, and the answer comes later; here it is always yes.
  SetDefaultBrowser(true);
}

bool FakeWelcomeModel::sync_available() const {
  return sync_available_;
}

std::optional<WelcomeSpace> FakeWelcomeModel::arrived() const {
  if (!applied_) {
    return std::nullopt;
  }
  return spaces_[1];
}

void FakeWelcomeModel::StepReached(WelcomeStep step) {
  reached_.push_back(step);
}

void FakeWelcomeModel::Finish() {
  ++finish_count_;
}

void FakeWelcomeModel::Notify() {
  for (Observer& observer : observers_) {
    observer.OnWelcomeChanged();
  }
}

}  // namespace arcium
