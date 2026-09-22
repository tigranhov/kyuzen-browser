// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/product_version.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/version_info/version_info.h"

namespace arcium {

std::string_view ProductVersion() {
  return KYUZEN_PRODUCT_VERSION;
}

int BuildNumber() {
  return KYUZEN_BUILD_NUMBER;
}

std::u16string AboutVersionText() {
  return base::UTF8ToUTF16(base::StrCat(
      {"Version ", ProductVersion(), " (", base::NumberToString(BuildNumber()),
       "), built on Chromium ", version_info::GetVersionNumber()}));
}

}  // namespace arcium
