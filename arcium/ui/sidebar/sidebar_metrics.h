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
// The buttons inside the URL pill: small enough that three of them plus the
// domain fit a 250px sidebar without the text eliding on a normal host.
inline constexpr int kPillButtonSize = 20;
inline constexpr int kPillButtonGap = 2;
inline constexpr int kPillIconSize = 14;
// Zen's own stylesheet fades these over 150ms. Copied rather than guessed.
inline constexpr int kPillRevealMs = 150;
// The pinned-extension buttons above the favourites. Smaller than a favourite
// tile on purpose: an extension is a control, not a destination.
inline constexpr int kExtensionButtonSize = 26;
inline constexpr int kExtensionButtonGap = 6;
// The page's own corners. It runs to the window's edges, so the two on the
// window side land on the corners macOS already rounds and read as one
// curve; the two against the sidebar are where this shows as a choice.
inline constexpr int kContentCornerRadius = 12;
inline constexpr int kSpaceChipHeight = 28;
inline constexpr int kProfileBadgeSize = 20;
// Width reserved at the top-left for the macOS traffic lights, plus padding.
// The browser overrides this from the frame's real exclusion area at runtime.
inline constexpr int kDefaultCaptionButtonWidth = 70;

// Where the nav row's buttons sit: the sidebar's own padding, plus the
// vertical margin FlexLayout gives every child by default.
inline constexpr int kNavRowTopMargin = 3;
inline constexpr int kNavRowY = kSidebarPadding + kNavRowTopMargin;

// The height macOS should treat as this window's title bar.
//
// It has no title bar to speak of -- Arcium hides the tab strip and the
// toolbar, so upstream's height comes out near zero and the traffic lights
// end up hard against the top of the window, above everything they sit
// beside. AppKit centres those buttons vertically in the title bar, so a
// height of twice the nav row's centre puts them on the nav row's own centre
// line, level with back, forward and reload.
inline constexpr int kTitlebarHeight = 2 * (kNavRowY + kNavButtonSize / 2);

}  // namespace arcium::metrics

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_METRICS_H_
