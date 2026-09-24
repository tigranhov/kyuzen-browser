// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_IMPORT_ARC_READER_H_
#define ARCIUM_BROWSER_IMPORT_ARC_READER_H_

#include <optional>
#include <string_view>

#include "arcium/browser/import/import_plan.h"

namespace arcium {

// Reads Arc's saved sidebar, the text of StorableSidebar.json. Returns
// nothing when the text is not JSON describing an object with a sidebar in
// it; anything Kyuzen cannot take inside it -- Today's tabs, notes, easels, an
// item kind nobody has seen, an item that holds itself -- is left out and the
// rest kept. See docs/research/zen-arc-import-formats.md, section 1.
//
// Arc's separate logins are its profiles, which the file names only by their
// folder ("Profile 1"); the plan names them so, and the finder, which can see
// Arc's own list of profile names, renames them.
std::optional<ImportPlan> ReadArcSidebar(std::string_view json);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_IMPORT_ARC_READER_H_
