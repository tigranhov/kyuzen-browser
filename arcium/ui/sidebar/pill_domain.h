// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_PILL_DOMAIN_H_
#define ARCIUM_UI_SIDEBAR_PILL_DOMAIN_H_

#include <string>

class GURL;

namespace arcium {

// The part of an address the pill shows: where the reader is, and nothing
// else. That is the host, with a leading "www." dropped because it is a
// prefix rather than a name, and with the port kept when there is one. It is
// deliberately not the registrable domain, which would answer "google.com"
// for mail.google.com and tell the reader they are somewhere they are not.
// A page that is not a website -- settings, a local file, a blank tab -- has
// no such place, and gets an empty string.
std::u16string PillDomain(const GURL& url);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_PILL_DOMAIN_H_
