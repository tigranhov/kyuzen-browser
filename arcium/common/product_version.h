// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_COMMON_PRODUCT_VERSION_H_
#define ARCIUM_COMMON_PRODUCT_VERSION_H_

#include <string>
#include <string_view>

namespace arcium {

// The browser's own version, such as "0.1.0". Separate from Chromium's,
// because two releases of this browser can carry the same Chromium and a
// reader -- or an update deciding whether it has something newer to offer --
// would have no way to tell them apart.
std::string_view ProductVersion();

// A number that only ever increases, one per release. This is the one an
// update compares; the version above is the one a person reads.
int BuildNumber();

// The line the About page shows, such as "Version 0.1.0 (1), built on
// Chromium 146.0.7680.80".
std::u16string AboutVersionText();

}  // namespace arcium

#endif  // ARCIUM_COMMON_PRODUCT_VERSION_H_
