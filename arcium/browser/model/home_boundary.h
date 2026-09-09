// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_HOME_BOUNDARY_H_
#define ARCIUM_BROWSER_MODEL_HOME_BOUNDARY_H_

class GURL;

namespace arcium {

// True when a link click from `current` to `target`, in an entry whose stored
// home is `home`, should be pushed out of the tab into a new one.
//
// This is the whole rule. It deliberately knows nothing about navigations:
// deciding that a navigation *is* a link click happens in the caller, so this
// stays testable with three URLs and no fixture.
bool LinkLeavesHome(const GURL& current, const GURL& target, const GURL& home);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_HOME_BOUNDARY_H_
