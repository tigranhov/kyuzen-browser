// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/product_version.h"

namespace arcium {

std::string_view ProductVersion() {
  return KYUZEN_PRODUCT_VERSION;
}

int BuildNumber() {
  return KYUZEN_BUILD_NUMBER;
}

}  // namespace arcium
