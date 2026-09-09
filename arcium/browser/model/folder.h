// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_FOLDER_H_
#define ARCIUM_BROWSER_MODEL_FOLDER_H_

#include <optional>
#include <string>

#include "arcium/browser/model/entry_id.h"

namespace arcium {

// The deepest a folder tree goes, counted in levels: four means depths 0
// through 3. The sidebar is 250px wide and indents 16px per level, so the
// deepest row starts 48px in with ~200px left for its title. The model
// refuses a move past this rather than leaving the view to draw something it
// cannot.
//
// **This is a product default, not a structural limit.** Nothing in the model
// or the flattening needs a bound -- BuildSidebarFolders walks an explicit
// stack, so depth costs no C++ stack -- and the spec's R2.5.1 asks for no cap
// at all. What a deeper tree costs is horizontal room, since indentation eats
// a fixed 250px that scrolling never gives back. Zen makes the same tradeoff
// and exposes it as `zen.folders.max-subfolders`; making this a preference is
// the agreed direction when someone actually wants a deeper tree. Until then
// one constant is the whole knob, and every refusal already routes through it.
//
// **Four is the floor. Do not lower it further without reading this.** A cap
// of three makes CanMoveFolderTo's cycle check unreachable: moving a folder
// into its own descendant costs at least two extra levels, so at three the cap
// refuses every cycle before the cycle rule is consulted, and the rule becomes
// dead code that no test can cover. The arithmetic, for the minimal cycle -- a
// folder with one child, dragged into that child -- is new_depth 2 plus a
// subtree height of 1 against `<= kMaxFolderDepth - 1`: 3 <= 3 holds here and
// 3 <= 2 does not. This project has already shipped one guard that was wrong
// precisely because nothing exercised it.
inline constexpr int kMaxFolderDepth = 4;

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
