// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/box_commands.h"

#include "base/i18n/case_conversion.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "chrome/app/chrome_command_ids.h"

namespace arcium {

namespace {

// The reader's words, lower-cased and split on whitespace.
std::vector<std::u16string> WordsOf(std::u16string_view text) {
  return base::SplitString(base::i18n::ToLower(text), u" \t",
                           base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
}

bool AnyWordStartsWith(const std::vector<std::u16string>& words,
                       std::u16string_view prefix) {
  for (const std::u16string& word : words) {
    if (std::u16string_view(word).starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

// Letters typed, not characters: "  a " is one letter and offers nothing.
size_t LetterCount(const std::vector<std::u16string>& words) {
  size_t count = 0;
  for (const std::u16string& word : words) {
    count += word.size();
  }
  return count;
}

}  // namespace

const std::vector<BoxCommand>& AllBoxCommands() {
  static const base::NoDestructor<std::vector<BoxCommand>> commands({
      {IDC_CLOSE_TAB, u"Close tab", u""},
      {IDC_RESTORE_TAB, u"Reopen closed tab", u"undo restore"},
      {IDC_DUPLICATE_TAB, u"Duplicate tab", u"copy clone"},
      {IDC_COPY_URL, u"Copy link", u"address url share"},
      {IDC_FIND, u"Find in page", u"search"},
      {IDC_PRINT, u"Print", u""},
      {IDC_DEV_TOOLS, u"Developer tools", u"inspect devtools console"},
      {IDC_SHOW_HISTORY, u"History", u""},
      {IDC_SHOW_DOWNLOADS, u"Downloads", u""},
      {IDC_MANAGE_EXTENSIONS, u"Extensions", u"addons plugins"},
      {IDC_OPTIONS, u"Settings", u"preferences options"},
      {IDC_CLEAR_BROWSING_DATA, u"Clear browsing data",
       u"cache cookies history"},
      {IDC_TASK_MANAGER, u"Task manager", u"memory processes"},
      {kBoxCommandNewSpace, u"New space", u"add create"},
      {kBoxCommandToggleSidebar, u"Hide or show sidebar", u"toggle"},
      {kBoxCommandSplit, u"Split the screen", u"side pane panes"},
      {kBoxCommandSiteSearch, u"Site search shortcuts", u"keywords engines"},
      {kBoxCommandImport, u"Import from Zen or Arc",
       u"bring move switch spaces"},
  });
  return *commands;
}

std::vector<const BoxCommand*> MatchBoxCommands(
    std::u16string_view query,
    const std::vector<BoxCommand>& commands) {
  std::vector<const BoxCommand*> matched;
  const std::vector<std::u16string> typed = WordsOf(query);
  // One letter starts half the commands in the list; it is not yet a request
  // for any of them.
  if (LetterCount(typed) < 2) {
    return matched;
  }
  for (const BoxCommand& command : commands) {
    std::vector<std::u16string> words = WordsOf(command.name);
    for (std::u16string& extra : WordsOf(command.extra_names)) {
      words.push_back(std::move(extra));
    }
    bool all = true;
    for (const std::u16string& word : typed) {
      if (!AnyWordStartsWith(words, word)) {
        all = false;
        break;
      }
    }
    if (all) {
      matched.push_back(&command);
      if (matched.size() == kMaxCommandRows) {
        break;
      }
    }
  }
  return matched;
}

std::u16string SiteSearchTitle(std::u16string_view engine_name,
                               std::u16string_view query) {
  return base::StrCat({u"Search ", engine_name, u" for ", query});
}

}  // namespace arcium
