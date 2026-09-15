// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_BOX_COMMANDS_H_
#define ARCIUM_UI_BROWSER_BOX_COMMANDS_H_

#include <string>
#include <string_view>
#include <vector>

namespace arcium {

// Arcium's own commands, which Chromium has no id for. Negative so they can
// never collide with an IDC_ id, which are all positive.
inline constexpr int kBoxCommandNewSpace = -1;
inline constexpr int kBoxCommandToggleSidebar = -2;
inline constexpr int kBoxCommandSiteSearch = -3;

// At most this many command rows, so a short word cannot push every page the
// reader was looking for out of the box.
inline constexpr size_t kMaxCommandRows = 3;

// A browser command the box can run. `id` is an IDC_ command or one of the
// kBoxCommand ids above.
struct BoxCommand {
  int id;
  std::u16string name;
  // Other words a reader might reach for, space separated: "inspect" for
  // Developer tools, "preferences" for Settings. Matched, never shown.
  std::u16string extra_names;
};

// Every command the box offers, in the order they are offered when several
// match.
const std::vector<BoxCommand>& AllBoxCommands();

// The commands `query` names: every word typed must start some word of the
// command's name or extra names, case ignored. Nothing for a query of fewer
// than two letters, and at most kMaxCommandRows.
std::vector<const BoxCommand*> MatchBoxCommands(
    std::u16string_view query,
    const std::vector<BoxCommand>& commands = AllBoxCommands());

// "Search YouTube for cats": what a row searching a site other than the
// default engine says, so the reader can see which site will be searched.
std::u16string SiteSearchTitle(std::u16string_view engine_name,
                               std::u16string_view query);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_BOX_COMMANDS_H_
