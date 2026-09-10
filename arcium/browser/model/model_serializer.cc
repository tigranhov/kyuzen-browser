// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_serializer.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"

namespace arcium {

namespace {

// Strings rather than ints on the wire: a JSON file a human may read during a
// bug report should say "pinned", not "1".
std::string KindToString(EntryKind kind) {
  return kind == EntryKind::kFavorite ? "favorite" : "pinned";
}

EntryKind KindFromString(const std::string* value) {
  return (value && *value == "favorite") ? EntryKind::kFavorite
                                         : EntryKind::kPinned;
}

std::string TimeoutToString(ArchiveTimeout timeout) {
  switch (timeout) {
    case ArchiveTimeout::kTwelveHours:
      return "12h";
    case ArchiveTimeout::kOneDay:
      return "24h";
    case ArchiveTimeout::kSevenDays:
      return "7d";
    case ArchiveTimeout::kNever:
      return "never";
  }
}

ArchiveTimeout TimeoutFromString(const std::string* value) {
  if (!value) {
    return ArchiveTimeout::kTwelveHours;
  }
  if (*value == "24h") {
    return ArchiveTimeout::kOneDay;
  }
  if (*value == "7d") {
    return ArchiveTimeout::kSevenDays;
  }
  if (*value == "never") {
    return ArchiveTimeout::kNever;
  }
  return ArchiveTimeout::kTwelveHours;
}

}  // namespace

base::DictValue SerializeModel(const ArciumModel& model) {
  base::DictValue dict;
  dict.Set("version", kModelSchemaVersion);

  base::ListValue spaces;
  for (const Space& space : model.spaces()) {
    base::DictValue value;
    value.Set("id", space.id.value());
    value.Set("name", base::UTF16ToUTF8(space.name));
    value.Set("archive_timeout", TimeoutToString(space.archive_timeout));
    value.Set("position", space.position);
    value.Set("icon", base::UTF16ToUTF8(space.icon));
    value.Set("gradient", space.gradient);
    if (space.last_active_tab.is_valid()) {
      value.Set("last_active_tab", space.last_active_tab.value());
    }
    spaces.Append(std::move(value));
  }
  dict.Set("spaces", std::move(spaces));

  base::ListValue folders;
  for (const Folder& folder : model.folders()) {
    base::DictValue value;
    value.Set("id", folder.id.value());
    value.Set("space_id", folder.space_id.value());
    // Only when there is one: an absent key is what the top level means, and
    // it is also what every version 1 file says about every folder it has.
    if (folder.parent_id) {
      value.Set("parent_id", folder.parent_id->value());
    }
    value.Set("name", base::UTF16ToUTF8(folder.name));
    value.Set("collapsed", folder.collapsed);
    value.Set("position", folder.position);
    folders.Append(std::move(value));
  }
  dict.Set("folders", std::move(folders));

  base::ListValue entries;
  for (const TabEntry& entry : model.entries()) {
    base::DictValue value;
    value.Set("id", entry.id.value());
    value.Set("kind", KindToString(entry.kind));
    value.Set("space_id", entry.space_id.value());
    if (entry.folder_id) {
      value.Set("folder_id", entry.folder_id->value());
    }
    value.Set("position", entry.position);
    value.Set("url", entry.url.spec());
    value.Set("custom_title", base::UTF16ToUTF8(entry.custom_title));
    value.Set("last_title", base::UTF16ToUTF8(entry.last_title));
    // A decimal string, not a number: a double cannot hold a microsecond
    // timestamp exactly above 2^53, and today's timestamps already are.
    value.Set(
        "created_at",
        base::NumberToString(
            entry.created_at.ToDeltaSinceWindowsEpoch().InMicroseconds()));
    entries.Append(std::move(value));
  }
  dict.Set("entries", std::move(entries));

  dict.Set("last_active_space", model.last_active_space().value());

  return dict;
}

bool DeserializeModel(const base::DictValue& dict, ArciumModel* model) {
  const std::optional<int> version = dict.FindInt("version");
  if (!version || *version > kModelSchemaVersion) {
    return false;
  }

  std::vector<Space> spaces;
  std::set<SpaceId> space_ids;
  if (const base::ListValue* list = dict.FindList("spaces")) {
    for (const base::Value& item : *list) {
      const base::DictValue* value = item.GetIfDict();
      if (!value) {
        continue;
      }
      const std::string* id = value->FindString("id");
      Space space;
      space.id = id ? SpaceId::FromString(*id) : SpaceId();
      if (!space.id.is_valid()) {
        continue;
      }
      const std::string* name = value->FindString("name");
      space.name = name ? base::UTF8ToUTF16(*name) : u"Space";
      space.archive_timeout =
          TimeoutFromString(value->FindString("archive_timeout"));
      space.position = value->FindInt("position").value_or(0);
      const std::string* icon = value->FindString("icon");
      space.icon = icon ? base::UTF8ToUTF16(*icon) : std::u16string();
      space.gradient = value->FindInt("gradient").value_or(0);
      const std::string* last_tab = value->FindString("last_active_tab");
      space.last_active_tab =
          last_tab ? TabKey::FromString(*last_tab) : TabKey();
      space_ids.insert(space.id);
      spaces.push_back(std::move(space));
    }
  }
  // Every space failing to parse is row-level damage, not structural damage:
  // the entries and folders in the rest of the file are still worth keeping,
  // so synthesise the same default space a fresh model would start with
  // rather than refusing the whole file. This has to happen here, before
  // entries and folders are parsed, because they need a valid space id to
  // fall back to; ArciumModel::ReplaceAll's own empty-spaces fallback runs
  // too late for that.
  if (spaces.empty()) {
    Space space;
    space.id = SpaceId::Generate();
    space.name = u"Space";
    space.archive_timeout = ArchiveTimeout::kTwelveHours;
    space.position = 0;
    space_ids.insert(space.id);
    spaces.push_back(std::move(space));
  }
  const SpaceId default_space = spaces.front().id;

  std::vector<Folder> folders;
  std::set<FolderId> folder_ids;
  if (const base::ListValue* list = dict.FindList("folders")) {
    for (const base::Value& item : *list) {
      const base::DictValue* value = item.GetIfDict();
      if (!value) {
        continue;
      }
      const std::string* id = value->FindString("id");
      Folder folder;
      folder.id = id ? FolderId::FromString(*id) : FolderId();
      if (!folder.id.is_valid()) {
        continue;
      }
      const std::string* space_id = value->FindString("space_id");
      folder.space_id = space_id ? SpaceId::FromString(*space_id) : SpaceId();
      if (!space_ids.contains(folder.space_id)) {
        folder.space_id = default_space;
      }
      const std::string* name = value->FindString("name");
      folder.name = name ? base::UTF8ToUTF16(*name) : std::u16string();
      folder.collapsed = value->FindBool("collapsed").value_or(false);
      folder.position = value->FindInt("position").value_or(0);
      // Parsed now, checked below: a folder's parent may appear later in the
      // file, so the link cannot be validated until every folder is known.
      if (const std::string* parent_id = value->FindString("parent_id")) {
        const FolderId parsed = FolderId::FromString(*parent_id);
        if (parsed.is_valid()) {
          folder.parent_id = parsed;
        }
      }
      folder_ids.insert(folder.id);
      folders.push_back(std::move(folder));
    }
  }

  // Three things can be wrong with the parent links in a file: a parent that
  // is not here, a parent in another space, and a chain that is either a
  // cycle or deeper than the sidebar can draw. All are repaired by detaching
  // the folder to the top level, which is the one repair that cannot itself
  // invent a new problem -- a root has no chain to be wrong about. None of
  // them refuses the file: the folders and entries in it are still the
  // user's, and a tree drawn flat is recoverable while a tree thrown away is
  // not.
  std::map<FolderId, size_t> folder_index;
  for (size_t i = 0; i < folders.size(); ++i) {
    folder_index[folders[i].id] = i;
  }
  for (Folder& folder : folders) {
    if (!folder.parent_id) {
      continue;
    }
    const auto it = folder_index.find(*folder.parent_id);
    if (it == folder_index.end() ||
        folders[it->second].space_id != folder.space_id) {
      folder.parent_id.reset();
    }
  }
  for (Folder& folder : folders) {
    std::set<FolderId> seen = {folder.id};
    int depth = 0;
    std::optional<FolderId> parent = folder.parent_id;
    while (parent.has_value()) {
      if (!seen.insert(*parent).second) {
        // Walked back onto something already on this chain.
        folder.parent_id.reset();
        depth = 0;
        break;
      }
      ++depth;
      const auto found = folder_index.find(*parent);
      if (found == folder_index.end()) {
        // Unreachable while the pass above runs, because it clears exactly
        // these. find() rather than operator[] all the same: operator[] on a
        // missing key inserts a default-constructed 0, so dropping that pass
        // would not fail here -- it would silently read folders[0] and give
        // this folder whichever parent happened to sit there. A mutation
        // probe caught precisely that masking.
        folder.parent_id.reset();
        depth = 0;
        break;
      }
      parent = folders[found->second].parent_id;
    }
    if (depth > kMaxFolderDepth - 1) {
      folder.parent_id.reset();
    }
  }

  std::vector<TabEntry> entries;
  if (const base::ListValue* list = dict.FindList("entries")) {
    for (const base::Value& item : *list) {
      const base::DictValue* value = item.GetIfDict();
      if (!value) {
        continue;
      }
      const std::string* id = value->FindString("id");
      TabEntry entry;
      entry.id = id ? EntryId::FromString(*id) : EntryId();
      if (!entry.id.is_valid()) {
        continue;
      }
      const std::string* url = value->FindString("url");
      entry.url = url ? GURL(*url) : GURL();
      if (!entry.url.is_valid()) {
        continue;
      }
      entry.kind = KindFromString(value->FindString("kind"));
      const std::string* space_id = value->FindString("space_id");
      entry.space_id = space_id ? SpaceId::FromString(*space_id) : SpaceId();
      if (!space_ids.contains(entry.space_id)) {
        entry.space_id = default_space;
      }
      // An entry pointing at a folder that is gone belongs at the top level,
      // not nowhere.
      if (const std::string* folder_id = value->FindString("folder_id")) {
        const FolderId parsed = FolderId::FromString(*folder_id);
        if (folder_ids.contains(parsed)) {
          entry.folder_id = parsed;
        }
      }
      entry.position = value->FindInt("position").value_or(0);
      if (const std::string* title = value->FindString("custom_title")) {
        entry.custom_title = base::UTF8ToUTF16(*title);
      }
      if (const std::string* title = value->FindString("last_title")) {
        entry.last_title = base::UTF8ToUTF16(*title);
      }
      int64_t created_micros = 0;
      if (const std::string* created = value->FindString("created_at")) {
        base::StringToInt64(*created, &created_micros);
      }
      entry.created_at = base::Time::FromDeltaSinceWindowsEpoch(
          base::Microseconds(created_micros));
      entries.push_back(std::move(entry));
    }
  }

  model->ReplaceAll(std::move(spaces), std::move(folders), std::move(entries));

  // After ReplaceAll, so the id is checked against the spaces that survived
  // parsing; last_active_space() falls back to the first space on its own
  // when this names one that did not.
  if (const std::string* active = dict.FindString("last_active_space")) {
    model->SetLastActiveSpace(SpaceId::FromString(*active));
  }

  return true;
}

}  // namespace arcium
