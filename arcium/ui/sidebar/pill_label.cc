// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/pill_domain.h"

#include <string_view>

#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace arcium {
namespace {

constexpr std::string_view kWww = "www.";

}  // namespace

std::u16string PillDomain(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    return std::u16string();
  }

  std::string_view host = url.host();
  // Only when something is left behind it: "www.com" is a name in its own
  // right, and trimming it would leave "com", which is nowhere.
  if (host.starts_with(kWww) &&
      host.find('.', kWww.size()) != std::string_view::npos) {
    host.remove_prefix(kWww.size());
  }

  if (url.has_port()) {
    return base::UTF8ToUTF16(base::StrCat({host, ":", url.port()}));
  }
  return base::UTF8ToUTF16(host);
}

}  // namespace arcium
