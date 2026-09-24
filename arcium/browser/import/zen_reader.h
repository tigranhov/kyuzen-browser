// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_IMPORT_ZEN_READER_H_
#define ARCIUM_BROWSER_IMPORT_ZEN_READER_H_

#include <optional>
#include <string_view>

#include "arcium/browser/import/import_plan.h"

namespace arcium {

// Reads Zen's saved sidebar: the decompressed text of a profile's
// zen-sessions.jsonlz4, and that profile's containers.json (empty when there
// is none). Returns nothing only when the session is not JSON describing an
// object; anything Kyuzen cannot take inside it -- an unpinned tab, a folder's
// placeholder, a glance, an address that is not a web page -- is left out and
// the rest kept. See docs/research/zen-arc-import-formats.md, section 2.
std::optional<ImportPlan> ReadZenSession(std::string_view session_json,
                                         std::string_view containers_json);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_IMPORT_ZEN_READER_H_
