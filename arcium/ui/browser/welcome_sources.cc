// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/welcome_sources.h"

#include <algorithm>
#include <utility>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/profile_defaults.h"
#include "base/strings/utf_string_conversions.h"

namespace arcium {
namespace {

// As many rows as the last step's picture has room for.
constexpr size_t kArrivedRows = 4;

}  // namespace

std::optional<WelcomeStep> WelcomeResumeStep(int stored,
                                             bool model_file_absent) {
  if (stored == kWelcomeDone) {
    return std::nullopt;
  }
  const int first = static_cast<int>(WelcomeStep::kSetup) + 1;
  const int last = static_cast<int>(WelcomeStep::kDone) + 1;
  if (stored >= first && stored <= last) {
    return static_cast<WelcomeStep>(stored - 1);
  }
  // Never started, or a number no build wrote: the welcome is for a fresh
  // install only.
  if (model_file_absent) {
    return WelcomeStep::kSetup;
  }
  return std::nullopt;
}

std::u16string SourceName(ImportSourceKind kind) {
  switch (kind) {
    case ImportSourceKind::kZen:
      return u"Zen";
    case ImportSourceKind::kArc:
      return u"Arc";
  }
}

std::vector<std::vector<FoundSource>> GroupByBrowser(
    std::vector<FoundSource> found) {
  std::vector<std::vector<FoundSource>> groups;
  for (ImportSourceKind kind :
       {ImportSourceKind::kZen, ImportSourceKind::kArc}) {
    std::vector<FoundSource> group;
    for (FoundSource& source : found) {
      if (source.kind == kind) {
        group.push_back(std::move(source));
      }
    }
    if (!group.empty()) {
      groups.push_back(std::move(group));
    }
  }
  return groups;
}

WelcomeSource DescribeSource(const std::vector<FoundSource>& group,
                             size_t profile,
                             const std::u16string& detail) {
  WelcomeSource source;
  if (group.empty()) {
    return source;
  }
  profile = std::min(profile, group.size() - 1);
  source.kind = group[profile].kind == ImportSourceKind::kZen
                    ? WelcomeSource::Kind::kZen
                    : WelcomeSource::Kind::kArc;
  source.name = SourceName(group[profile].kind);
  source.detail = detail;
  if (group.size() > 1) {
    for (const FoundSource& found : group) {
      source.profiles.push_back(base::UTF8ToUTF16(found.profile_name));
    }
  }
  source.profile = profile;
  const ImportPlan::Counts counts = group[profile].plan.counts();
  source.spaces = counts.spaces;
  source.pinned = counts.pinned;
  source.favorites = counts.favorites;
  source.folders = counts.folders;
  return source;
}

std::vector<WelcomeSpace> SpacesToImport(const ImportPlan& plan,
                                         const ImportChoices& choices,
                                         const std::u16string& origin) {
  std::vector<WelcomeSpace> spaces;
  for (const ImportSpace& from : plan.spaces) {
    WelcomeSpace space;
    space.icon = base::UTF8ToUTF16(from.icon);
    space.name = base::UTF8ToUTF16(from.name);
    space.pinned = plan.PinnedCountIn(from.key);
    space.origin = origin;
    space.separate_logins = choices.separate_logins.contains(from.key);
    space.had_separate_logins = !from.profile_key.empty();
    spaces.push_back(std::move(space));
  }
  return spaces;
}

ImportPlan StartFreshPlan() {
  ImportPlan plan;
  for (const char* name : {"Personal", "Work"}) {
    ImportSpace space;
    space.key = name;
    space.name = name;
    plan.spaces.push_back(std::move(space));
  }
  return plan;
}

WelcomeSpace SpaceAsArrived(const ArciumModel& model,
                            SpaceId id,
                            const std::u16string& origin) {
  WelcomeSpace arrived;
  const Space* space = model.GetSpace(id);
  if (!space) {
    return arrived;
  }
  arrived.icon = space->icon;
  arrived.name = space->name;
  arrived.origin = origin;
  const std::vector<const TabEntry*> pinned =
      model.EntriesForKind(id, EntryKind::kPinned);
  arrived.pinned = pinned.size();

  // The sidebar draws a space's folders first, then its loose pinned pages.
  std::vector<const Folder*> folders;
  for (const Folder& folder : model.folders()) {
    if (folder.space_id == id && !folder.parent_id) {
      folders.push_back(&folder);
    }
  }
  std::ranges::sort(folders, {}, &Folder::position);
  for (const Folder* folder : folders) {
    if (arrived.rows.size() == kArrivedRows) {
      return arrived;
    }
    arrived.rows.push_back({folder->name, /*folder=*/true});
  }
  for (const TabEntry* entry : pinned) {
    if (arrived.rows.size() == kArrivedRows) {
      break;
    }
    if (entry->folder_id) {
      continue;
    }
    const std::u16string& title = entry->DisplayTitle();
    arrived.rows.push_back(
        {title.empty() ? base::UTF8ToUTF16(entry->url.host()) : title,
         /*folder=*/false});
  }
  return arrived;
}

}  // namespace arcium
