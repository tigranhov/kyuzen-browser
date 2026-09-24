// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_IMPORT_MOZLZ4_H_
#define ARCIUM_BROWSER_IMPORT_MOZLZ4_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "base/containers/span.h"

namespace arcium {

// The largest file, and the largest decompressed size, the decoder accepts.
// Session files are a few megabytes; the declared size is read from the file
// itself, so it is capped before anything is allocated for it.
inline constexpr size_t kMaxMozLz4Size = 64u * 1024u * 1024u;

// Decodes Mozilla's "mozLz40\0" wrapper around one LZ4 block, the format
// Firefox and Zen save sessions in. Returns nothing for anything that is not
// exactly such a file: a short or foreign header, a size over the cap, a
// block that reads or writes out of bounds, or output of another length than
// the header declares. The input is another program's file and is treated as
// hostile throughout.
std::optional<std::string> DecodeMozLz4(base::span<const uint8_t> file);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_IMPORT_MOZLZ4_H_
