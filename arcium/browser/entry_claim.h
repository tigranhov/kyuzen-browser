// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_ENTRY_CLAIM_H_
#define ARCIUM_BROWSER_ENTRY_CLAIM_H_

#include "components/tabs/public/tab_interface.h"

namespace arcium {

class ArciumModel;
class TabBinding;

// True when an entry that *still exists*, in the space being drawn, claims
// `handle`.
//
// A binding alone is not enough, and the difference is not hypothetical:
// ArciumModel::ReplaceAll — which ModelStore::Load calls once the first window
// is already interactive — removes entries without touching TabBinding, so a
// tab can be left bound to an entry the model no longer has. Such a tab is a
// Today tab: it must be drawn in Today (or it becomes invisible) and it must
// be archivable (or it becomes immortal as well as invisible).
//
// The space matters for the same reason. Entries carry a space id, and every
// surface — rows(), the archive, search — works in one space at a time, which
// in Stage 2 is always the model's default. An entry of another space claiming
// a tab would put that tab in the same nowhere: claimed, so not a Today tab,
// and drawn by no section that exists. Stage 3 gives windows a space of their
// own, and that is the argument this predicate grows then; until it does,
// "the space being drawn" is default_space_id() and is read from the model.
//
// This lives here rather than on either caller because both SidebarTabModel
// and ArchiveService need exactly this predicate, and two copies of it is how
// the bug comes back.
bool IsClaimedByEntry(const ArciumModel& model,
                      const TabBinding& binding,
                      tabs::TabHandle handle);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_ENTRY_CLAIM_H_
