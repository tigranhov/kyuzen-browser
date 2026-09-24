// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/mozlz4.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

// The two vectors in docs/research/zen-arc-import-formats.md, checked there
// against a reference decoder.
const std::vector<uint8_t> kLiteralsOnly = {
    0x6d, 0x6f, 0x7a, 0x4c, 0x7a, 0x34, 0x30, 0x00, 0x07, 0x00,
    0x00, 0x00, 0x70, 0x7b, 0x22, 0x61, 0x22, 0x3a, 0x31, 0x7d};

const std::vector<uint8_t> kOverlappingMatch = {
    0x6d, 0x6f, 0x7a, 0x4c, 0x7a, 0x34, 0x30, 0x00, 0x1c, 0x00, 0x00,
    0x00, 0x48, 0x61, 0x62, 0x63, 0x64, 0x04, 0x00, 0xc0, 0x78, 0x79,
    0x7a, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};

TEST(MozLz4Test, LiteralsOnlyDecode) {
  EXPECT_EQ(std::optional<std::string>("{\"a\":1}"),
            DecodeMozLz4(kLiteralsOnly));
}

// A match whose offset is shorter than its length copies bytes it has just
// written, which is how LZ4 spells a repeat.
TEST(MozLz4Test, AnOverlappingMatchRepeats) {
  EXPECT_EQ(std::optional<std::string>("abcdabcdabcdabcdxyz123456789"),
            DecodeMozLz4(kOverlappingMatch));
}

TEST(MozLz4Test, AShortHeaderIsRefused) {
  EXPECT_FALSE(DecodeMozLz4(
      std::vector<uint8_t>(kLiteralsOnly.begin(), kLiteralsOnly.begin() + 11)));
}

TEST(MozLz4Test, AnotherMagicIsRefused) {
  std::vector<uint8_t> file = kLiteralsOnly;
  file[3] = 'X';
  EXPECT_FALSE(DecodeMozLz4(file));
}

// The declared size is read from the file, so it is never trusted to size an
// allocation beyond the cap.
TEST(MozLz4Test, AnOversizedDeclarationIsRefused) {
  std::vector<uint8_t> file = kLiteralsOnly;
  file[8] = 0x00;
  file[9] = 0x00;
  file[10] = 0x00;
  file[11] = 0x10;  // 256 MiB.
  EXPECT_FALSE(DecodeMozLz4(file));
}

TEST(MozLz4Test, ATruncatedBlockIsRefused) {
  std::vector<uint8_t> file = kLiteralsOnly;
  file.pop_back();
  EXPECT_FALSE(DecodeMozLz4(file));
}

TEST(MozLz4Test, OutputShorterThanDeclaredIsRefused) {
  std::vector<uint8_t> file = kLiteralsOnly;
  file[8] = 0x08;
  EXPECT_FALSE(DecodeMozLz4(file));
}

TEST(MozLz4Test, OutputLongerThanDeclaredIsRefused) {
  std::vector<uint8_t> file = kLiteralsOnly;
  file[8] = 0x06;
  EXPECT_FALSE(DecodeMozLz4(file));
}

TEST(MozLz4Test, AZeroOffsetIsRefused) {
  std::vector<uint8_t> file = kOverlappingMatch;
  file[17] = 0x00;  // The match's offset, low byte.
  EXPECT_FALSE(DecodeMozLz4(file));
}

// An offset reaching back before the first byte written would read memory
// that is not the output.
TEST(MozLz4Test, AnOffsetBeforeTheStartIsRefused) {
  std::vector<uint8_t> file = kOverlappingMatch;
  file[17] = 0x05;
  EXPECT_FALSE(DecodeMozLz4(file));
}

}  // namespace
}  // namespace arcium
