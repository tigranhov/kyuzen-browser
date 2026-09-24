// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/arc_reader.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/values.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// Deeper than any sidebar a person builds; past it the file is damaged.
constexpr int kMaxDepth = 256;

constexpr int kParseOptions =
    base::JSON_PARSE_RFC | base::JSON_REPLACE_INVALID_CHARACTERS;

std::string_view StringOr(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? std::string_view(*value) : std::string_view();
}

// Arc saves an ordered dictionary as one flat list, a key and then its value.
// A key followed by anything but an id is a damaged pair and is dropped.
std::vector<std::pair<const base::Value*, std::string>> IdPairs(
    const base::ListValue* list) {
  std::vector<std::pair<const base::Value*, std::string>> pairs;
  for (size_t i = 0; list && i + 1 < list->size(); i += 2) {
    if (const std::string* id = (*list)[i + 1].GetIfString()) {
      pairs.emplace_back(&(*list)[i], *id);
    }
  }
  return pairs;
}

// The spaces and items lists carry each object after its id, and each object
// repeats its id, so the objects alone are the list; a pair out of step
// loses nothing.
std::vector<std::pair<std::string, const base::DictValue*>> Objects(
    const base::ListValue* list) {
  std::vector<std::pair<std::string, const base::DictValue*>> objects;
  if (!list) {
    return objects;
  }
  for (const base::Value& value : *list) {
    const base::DictValue* object = value.GetIfDict();
    const std::string id(object ? StringOr(*object, "id") : "");
    if (!id.empty()) {
      objects.emplace_back(id, object);
    }
  }
  return objects;
}

// Which of Arc's profiles a space or a favourites row belongs to. The default
// profile has no folder name; another has its folder's name and the Mac
// that made it.
struct ProfileTag {
  std::string directory;
  std::string machine;
};

std::optional<ProfileTag> ReadProfileTag(const base::Value& value) {
  const base::DictValue* tag = value.GetIfDict();
  if (!tag) {
    return std::nullopt;
  }
  // Both `{"default": true}` and `{"default": {}}` are seen; the key is what
  // marks it.
  if (tag->contains("default")) {
    return ProfileTag();
  }
  const base::DictValue* custom = tag->FindDictByDottedPath("custom._0");
  const std::string directory(custom ? StringOr(*custom, "directoryBasename")
                                     : "");
  // One folder's name, never a path.
  if (directory.empty() || directory == "." || directory == ".." ||
      directory.find_first_of("/\\") != std::string::npos) {
    return std::nullopt;
  }
  return ProfileTag{directory, std::string(StringOr(*custom, "machineID"))};
}

std::string IconOf(const base::DictValue& space) {
  const base::DictValue* icon =
      space.FindDictByDottedPath("customInfo.iconType");
  if (!icon) {
    return std::string();
  }
  const std::string_view text = StringOr(*icon, "emoji_v2");
  if (IsEmojiIcon(text)) {
    return std::string(text);
  }
  // Older files keep only the emoji's first code point.
  const std::optional<int> code = icon->FindInt("emoji");
  std::string emoji;
  if (code && *code > 0x7F && base::IsValidCharacter(*code)) {
    base::WriteUnicodeCharacter(*code, &emoji);
  }
  return emoji;
}

// A space's pinned section, found by its marker: files have the pinned and
// Today sections in either order.
std::string PinnedSectionOf(const base::DictValue& space) {
  for (const auto& [marker, id] : IdPairs(space.FindList("containerIDs"))) {
    if (marker->is_string() && marker->GetString() == "pinned") {
      return id;
    }
  }
  for (const auto& [marker, id] : IdPairs(space.FindList("newContainerIDs"))) {
    if (marker->is_dict() && marker->GetDict().contains("pinned")) {
      return id;
    }
  }
  return std::string();
}

// The sidebar of Arc's main window is the section after the `global`
// marker; the others belong to Little Arc windows.
const base::DictValue* MainSidebar(const base::ListValue& containers) {
  for (size_t i = 0; i + 1 < containers.size(); ++i) {
    const base::DictValue* marker = containers[i].GetIfDict();
    if (marker && marker->contains("global")) {
      return containers[i + 1].GetIfDict();
    }
  }
  return nullptr;
}

struct Page {
  GURL url;
  std::string title;
  bool renamed = false;
};

// Walks Arc's item tree once. Every item is taken at most once, which is
// also what stops an item that holds itself.
class ItemWalker {
 public:
  ItemWalker(const base::DictValue& sidebar, ImportPlan& plan) : plan_(plan) {
    for (const auto& [id, item] : Objects(sidebar.FindList("items"))) {
      items_.emplace(id, item);
    }
  }

  // A pinned section: its tabs, its folders, and the tabs of its split
  // views, which arrive as ordinary pinned tabs.
  void Pinned(const std::string& id,
              const std::string& space,
              const std::string& folder,
              int depth) {
    const base::DictValue* item = Visit(id, depth);
    if (!item) {
      return;
    }
    if (const std::optional<Page> page = PageOf(*item)) {
      plan_->AddEntry({ImportEntryKind::kPinned, space, folder, page->url,
                       page->title, page->renamed});
      return;
    }
    const base::DictValue* data = item->FindDict("data");
    std::string inner = folder;
    if (data && data->contains("list")) {
      std::string name(StringOr(*item, "title"));
      plan_->folders.push_back(
          {id, space, folder, name.empty() ? "Untitled folder" : name, false});
      inner = id;
    } else if (!data || !(data->contains("splitView") ||
                          data->contains("itemContainer"))) {
      return;
    }
    for (const std::string& child : ChildrenOf(*item)) {
      Pinned(child, space, inner, depth + 1);
    }
  }

