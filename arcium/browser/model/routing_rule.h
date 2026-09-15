// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_ROUTING_RULE_H_
#define ARCIUM_BROWSER_MODEL_ROUTING_RULE_H_

#include <string>
#include <string_view>
#include <vector>

#include "arcium/browser/model/entry_id.h"

class GURL;

namespace arcium {

// "Pages on this site open in this space." The site is a host with `www.`
// removed and lower-cased, and covers that host and every subdomain of it.
// A host rather than a substring of the address, which is what Arc uses, so a
// query string that happens to contain the text cannot trigger it.
struct RoutingRule {
  std::string site;
  SpaceId space_id;
};

// The site a rule for `url` would name: its host, lower-cased, without a
// leading `www.` when something is left after it. Empty for anything that is
// not an http or https page, which no rule can name.
std::string RuleSiteForUrl(const GURL& url);

// The same normalising applied to a site typed or stored as text.
std::string NormaliseRuleSite(std::string_view site);

// Whether `url` is on `site` or a subdomain of it. `github.com` covers
// `gist.github.com` and not `notgithub.com`.
bool SiteMatchesUrl(std::string_view site, const GURL& url);

// The rule with the longest site that covers `url`, so `gist.github.com` can
// be sent somewhere `github.com` is not; null when none does.
const RoutingRule* BestRuleForUrl(const std::vector<RoutingRule>& rules,
                                  const GURL& url);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ROUTING_RULE_H_
