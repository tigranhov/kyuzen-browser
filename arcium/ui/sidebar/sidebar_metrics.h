// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_METRICS_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_METRICS_H_

// Layout constants from the Stage 1 spec ("comfortable" density).
namespace arcium::metrics {

inline constexpr int kSidebarWidth = 250;
inline constexpr int kSidebarPadding = 8;
inline constexpr int kRowHeight = 32;
inline constexpr int kRowCornerRadius = 8;
inline constexpr int kRowHorizontalPadding = 8;
inline constexpr int kRowIconTextGap = 9;
inline constexpr int kFaviconSize = 16;
// How far a row inside a folder sits in from a top-level one.
inline constexpr int kFolderIndent = 16;
inline constexpr int kFavoritesPerRow = 4;
inline constexpr int kFavoriteTileGap = 6;
inline constexpr int kNavButtonSize = 26;
inline constexpr int kUrlPillHeight = 28;
inline constexpr int kSpaceChipHeight = 28;
inline constexpr int kProfileBadgeSize = 20;
inline constexpr int kContentInset = 8;
inline constexpr int kContentCornerRadius = 12;
// Width reserved at the top-left for the macOS traffic lights, plus padding.
// The browser overrides this from the frame's real exclusion area at runtime.
inline constexpr int kDefaultCaptionButtonWidth = 70;

}  // namespace arcium::metrics

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_METRICS_H_
