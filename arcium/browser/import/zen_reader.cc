// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/zen_reader.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// Longer than any folder or split nesting Zen allows; a chain past it is a
// cycle in a damaged file.
constexpr int kMaxChain = 64;

// Firefox writes page titles as the page gave them, so half an emoji arrives
// as a lone escape; replacing it keeps the rest of the file readable.
constexpr int kParseOptions =
    base::JSON_PARSE_RFC | base::JSON_REPLACE_INVALID_CHARACTERS;

std::string_view StringOr(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? std::string_view(*value) : std::string_view();
}

bool BoolOr(const base::DictValue& dict, std::string_view key) {
  return dict.FindBool(key).value_or(false);
}

// Firefox's four built-in containers are named by a translation key until
// their owner renames them.
std::string_view DefaultContainerName(std::string_view l10n_id) {
  static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
      {"user-context-personal", "Personal"},
      {"user-context-work", "Work"},
      {"user-context-banking", "Banking"},
      {"user-context-shopping", "Shopping"},
  };
  for (const auto& [id, name] : kNames) {
    if (id == l10n_id) {
      return name;
    }
  }
  return std::string_view();
}

// The containers a person can see, by id. Hidden ones are Firefox's own.
std::map<int, std::string> ContainerNames(std::string_view containers_json) {
  std::map<int, std::string> names;
  const std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(containers_json, kParseOptions);
  const base::ListValue* identities =
      dict ? dict->FindList("identities") : nullptr;
  if (!identities) {
    return names;
  }
  for (const base::Value& item : *identities) {
    const base::DictValue* identity = item.GetIfDict();
    if (!identity || !BoolOr(*identity, "public")) {
      continue;
    }
    const std::optional<int> id = identity->FindInt("userContextId");
    std::string name(StringOr(*identity, "name"));
    if (name.empty()) {
      name = DefaultContainerName(StringOr(*identity, "l10nId"));
    }
    if (id && !name.empty()) {
      names[*id] = std::move(name);
    }
  }
  return names;
}

// The history entry the tab shows: `index` counts from 1, and without one the
// last entry is current.
const base::DictValue* CurrentEntry(const base::DictValue& tab) {
  const base::ListValue* entries = tab.FindList("entries");
  if (!entries || entries->empty()) {
    return nullptr;
  }
  size_t at = entries->size();
  const std::optional<int> index = tab.FindInt("index");
  if (index && *index >= 1 && static_cast<size_t>(*index) <= entries->size()) {
    at = static_cast<size_t>(*index);
  }
  return (*entries)[at - 1].GetIfDict();
}

struct Page {
  GURL url;
  std::string title;
  bool renamed = false;
};

// A pinned tab goes back to the address it was pinned at, which is what
// Kyuzen's home address means; the page on screen may be somewhere else.
std::optional<Page> PageOf(const base::DictValue& tab) {
  const base::DictValue* current = CurrentEntry(tab);
  const base::DictValue* pinned =
      tab.FindDictByDottedPath("_zenPinnedInitialState.entry");
  const base::DictValue* source =
      pinned && !StringOr(*pinned, "url").empty() ? pinned : current;
  if (!source) {
    return std::nullopt;
  }
  Page page{GURL(StringOr(*source, "url"))};
  if (!IsImportableUrl(page.url)) {
    return std::nullopt;
  }
  page.title = StringOr(tab, "zenStaticLabel");
  page.renamed = !page.title.empty();
  if (page.title.empty()) {
    page.title = StringOr(*source, "title");
  }
  // The current page's title names the pin only while it is the same page.
  if (page.title.empty() && current && current != source &&
      GURL(StringOr(*current, "url")) == page.url) {
    page.title = StringOr(*current, "title");
  }
  if (page.title.empty()) {
    page.title = page.url.spec();
  }
  return page;
}

// Zen's `folders` list holds folders and, marked, the split views nested in
// them. A split view is not a folder: its tabs belong to the folder around it.
struct ZenFolder {
  std::string name;
  std::string space;
  std::string parent;
  bool collapsed = false;
  bool split = false;
};
using ZenFolders = std::map<std::string, ZenFolder>;

// The folder a group id stands for once split views are seen through, or
// empty when it names none.
std::string ResolveFolder(const ZenFolders& folders, std::string id) {
  for (int step = 0; step < kMaxChain && !id.empty(); ++step) {
    const auto it = folders.find(id);
    if (it == folders.end()) {
      return std::string();
    }
    if (!it->second.split) {
      return id;
    }
    id = it->second.parent;
  }
  return std::string();
}

ZenFolders ReadFolders(const base::DictValue& session,
                       const std::set<std::string>& spaces) {
  ZenFolders folders;
  const base::ListValue* list = session.FindList("folders");
  if (!list) {
    return folders;
  }
  for (const base::Value& item : *list) {
    const base::DictValue* folder = item.GetIfDict();
    const std::string id(folder ? StringOr(*folder, "id") : "");
    if (id.empty() || folders.contains(id)) {
      continue;
    }
    folders[id] = ZenFolder{std::string(StringOr(*folder, "name")),
                            std::string(StringOr(*folder, "workspaceId")),
                            std::string(StringOr(*folder, "parentId")),
                            BoolOr(*folder, "collapsed"),
                            BoolOr(*folder, "splitViewGroup")};
  }
  // A folder whose space is gone is dropped, and one whose parent is gone,
  // in another space, or part of a cycle stands at the top of its space.
  std::erase_if(folders, [&](const auto& pair) {
    return !pair.second.split && !spaces.contains(pair.second.space);
  });
  for (auto& [id, folder] : folders) {
    if (folder.split) {
      continue;
    }
    std::string parent = ResolveFolder(folders, folder.parent);
    std::string walk = parent;
    int steps = 0;
    for (; steps < kMaxChain && !walk.empty(); ++steps) {
      walk = ResolveFolder(folders, folders.at(walk).parent);
    }
    if (steps == kMaxChain ||
        (!parent.empty() &&
         (parent == id || folders.at(parent).space != folder.space))) {
      parent.clear();
    }
    folder.parent = std::move(parent);
  }
  return folders;
}

}  // namespace

