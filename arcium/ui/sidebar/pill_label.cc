// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/pill_label.h"

#include <string_view>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace arcium {
namespace {

constexpr std::string_view kWww = "www.";

// The pages Chromium opens a new tab on. A tab sitting on one of them has
// gone nowhere, which is what the reader is told.
bool IsANewTabPage(const GURL& url) {
  return url.SchemeIs("chrome") &&
         (url.host() == "newtab" || url.host() == "new-tab-page" ||
          url.host() == "new-tab-page-third-party");
}

// The last segment of a file path, with its percent escapes read back, so a
// file called "a report.pdf" is named the way its owner named it.
std::u16string FileName(const GURL& url) {
  std::string_view path = url.path();
  const size_t slash = path.find_last_of('/');
  if (slash != std::string_view::npos) {
    path.remove_prefix(slash + 1);
  }
  if (path.empty()) {
    // A directory listing has no file to name.
    return u"Local file";
  }
  return base::UTF8ToUTF16(
      base::UnescapeURLComponent(path, base::UnescapeRule::SPACES));
}

}  // namespace

std::u16string PillLabel(const GURL& url) {
  if (!url.is_valid() || url.IsAboutBlank() || IsANewTabPage(url)) {
    return u"New tab";
  }

  if (url.SchemeIsFile()) {
    return FileName(url);
  }

  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    // A page belonging to the browser rather than to a site: settings, the
    // extensions list, the downloads page. Its scheme is half of what names
    // it, so both halves are shown.
    if (url.has_host()) {
      return base::UTF8ToUTF16(
          base::StrCat({url.scheme(), "://", url.host()}));
    }
    return base::UTF8ToUTF16(base::StrCat({url.scheme(), ":"}));
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
