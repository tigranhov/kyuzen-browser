// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_IMPORT_IMPORT_FINDER_H_
#define ARCIUM_BROWSER_IMPORT_IMPORT_FINDER_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/import/import_plan.h"
#include "base/files/file_path.h"

namespace arcium {

// One place a setup can come from: a Zen profile, or Arc's sidebar.
struct FoundSource {
  FoundSource();
  FoundSource(const FoundSource&);
  FoundSource(FoundSource&&);
  FoundSource& operator=(const FoundSource&);
  FoundSource& operator=(FoundSource&&);
  ~FoundSource();

  ImportSourceKind kind = ImportSourceKind::kZen;
  // The Zen profile's name, for choosing between several; empty for Arc and
  // for a file someone picked.
  std::string profile_name;
  // The file the plan was read from.
  base::FilePath path;
  ImportPlan plan;
};

// Looks under `home`, the person's home folder, for Zen's profiles and Arc's
// sidebar, and reads each one that holds at least one space. Zen's profiles
// come first, the one Zen starts with ahead of the rest, then Arc.
//
// Blocking: it reads files, decompresses and parses them. Post it to the
// thread pool with MayBlock; it touches nothing else.
std::vector<FoundSource> FindSources(const base::FilePath& home);

// Reads a file someone picked, perhaps copied from another Mac: a Zen
// zen-sessions.jsonlz4 or Arc's StorableSidebar.json. Nothing when it is
// neither. Blocking, as above.
std::optional<FoundSource> ReadSourceFile(const base::FilePath& path);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_IMPORT_IMPORT_FINDER_H_
