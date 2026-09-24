// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/mozlz4.h"

#include <array>

#include "base/containers/span_reader.h"

namespace arcium {

namespace {

constexpr std::array<uint8_t, 8> kMagic = {'m', 'o', 'z', 'L',
                                           'z', '4', '0', '\0'};

// Reads an LZ4 length: the nibble, and while it is 15, further bytes added to
// it until one is not 255. Refuses a run past the input or past the cap.
bool ReadLength(base::SpanReader<const uint8_t>& in, size_t& length) {
  if (length != 15) {
    return true;
  }
  uint8_t more = 255;
  while (more == 255) {
    if (!in.ReadU8BigEndian(more)) {
      return false;
    }
    length += more;
    if (length > kMaxMozLz4Size) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::string> DecodeMozLz4(base::span<const uint8_t> file) {
  if (file.size() > kMaxMozLz4Size + 12) {
    return std::nullopt;
  }
  base::SpanReader<const uint8_t> in(file);
  std::optional<base::span<const uint8_t, 8>> magic = in.Read<8>();
  uint32_t declared = 0;
  if (!magic || *magic != base::span(kMagic) ||
      !in.ReadU32LittleEndian(declared) || declared > kMaxMozLz4Size) {
    return std::nullopt;
  }

  std::string out;
  out.reserve(declared);
  while (in.remaining() > 0) {
    uint8_t token = 0;
    in.ReadU8BigEndian(token);
    size_t literals = token >> 4;
    if (!ReadLength(in, literals)) {
      return std::nullopt;
    }
    std::optional<base::span<const uint8_t>> bytes = in.Read(literals);
    if (!bytes || out.size() + literals > declared) {
      return std::nullopt;
    }
    out.append(bytes->begin(), bytes->end());
    // The last sequence is literals only, and ends the block.
    if (in.remaining() == 0) {
      break;
    }
    uint16_t offset = 0;
    if (!in.ReadU16LittleEndian(offset) || offset == 0 || offset > out.size()) {
      return std::nullopt;
    }
    size_t match = token & 0x0f;
    if (!ReadLength(in, match)) {
      return std::nullopt;
    }
    match += 4;
    if (out.size() + match > declared) {
      return std::nullopt;
    }
    // Byte by byte, because a match may overlap the bytes it is copying.
    const size_t from = out.size() - offset;
    for (size_t i = 0; i < match; ++i) {
      out.push_back(out[from + i]);
    }
  }
  if (out.size() != declared) {
    return std::nullopt;
  }
  return out;
}

}  // namespace arcium
