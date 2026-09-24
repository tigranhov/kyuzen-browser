// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_IMPORT_IMPORT_APPLIER_H_
#define ARCIUM_BROWSER_IMPORT_IMPORT_APPLIER_H_

#include <cstddef>
#include <set>
#include <string>

#include "arcium/browser/import/import_plan.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"

namespace arcium {

// What the person chose on the welcome's first two steps.
struct ImportChoices {
  ImportChoices();
  ImportChoices(const ImportChoices&);
  ImportChoices& operator=(const ImportChoices&);
  ~ImportChoices();

  // Which kinds of thing come across. Without spaces, everything lands in
  // `into`; without pinned tabs there is nothing for folders to hold.
  bool spaces = true;
  bool pinned = true;
  bool favorites = true;
  bool folders = true;
  SpaceId into;
  // The plan's spaces that keep separate logins. The welcome starts with the
  // ones that had them in the source; any space not listed shares the logins.
  std::set<std::string> separate_logins;
  // How many colours a profile can take. Each new profile takes the next one
  // after those already in use, so two imported profiles never look alike.
  int profile_colors = 1;
};

// The choices the welcome starts from: everything, with separate logins
// wherever the source had them.
ImportChoices DefaultChoices(const ImportPlan& plan);

// What an import added. Things the model already had are not counted.
struct ImportResult {
  size_t spaces = 0;
  size_t profiles = 0;
  size_t folders = 0;
  size_t pinned = 0;
  size_t favorites = 0;
  // The plan's first space as it now stands in the model, newly made or
  // already there; invalid when the plan brought none.
  SpaceId first_space;
  // The space a fresh model starts with, when it still held nothing and the
  // import brought spaces of its own. The caller removes it once another
  // space is on screen, so no empty space is left behind.
  SpaceId empty_starter;
};

// Brings `plan` into `model` as `choices` say. Adds only what is not there
// already, which makes a second import of the same source add nothing: a
// space matches by name, a folder by name within its parent, and an entry by
// address within its space and kind. A folder deeper than Kyuzen draws hands
// its contents to the deepest folder it may have. Nothing loads: every entry
// arrives cold, as a pinned entry does after a relaunch.
ImportResult ApplyImportPlan(const ImportPlan& plan,
                             const ImportChoices& choices,
                             ArciumModel& model);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_IMPORT_IMPORT_APPLIER_H_