std::optional<ImportPlan> ReadZenSession(std::string_view session_json,
                                         std::string_view containers_json) {
  const std::optional<base::DictValue> session =
      base::JSONReader::ReadDict(session_json, kParseOptions);
  if (!session) {
    return std::nullopt;
  }
  ImportPlan plan;
  plan.source = ImportSourceKind::kZen;
  const base::ListValue empty;
  const base::ListValue* tabs = session->FindList("tabs");
  tabs = tabs ? tabs : &empty;

  // Spaces, and the separate logins each container stands for.
  const std::map<int, std::string> container_names =
      ContainerNames(containers_json);
  std::map<std::string, int> container_of;
  if (const base::ListValue* spaces = session->FindList("spaces")) {
    for (const base::Value& item : *spaces) {
      const base::DictValue* space = item.GetIfDict();
      const std::string uuid(space ? StringOr(*space, "uuid") : "");
      if (uuid.empty() || container_of.contains(uuid)) {
        continue;
      }
      const int container = space->FindInt("containerTabId").value_or(0);
      container_of[uuid] = container;
      ImportSpace imported{uuid, std::string(StringOr(*space, "name")),
                           std::string(StringOr(*space, "icon")),
                           std::string()};
      if (imported.name.empty()) {
        imported.name = "Space";
      }
      if (!IsEmojiIcon(imported.icon)) {
        imported.icon.clear();
      }
      if (container != 0) {
        imported.profile_key =
            "zen-container-" + base::NumberToString(container);
        if (std::ranges::none_of(plan.profiles, [&](const ImportProfile& p) {
              return p.key == imported.profile_key;
            })) {
          const auto name = container_names.find(container);
          plan.profiles.push_back(
              {imported.profile_key,
               name != container_names.end()
                   ? name->second
                   : "Container " + base::NumberToString(container)});
        }
      }
      plan.spaces.push_back(std::move(imported));
    }
  }
  std::set<std::string> space_keys;
  for (const auto& [uuid, container] : container_of) {
    space_keys.insert(uuid);
  }

  // Folders, in the order they stand in the sidebar: where the first tab in
  // them, placeholders included, stands in `tabs`. Zen writes a nested
  // folder's tabs inline where the folder sits, so a parent always comes
  // before its children, which is the order Kyuzen must create them in.
  const ZenFolders folders = ReadFolders(*session, space_keys);
  std::map<std::string, size_t> first_seen;
  for (size_t i = 0; i < tabs->size(); ++i) {
    const base::DictValue* tab = (*tabs)[i].GetIfDict();
    std::string folder =
        tab ? ResolveFolder(folders, std::string(StringOr(*tab, "groupId")))
            : std::string();
    for (int step = 0; step < kMaxChain && !folder.empty(); ++step) {
      first_seen.emplace(folder, i);
      folder = folders.at(folder).parent;
    }
  }
  std::vector<std::tuple<size_t, int, std::string>> order;
  for (const auto& [id, folder] : folders) {
    if (folder.split) {
      continue;
    }
    int depth = 0;
    for (std::string up = folder.parent; !up.empty() && depth < kMaxChain;
         up = folders.at(up).parent) {
      ++depth;
    }
    const auto seen = first_seen.find(id);
    order.emplace_back(seen != first_seen.end()
                           ? seen->second
                           : std::numeric_limits<size_t>::max(),
                       depth, id);
  }
  std::ranges::sort(order);
  for (const auto& [seen, depth, id] : order) {
    const ZenFolder& folder = folders.at(id);
    plan.folders.push_back(
        {id, folder.space, folder.parent, folder.name, folder.collapsed});
  }

  // Pinned tabs and Essentials, in `tabs` order. An Essential belongs to its
  // container, and Zen shows it in the spaces on that container; one on a
  // container no space uses is shown everywhere, so it goes everywhere.
  for (const base::Value& item : *tabs) {
    const base::DictValue* tab = item.GetIfDict();
    if (!tab || !BoolOr(*tab, "pinned") || BoolOr(*tab, "zenIsEmpty") ||
        BoolOr(*tab, "zenIsGlance")) {
      continue;
    }
    const std::optional<Page> page = PageOf(*tab);
    if (!page) {
      continue;
    }
    if (BoolOr(*tab, "zenEssential")) {
      const int container = tab->FindInt("userContextId").value_or(0);
      const bool claimed = std::ranges::any_of(
          container_of,
          [&](const auto& pair) { return pair.second == container; });
      for (const ImportSpace& space : plan.spaces) {
        if (!claimed || container_of.at(space.key) == container) {
          plan.AddEntry({ImportEntryKind::kFavorite, space.key, std::string(),
                         page->url, page->title, page->renamed});
        }
      }
      continue;
    }
    const std::string space(StringOr(*tab, "zenWorkspace"));
    if (!space_keys.contains(space)) {
      continue;
    }
    std::string folder =
        ResolveFolder(folders, std::string(StringOr(*tab, "groupId")));
    if (!folder.empty() && folders.at(folder).space != space) {
      folder.clear();
    }
    plan.AddEntry({ImportEntryKind::kPinned, space, folder, page->url,
                   page->title, page->renamed});
  }
  return plan;
}

}  // namespace arcium