  // A favourites row: its tabs, and the tabs of its split views. A row has
  // never been seen holding a folder; if one does, its tabs come along.
  void Favorites(const std::string& id, int depth, std::vector<Page>& pages) {
    const base::DictValue* item = Visit(id, depth);
    if (!item) {
      return;
    }
    if (std::optional<Page> page = PageOf(*item)) {
      pages.push_back(std::move(*page));
      return;
    }
    const base::DictValue* data = item->FindDict("data");
    if (data && (data->contains("list") || data->contains("splitView") ||
                 data->contains("itemContainer"))) {
      for (const std::string& child : ChildrenOf(*item)) {
        Favorites(child, depth + 1, pages);
      }
    }
  }

 private:
  const base::DictValue* Visit(const std::string& id, int depth) {
    if (depth > kMaxDepth || !seen_.insert(id).second) {
      return nullptr;
    }
    const auto it = items_.find(id);
    return it != items_.end() ? it->second : nullptr;
  }

  static std::vector<std::string> ChildrenOf(const base::DictValue& item) {
    std::vector<std::string> children;
    if (const base::ListValue* ids = item.FindList("childrenIds")) {
      for (const base::Value& id : *ids) {
        if (id.is_string()) {
          children.push_back(id.GetString());
        }
      }
    }
    return children;
  }

  // A tab's own name is the one its owner gave it, then the page's title.
  static std::optional<Page> PageOf(const base::DictValue& item) {
    const base::DictValue* tab = item.FindDictByDottedPath("data.tab");
    if (!tab) {
      return std::nullopt;
    }
    Page page{GURL(StringOr(*tab, "savedURL")),
              std::string(StringOr(item, "title"))};
    if (!IsImportableUrl(page.url)) {
      return std::nullopt;
    }
    page.renamed = !page.title.empty();
    if (page.title.empty()) {
      page.title = StringOr(*tab, "savedTitle");
    }
    if (page.title.empty()) {
      page.title = page.url.spec();
    }
    return page;
  }

  const raw_ref<ImportPlan> plan_;
  std::map<std::string, const base::DictValue*> items_;
  std::set<std::string> seen_;
};

}  // namespace

std::optional<ImportPlan> ReadArcSidebar(std::string_view json) {
  const std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(json, kParseOptions);
  const base::ListValue* containers =
      root ? root->FindListByDottedPath("sidebar.containers") : nullptr;
  if (!containers) {
    return std::nullopt;
  }
  ImportPlan plan;
  plan.source = ImportSourceKind::kArc;
  const base::DictValue* sidebar = MainSidebar(*containers);
  if (!sidebar) {
    return plan;
  }
  ItemWalker walker(*sidebar, plan);

  // Spaces in their order, each with its profile and pinned section.
  std::vector<ProfileTag> tags;
  std::vector<std::string> sections;
  for (const auto& [id, space] : Objects(sidebar->FindList("spaces"))) {
    if (std::ranges::any_of(
            plan.spaces, [&](const ImportSpace& s) { return s.key == id; })) {
      continue;
    }
    const base::Value* profile = space->Find("profile");
    const ProfileTag tag = (profile ? ReadProfileTag(*profile) : std::nullopt)
                               .value_or(ProfileTag());
    ImportSpace imported{id, std::string(StringOr(*space, "title")),
                         IconOf(*space), std::string()};
    if (imported.name.empty()) {
      imported.name = "Space";
    }
    if (!tag.directory.empty()) {
      imported.profile_key = "arc-profile-" + tag.directory;
      if (std::ranges::none_of(plan.profiles, [&](const ImportProfile& p) {
            return p.key == imported.profile_key;
          })) {
        plan.profiles.push_back({imported.profile_key, tag.directory});
      }
    }
    plan.spaces.push_back(std::move(imported));
    tags.push_back(tag);
    sections.push_back(PinnedSectionOf(*space));
  }
  for (size_t i = 0; i < plan.spaces.size(); ++i) {
    if (!sections[i].empty()) {
      walker.Pinned(sections[i], plan.spaces[i].key, std::string(), 0);
    }
  }

  // Favourites belong to a profile, one row for each Mac that has used it.
  struct Row {
    ProfileTag tag;
    std::vector<Page> pages;
  };
  std::vector<Row> rows;
  for (const auto& [marker, id] :
       IdPairs(sidebar->FindList("topAppsContainerIDs"))) {
    if (std::optional<ProfileTag> tag = ReadProfileTag(*marker)) {
      rows.push_back({std::move(*tag), {}});
      walker.Favorites(id, 0, rows.back().pages);
    }
  }
  // A space shows its own Mac's row. Nothing says which Mac this is, so when
  // that row is empty or missing the space takes every row of its profile.
  for (size_t i = 0; i < plan.spaces.size(); ++i) {
    const ProfileTag& tag = tags[i];
    bool exact = false;
    for (const Row& row : rows) {
      exact |= row.tag.directory == tag.directory &&
               row.tag.machine == tag.machine && !row.pages.empty();
    }
    for (const Row& row : rows) {
      if (row.tag.directory != tag.directory ||
          (exact && row.tag.machine != tag.machine)) {
        continue;
      }
      for (const Page& page : row.pages) {
        plan.AddEntry({ImportEntryKind::kFavorite, plan.spaces[i].key,
                       std::string(), page.url, page.title, page.renamed});
      }
    }
  }
  return plan;
}

}  // namespace arcium
