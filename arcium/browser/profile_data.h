// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_PROFILE_DATA_H_
#define ARCIUM_BROWSER_PROFILE_DATA_H_

#include "arcium/browser/model/entry_id.h"
#include "base/functional/callback_forward.h"

namespace content {
class BrowserContext;
}

namespace arcium {

// Removes everything a site keeps in `profile`: cookies, site storage and
// cache, and nothing else -- history, bookmarks and passwords are shared
// between profiles and are not touched. `done` runs when the removal has
// finished. The profile's tabs are left as they are, showing what they
// already had, as Chrome's own clear does.
//
// A profile nothing has opened this session has its storage built in order
// to be cleared; there is no way to remove a cookie store that does not
// exist yet.
void ClearArciumProfileData(content::BrowserContext* context,
                            const ProfileId& profile,
                            base::OnceClosure done);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_PROFILE_DATA_H_
