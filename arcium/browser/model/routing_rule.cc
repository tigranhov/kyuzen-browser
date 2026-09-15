// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/routing_rule.h"

#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace arcium {

namespace {

constexpr std::string_view kWww = "www.";

}  // namespace

std::string NormaliseRuleSite(std::string_view site) {
  std::string lower = base::ToLowerASCII(site);
  std::string_view view = lower;
  if (view.starts_with(kWww) &&
      view.find('.', kWww.size()) != std::string_view::npos) {
    view.remove_prefix(kWww.size());
  }
  return std::string(view);
}

std::string RuleSiteForUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    return std::string();
  }
  return NormaliseRuleSite(url.host());
}

bool SiteMatchesUrl(std::string_view site, const GURL& url) {
  if (site.empty()) {
    return false;
  }
  const std::string host = RuleSiteForUrl(url);
  if (host.empty()) {
    return false;
  }
  if (host == site) {
    return true;
  }
  // A subdomain: the host ends with "." followed by the whole site, so the
  // match falls on a label boundary and "notgithub.com" is not "github.com".
  return host.size() > site.size() && std::string_view(host).ends_with(site) &&
         host[host.size() - site.size() - 1] == '.';
}

const RoutingRule* BestRuleForUrl(const std::vector<RoutingRule>& rules,
                                  const GURL& url) {
  const RoutingRule* best = nullptr;
  for (const RoutingRule& rule : rules) {
    if (!SiteMatchesUrl(rule.site, url)) {
      continue;
    }
    if (!best || rule.site.size() > best->site.size()) {
      best = &rule;
    }
  }
  return best;
}

}  // namespace arcium
