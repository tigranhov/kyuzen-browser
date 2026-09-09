// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/home_boundary.h"

#include "url/gurl.h"

namespace arcium {

bool LinkLeavesHome(const GURL& current, const GURL& target, const GURL& home) {
  return false;
}

}  // namespace arcium
