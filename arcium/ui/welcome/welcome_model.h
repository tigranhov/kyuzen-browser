// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_WELCOME_WELCOME_MODEL_H_
#define ARCIUM_UI_WELCOME_WELCOME_MODEL_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "base/observer_list_types.h"

namespace arcium {

// The welcome's steps, in order. Sync is shown only once folder sync exists.
enum class WelcomeStep {
  kSetup,
  kLogins,
  kSearch,
  kDefaultBrowser,
  kSync,
  kDone,
};

// What the first step lets a person leave behind.
enum class WelcomeKind { kSpaces, kPinned, kFavorites, kFolders };

// A browser the welcome can bring a setup from.
struct WelcomeSource {
  WelcomeSource();
  WelcomeSource(const WelcomeSource&);
  WelcomeSource& operator=(const WelcomeSource&);
  ~WelcomeSource();

  enum class Kind { kZen, kArc };
  Kind kind = Kind::kZen;
  // "Zen", "Arc".
  std::u16string name;
  // Where it was found: "Found on this Mac", or the picked file's name.
  std::u16string detail;
  // The browser's profiles that hold spaces, when there are several to
  // choose between; empty otherwise.
  std::vector<std::u16string> profiles;
  size_t profile = 0;
  // What the chosen profile holds.
  size_t spaces = 0;
  size_t pinned = 0;
  size_t favorites = 0;
  size_t folders = 0;
};

// A space as the welcome shows it: on the way in, or as it arrived.
struct WelcomeSpace {
  WelcomeSpace();
  WelcomeSpace(const WelcomeSpace&);
  WelcomeSpace& operator=(const WelcomeSpace&);
  ~WelcomeSpace();

  // One emoji, or empty to draw the first letter of the name.
  std::u16string icon;
  std::u16string name;
  size_t pinned = 0;
  // "from Zen", or "Example" for the spaces Start fresh suggests.
  std::u16string origin;
  bool separate_logins = false;
  // Whether it kept its logins apart in the source browser, on a Zen
  // container or an Arc profile of its own.
  bool had_separate_logins = false;
  // The first rows of its pinned section, for the last step's picture.
  struct Row {
    std::u16string title;
    bool folder = false;
  };
  std::vector<Row> rows;
};

// Everything the welcome card shows and every choice it hands back. The
// browser implements it over the finder, the model, the search engines and
// the system's default-browser setting; the playground and the tests use a
// fake. Every change is announced to observers, and the card redraws the
// step on screen from here rather than keeping its own copy.
class WelcomeModel {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnWelcomeChanged() = 0;
  };

  virtual ~WelcomeModel() = default;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  // Bring your setup. The search for Zen and Arc starts when the card is on
  // screen and runs off the UI thread; until it answers there is nothing to
  // choose.
  virtual bool searching() const = 0;
  virtual const std::vector<WelcomeSource>& sources() const = 0;
  // The chosen source, or nothing for Start fresh.
  virtual std::optional<size_t> chosen_source() const = 0;
  virtual void ChooseSource(std::optional<size_t> index) = 0;
  virtual void ChooseProfile(size_t index) = 0;
  virtual bool kind_chosen(WelcomeKind kind) const = 0;
  virtual void SetKindChosen(WelcomeKind kind, bool chosen) = 0;
  // Asks for a file copied from another Mac; a readable one joins sources()
  // and is chosen.
  virtual void PickFile() = 0;

  // The spaces the chosen source will bring, or with Start fresh the two
  // example spaces, Personal and Work.
  virtual std::vector<WelcomeSpace> spaces() const = 0;
  virtual void SetSeparateLogins(size_t space, bool separate) = 0;
  // Brings the chosen source over as chosen. Adds only what is not there
  // yet, so running it again after going back adds only what changed.
  virtual void ApplyImport() = 0;

  // Search engine: the engines this country is offered, one chosen.
  virtual std::vector<std::u16string> engines() const = 0;
  virtual size_t chosen_engine() const = 0;
  virtual void ChooseEngine(size_t index) = 0;

  // Default browser. True once the system says so.
  virtual bool is_default_browser() const = 0;
  virtual void MakeDefaultBrowser() = 0;

  // Sync. False until folder sync exists, which hides the step.
  virtual bool sync_available() const = 0;

  // You're set: the first imported space as it now stands, if one arrived.
  virtual std::optional<WelcomeSpace> arrived() const = 0;

  // Progress. The card reports each step it moves forward to, so a quit
  // brings the welcome back there; Finish ends it for good.
  virtual void StepReached(WelcomeStep step) = 0;
  virtual void Finish() = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_WELCOME_WELCOME_MODEL_H_
