// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/home_boundary.h"

#include <string_view>

#include "url/gurl.h"

namespace arcium {

namespace {

// Firefox's app-tab rule allows a bare `www.` difference on either side, so
// www.example.com and example.com are one host. Both comparisons below go
// through this, so the two cannot drift apart.
std::string_view HostIgnoringWww(const GURL& url) {
  std::string_view host = url.host();
  if (host.starts_with("www.")) {
    host.remove_prefix(4);
  }
  return host;
}

// A URL with no host never matches anything, including another URL with no
// host: "two tabs that are both about:blank" is not a home to stay on.
bool SameHost(const GURL& a, const GURL& b) {
  return a.has_host() && b.has_host() &&
         HostIgnoringWww(a) == HostIgnoringWww(b);
}

}  // namespace

bool LinkLeavesHome(const GURL& current, const GURL& target, const GURL& home) {
  // Anything that is not a web page with a host is Chromium's business, not
  // ours: mailto:, tel:, custom protocols, about:blank, invalid URLs.
  if (!target.SchemeIsHTTPOrHTTPS() || !target.has_host()) {
    return false;
  }
  // Home moves with you: the comparison is against the page currently loaded,
  // not against where the entry started.
  if (SameHost(target, current)) {
    return false;
  }
  // The deliberate divergence from Zen, which compares only against the
  // current page. Adding the entry's stored home as a second host that keeps
  // a click in the tab can only ever prevent a diversion, never cause one --
  // so this is strictly more forgiving than Firefox's rule. It exists for the
  // sign-in shape: a pinned mail.google.com that has walked to
  // accounts.google.com, where a link back to mail would otherwise be torn
  // out of its own pinned tab.
  //
  // Deleting this clause and the tests named Divergence* restores Firefox's
  // rule exactly. See the design doc, section 2.1.
  if (SameHost(target, home)) {
    return false;
  }
  return true;
}

}  // namespace arcium
