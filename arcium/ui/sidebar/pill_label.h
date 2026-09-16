// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_PILL_LABEL_H_
#define ARCIUM_UI_SIDEBAR_PILL_LABEL_H_

#include <string>

class GURL;

namespace arcium {

// The one line the pill shows: where the reader is, and nothing else.
//
// For a website that is the host, with a leading "www." dropped because it is
// a prefix rather than a name, and with the port kept when there is one. It is
// deliberately not the registrable domain, which would answer "google.com" for
// mail.google.com and tell the reader they are somewhere they are not.
//
// A page that is not a website still gets a short label rather than an empty
// pill: a tab that has gone nowhere says "New tab", a local file is named by
// its file, and a page belonging to the browser says its scheme and host.
std::u16string PillLabel(const GURL& url);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_PILL_LABEL_H_
