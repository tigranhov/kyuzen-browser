// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_FOLDER_H_
#define ARCIUM_BROWSER_MODEL_FOLDER_H_

#include <optional>
#include <string>

#include "arcium/browser/model/entry_id.h"

namespace arcium {

// The deepest a folder tree goes, counted in levels: five means depths 0
// through 4. The sidebar is 250px wide and indents 16px per level, so the
// deepest row still starts 80px in with ~170px left for its title -- tight
// but readable. The model refuses a move past this rather than leaving the
// view to draw something it cannot.
//
// Five and not three, which the acceptance list's "build a folder three deep"
// might suggest: that is a scenario to exercise, not a limit. And a cap of
// three makes CanMoveFolderTo's cycle check unreachable -- moving a folder
// into its own descendant always costs at least two extra levels, so the cap
// would refuse every cycle before the cycle rule was consulted. An invariant
// no input can reach is an invariant no test can cover, and this project has
// already shipped one guard that was wrong precisely because nothing exercised
// it.
inline constexpr int kMaxFolderDepth = 5;

// Holds pinned entries and other folders. Not built on Chromium tab groups: a
// group holds live tabs, and a cold pinned entry has none.
struct Folder {
  FolderId id;
  SpaceId space_id;
  // Absent for a folder at the top level of the Pinned section. ArciumModel
  // is the only thing that may set this, because it is the only thing that
  // can see the whole tree: it refuses a cycle and refuses a move past
  // kMaxFolderDepth.
  std::optional<FolderId> parent_id;
  std::u16string name;
  bool collapsed = false;
  // Numbered 0..n-1 among the folders sharing this one's space *and* parent,
  // so a nested folder's position is a place among its siblings rather than
  // among every folder in the space.
  int position = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_FOLDER_H_
