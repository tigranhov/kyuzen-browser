// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPACE_GRADIENTS_H_
#define ARCIUM_UI_SIDEBAR_SPACE_GRADIENTS_H_

namespace arcium {

// How many gradient presets a space can choose from. A space stores the index,
// never the colours, so this is the one number the space bar's theme menu and
// the palette have to agree on. Preset 0 is not a pair of its own but the
// colour mixer's, which is what every space that never chose one draws.
inline constexpr int kSpaceGradientCount = 8;

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPACE_GRADIENTS_H_
