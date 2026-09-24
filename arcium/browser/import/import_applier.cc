// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/import_applier.h"

#include <algorithm>
#include <map>
#include <optional>

#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/tab_entry.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversions.h"

namespace arcium {
namespace {

const Space* SpaceNamed(const ArciumModel& model, const std::u16string& name) {
  for (const Space& space : model.spaces()) {
    if (space.name == name) {
      return &space;
    }
  }
  return nullptr;
}

std::optional<FolderId> FolderNamed(const ArciumModel& model,
                                    SpaceId space,
                                    std::optional<FolderId> parent,
                                    const std::u16string& name) {
  for (const Folder& folder : model.folders()) {
    if (folder.space_id == space && folder.parent_id == parent &&
        folder.name == name) {
      return folder.id;
    }
  }
  return std::nullopt;
}

bool HasEntry(const ArciumModel& model,
              SpaceId space,
              EntryKind kind,
              const GURL& url) {
  return std::ranges::any_of(model.entries(), [&](const TabEntry& entry) {
    return entry.space_id == space && entry.kind == kind && entry.url == url;
  });
}

// The logins a new space uses: the shared ones, or when it keeps them
// separate, the profile of that name, made if the model has none yet.
class Logins {
 public:
  Logins(const ImportPlan& plan,
         const ImportChoices& choices,
         ArciumModel& model,
         ImportResult& result)
      : plan_(plan), choices_(choices), model_(model), result_(result) {}
  Logins(const Logins&) = delete;
  Logins& operator=(const Logins&) = delete;

  ProfileId For(const ImportSpace& space) {
    if (!choices_->separate_logins.contains(space.key)) {
      return DefaultProfileId();
    }
    // A space that shared the logins in the source and is given its own here
    // gets a profile of its own, named after it. Spaces that shared one in
    // the source find it again by name, as a second import does.
    std::string name = space.name;
    for (const ImportProfile& profile : plan_->profiles) {
      if (profile.key == space.profile_key) {
        name = profile.name;
      }
    }
    const std::u16string title = base::UTF8ToUTF16(name);
    for (const ArciumProfile& existing : model_->profiles()) {
      if (existing.name == title) {
        return existing.id;
      }
    }
    // Default holds preset 0; each profile after it takes the next preset,
    // round the palette.
    const int colors = std::max(choices_->profile_colors, 1);
    const int color = static_cast<int>(model_->profiles().size()) % colors;
    ++result_->profiles;
    return model_->AddProfile(title, color);
  }

 private:
  const raw_ref<const ImportPlan> plan_;
  const raw_ref<const ImportChoices> choices_;
  const raw_ref<ArciumModel> model_;
  const raw_ref<ImportResult> result_;
};

}  // namespace

ImportChoices::ImportChoices() = default;
ImportChoices::ImportChoices(const ImportChoices&) = default;
ImportChoices& ImportChoices::operator=(const ImportChoices&) = default;
ImportChoices::~ImportChoices() = default;

ImportChoices DefaultChoices(const ImportPlan& plan) {
  ImportChoices choices;
  for (const ImportSpace& space : plan.spaces) {
    if (!space.profile_key.empty()) {
      choices.separate_logins.insert(space.key);
    }
  }
  return choices;
}

ImportResult ApplyImportPlan(const ImportPlan& plan,
                             const ImportChoices& choices,
                             ArciumModel& model) {
  ImportResult result;
  // A fresh model's one space, still holding nothing.
  const bool fresh = model.spaces().size() == 1 && model.entries().empty() &&
                     model.folders().empty();
  const SpaceId starter = fresh ? model.spaces()[0].id : SpaceId();

  std::map<std::string, SpaceId> spaces;
  if (choices.spaces) {
    Logins logins(plan, choices, model, result);
    for (const ImportSpace& space : plan.spaces) {
      const std::u16string name = base::UTF8ToUTF16(space.name);
      if (const Space* existing = SpaceNamed(model, name)) {
        spaces[space.key] = existing->id;
        continue;
      }
      const SpaceId id = model.AddSpace(name, logins.For(space));
      if (!space.icon.empty()) {
        model.SetSpaceIcon(id, base::UTF8ToUTF16(space.icon));
      }
      spaces[space.key] = id;
      ++result.spaces;
    }
    if (!plan.spaces.empty()) {
      result.first_space = spaces[plan.spaces[0].key];
    }
  }
  const auto space_of = [&](const std::string& key) {
    const auto it = spaces.find(key);
    return it != spaces.end() ? it->second : choices.into;
  };

  // Folders, parents first as the plan lists them.
  std::map<std::string, std::optional<FolderId>> folders;
  if (choices.pinned && choices.folders) {
    for (const ImportFolder& folder : plan.folders) {
      const SpaceId space = space_of(folder.space_key);
      if (!model.GetSpace(space)) {
        continue;
      }
      const auto parent_it = folders.find(folder.parent_key);
      const std::optional<FolderId> parent =
          parent_it != folders.end() ? parent_it->second : std::nullopt;
      // Past the deepest level Kyuzen draws, a folder's contents join the
      // folder at that level.
      if (parent && model.FolderDepth(*parent) >= kMaxFolderDepth - 1) {
        folders[folder.key] = parent;
        continue;
      }
      const std::u16string name = base::UTF8ToUTF16(folder.name);
      std::optional<FolderId> id = FolderNamed(model, space, parent, name);
      if (!id) {
        id = model.AddFolder(space, name, parent);
        if (folder.collapsed) {
          model.SetFolderCollapsed(*id, true);
        }
        ++result.folders;
      }
      folders[folder.key] = id;
    }
  }

  for (const ImportEntry& entry : plan.entries) {
    const bool pinned = entry.kind == ImportEntryKind::kPinned;
    if (pinned ? !choices.pinned : !choices.favorites) {
      continue;
    }
    const SpaceId space = space_of(entry.space_key);
    const EntryKind kind = pinned ? EntryKind::kPinned : EntryKind::kFavorite;
    if (!model.GetSpace(space) || HasEntry(model, space, kind, entry.url)) {
      continue;
    }
    const std::u16string title = base::UTF8ToUTF16(entry.title);
    const EntryId id = model.AddEntry(space, kind, entry.url, title);
    // A name its owner gave it stays when the page renames itself.
    if (entry.renamed) {
      model.SetCustomTitle(id, title);
    }
    const auto folder = folders.find(entry.folder_key);
    if (pinned && folder != folders.end() && folder->second) {
      model.SetEntryFolder(id, folder->second);
    }
    ++(pinned ? result.pinned : result.favorites);
  }

  if (fresh && result.spaces > 0 && model.GetSpace(starter)) {
    const bool still_empty =
        std::ranges::none_of(
            model.entries(),
            [&](const TabEntry& e) { return e.space_id == starter; }) &&
        std::ranges::none_of(model.folders(), [&](const Folder& f) {
          return f.space_id == starter;
        });
    if (still_empty && result.first_space != starter) {
      result.empty_starter = starter;
    }
  }
  return result;
}

}  // namespace arcium
