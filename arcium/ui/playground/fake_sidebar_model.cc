// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/fake_sidebar_model.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"

namespace arcium {

namespace {

// A coloured rounded square stands in for a favicon.
class SwatchSource : public gfx::CanvasImageSource {
 public:
  explicit SwatchSource(SkColor color)
      : gfx::CanvasImageSource(gfx::Size(16, 16)), color_(color) {}
  void Draw(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    canvas->DrawRoundRect(gfx::RectF(0, 0, 16, 16), 4, flags);
  }

 private:
  SkColor color_;
};

ui::ImageModel SwatchFor(const std::string& url) {
  static constexpr SkColor kPalette[] = {
      SkColorSetRGB(0xE3, 0x4C, 0x4C), SkColorSetRGB(0x4C, 0x8B, 0xE3),
      SkColorSetRGB(0x3C, 0xB3, 0x71), SkColorSetRGB(0xF0, 0xA0, 0x30),
      SkColorSetRGB(0x58, 0x65, 0xF2), SkColorSetRGB(0x24, 0x29, 0x2E)};
  size_t hash = 0;
  for (char c : url) {
    hash = hash * 31 + static_cast<unsigned char>(c);
  }
  return ui::ImageModel::FromImageSkia(
      gfx::CanvasImageSource::MakeImageSkia<SwatchSource>(
          kPalette[hash % std::size(kPalette)]));
}

}  // namespace

FakeSidebarModel::FakeSidebarModel() = default;
FakeSidebarModel::~FakeSidebarModel() = default;

void FakeSidebarModel::AddTab(const std::u16string& title,
                              const std::string& url,
                              SidebarSection section,
                              bool active) {
  SidebarRow row;
  row.title = title;
  row.url = GURL(url);
  row.section = section;
  row.favicon = SwatchFor(url);
  // Everything outside Today is an entry in the real model, so the fake gives
  // those rows an id too.
  if (section != SidebarSection::kToday) {
    row.entry_id = EntryId::Generate();
  }
  if (active) {
    for (SidebarRow& r : rows_) {
      r.is_active = false;
    }
  }
  row.is_active = active;
  // Keep favourites first, then pinned, then today, like the real strip.
  auto pos = std::find_if(rows_.begin(), rows_.end(), [&](const SidebarRow& r) {
    return static_cast<int>(r.section) > static_cast<int>(section);
  });
  rows_.insert(pos, std::move(row));
  Reindex();
  Notify();
}

void FakeSidebarModel::AddColdEntry(const std::u16string& title,
                                    const std::string& url,
                                    SidebarSection section) {
  SidebarRow row;
  row.title = title;
  row.url = GURL(url);
  row.section = section;
  row.favicon = SwatchFor(url);
  row.entry_id = EntryId::Generate();
  row.is_cold = true;
  row.tab_index = -1;
  auto pos = std::find_if(rows_.begin(), rows_.end(), [&](const SidebarRow& r) {
    return static_cast<int>(r.section) > static_cast<int>(section);
  });
  rows_.insert(pos, std::move(row));
  Reindex();
  Notify();
}

void FakeSidebarModel::SetLoading(int tab_index, bool loading) {
  if (SidebarRow* row = FindByTabIndex(tab_index)) {
    row->is_loading = loading;
    Notify();
  }
}

void FakeSidebarModel::SetAudible(int tab_index, bool audible) {
  if (SidebarRow* row = FindByTabIndex(tab_index)) {
    row->is_audible = audible;
    Notify();
  }
}

void FakeSidebarModel::SetCanReturnToPinnedUrl(int tab_index, bool can_return) {
  if (SidebarRow* row = FindByTabIndex(tab_index)) {
    row->can_return_to_pinned_url = can_return;
    Notify();
  }
}

FolderId FakeSidebarModel::AddFolderWith(
    const std::u16string& name,
    const std::vector<std::u16string>& titles) {
  FolderId folder;
  for (const std::u16string& title : titles) {
    SidebarRow* row = FindByTitle(title);
    if (!row) {
      continue;
    }
    if (!folder.is_valid()) {
      folder = CreateFolderWithEntry(row->entry_id, name);
    } else {
      MoveEntryToFolder(row->entry_id, folder);
    }
  }
  return folder;
}

void FakeSidebarModel::SetFolderPosition(FolderId id, int position) {
  for (FakeFolder& folder : folders_) {
    if (folder.id == id) {
      folder.position = position;
      Notify();
      return;
    }
  }
}

std::vector<SidebarRow> FakeSidebarModel::rows() const {
  return rows_;
}

void FakeSidebarModel::ActivateTab(int tab_index) {
  for (SidebarRow& r : rows_) {
    r.is_active = !r.is_cold && r.tab_index == tab_index;
  }
  Notify();
}

void FakeSidebarModel::CloseTab(int tab_index) {
  SidebarRow* row = FindByTabIndex(tab_index);
  if (!row) {
    return;
  }
  const size_t pos = static_cast<size_t>(row - rows_.data());
  const bool was_active = row->is_active;
  rows_.erase(rows_.begin() + pos);
  Reindex();
  if (was_active && !rows_.empty()) {
    rows_[std::min(pos, rows_.size() - 1)].is_active = true;
  }
  Notify();
}

void FakeSidebarModel::MoveTab(int from_index, int to_index) {
  SidebarRow* from = FindByTabIndex(from_index);
  SidebarRow* to = FindByTabIndex(to_index);
  if (!from || !to) {
    return;
  }
  const size_t from_pos = static_cast<size_t>(from - rows_.data());
  const size_t to_pos = static_cast<size_t>(to - rows_.data());
  SidebarRow row = std::move(rows_[from_pos]);
  rows_.erase(rows_.begin() + from_pos);
  rows_.insert(rows_.begin() + to_pos, std::move(row));
  Reindex();
  Notify();
}

void FakeSidebarModel::NewTab() {
  AddTab(u"New Tab", "about:blank", SidebarSection::kToday, /*active=*/true);
}

void FakeSidebarModel::ClearToday() {
  std::erase_if(rows_, [](const SidebarRow& r) {
    return r.section == SidebarSection::kToday;
  });
  Reindex();
  Notify();
}

void FakeSidebarModel::AddToFavorites(int tab_index) {
  MakeEntry(tab_index, SidebarSection::kFavorites);
}

void FakeSidebarModel::PinTab(int tab_index) {
  MakeEntry(tab_index, SidebarSection::kPinned);
}

void FakeSidebarModel::UnpinEntry(EntryId id) {
  SidebarRow* row = FindByEntry(id);
  if (!row) {
    return;
  }
  if (row->is_cold) {
    // Nothing lives behind it, so the entry is all there was.
    std::erase_if(rows_,
                  [id](const SidebarRow& r) { return r.entry_id == id; });
  } else {
    // The tab falls back into Today because nothing claims it any more.
    SidebarRow moved = *row;
    moved.entry_id = EntryId();
    moved.section = SidebarSection::kToday;
    moved.can_return_to_pinned_url = false;
    // Today has no folders, and the entry that was in one is gone.
    moved.folder_id.reset();
    std::erase_if(rows_,
                  [id](const SidebarRow& r) { return r.entry_id == id; });
    rows_.push_back(std::move(moved));
  }
  Reindex();
  Notify();
}

void FakeSidebarModel::ActivateEntry(EntryId id) {
  SidebarRow* row = FindByEntry(id);
  if (!row) {
    return;
  }
  // A cold entry warms up: the click opened its URL.
  row->is_cold = false;
  for (SidebarRow& r : rows_) {
    r.is_active = r.entry_id == id;
  }
  Reindex();
  Notify();
}

void FakeSidebarModel::CloseEntryTab(EntryId id) {
  SidebarRow* row = FindByEntry(id);
  if (!row) {
    return;
  }
  // The entry stays; only its tab goes.
  row->is_cold = true;
  row->is_active = false;
  row->is_loading = false;
  row->can_return_to_pinned_url = false;
  Reindex();
  Notify();
}

void FakeSidebarModel::SetEntryTitle(EntryId id, const std::u16string& title) {
  if (SidebarRow* row = FindByEntry(id)) {
    row->title = title;
    Notify();
  }
}

void FakeSidebarModel::ReturnToPinnedUrl(EntryId id) {
  if (SidebarRow* row = FindByEntry(id)) {
    row->can_return_to_pinned_url = false;
    Notify();
  }
}

std::vector<SidebarFolder> FakeSidebarModel::folders() const {
  // Sorted by position, not by the vector's order, because that is what
  // SidebarTabModel does and the views are entitled to rely on it.
  std::vector<const FakeFolder*> ordered;
  for (const FakeFolder& folder : folders_) {
    ordered.push_back(&folder);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const FakeFolder* a, const FakeFolder* b) {
              return a->position < b->position;
            });
  std::vector<SidebarFolder> result;
  for (const FakeFolder* folder : ordered) {
    SidebarFolder out;
    out.id = folder->id;
    out.name = folder->name;
    out.collapsed = folder->collapsed;
    for (const SidebarRow& row : rows_) {
      if (row.folder_id == folder->id) {
        ++out.entry_count;
      }
    }
    result.push_back(std::move(out));
  }
  return result;
}

void FakeSidebarModel::SetFolderCollapsed(FolderId id, bool collapsed) {
  for (FakeFolder& folder : folders_) {
    if (folder.id == id) {
      // ArciumModel early-returns on an unchanged value, so a caller that
      // expects a notification out of a no-op change would be relying on
      // behaviour the real model does not have.
      if (folder.collapsed == collapsed) {
        return;
      }
      folder.collapsed = collapsed;
      Notify();
      return;
    }
  }
}

FolderId FakeSidebarModel::CreateFolderWithEntry(EntryId id,
                                                 const std::u16string& name) {
  SidebarRow* row = FindByEntry(id);
  // Folders hold pinned entries only, exactly as the browser model has it.
  if (!row || row->section != SidebarSection::kPinned) {
    return FolderId();
  }
  FakeFolder folder;
  folder.id = FolderId::Generate();
  folder.name = name;
  // The same rule ArciumModel::AddFolder uses: the next free position.
  folder.position = static_cast<int>(folders_.size());
  folders_.push_back(folder);
  row->folder_id = folder.id;
  Notify();
  return folder.id;
}

void FakeSidebarModel::MoveEntryToFolder(EntryId id,
                                         std::optional<FolderId> folder_id) {
  SidebarRow* row = FindByEntry(id);
  if (!row || row->section != SidebarSection::kPinned ||
      (folder_id.has_value() && !HasFolder(*folder_id))) {
    return;
  }
  row->folder_id = folder_id;
  Notify();
}

void FakeSidebarModel::SetFolderName(FolderId id, const std::u16string& name) {
  for (FakeFolder& folder : folders_) {
    if (folder.id == id) {
      folder.name = name;
      Notify();
      return;
    }
  }
}

void FakeSidebarModel::DeleteFolder(FolderId id) {
  const size_t before = folders_.size();
  std::erase_if(folders_, [id](const FakeFolder& f) { return f.id == id; });
  if (folders_.size() == before) {
    return;
  }
  // The entries come back to the top level; the folder was a grouping.
  for (SidebarRow& row : rows_) {
    if (row.folder_id == id) {
      row.folder_id.reset();
    }
  }
  NormaliseFolderPositions();
  Notify();
}

void FakeSidebarModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FakeSidebarModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FakeSidebarModel::Notify() {
  for (Observer& o : observers_) {
    o.OnSidebarModelChanged();
  }
}

void FakeSidebarModel::NormaliseFolderPositions() {
  // Mirrors ArciumModel::NormalisePositions: a single sequence, sorted by the
  // position folders already have, renumbered to 0..n-1.
  std::vector<FakeFolder*> ordered;
  ordered.reserve(folders_.size());
  for (FakeFolder& folder : folders_) {
    ordered.push_back(&folder);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const FakeFolder* a, const FakeFolder* b) {
              return a->position < b->position;
            });
  for (size_t i = 0; i < ordered.size(); ++i) {
    ordered[i]->position = static_cast<int>(i);
  }
}

void FakeSidebarModel::Reindex() {
  // Cold rows have no tab, so they keep -1 and take no index from the ones
  // that do.
  int next = 0;
  for (SidebarRow& row : rows_) {
    row.tab_index = row.is_cold ? -1 : next++;
  }
}

SidebarRow* FakeSidebarModel::FindByTabIndex(int tab_index) {
  for (SidebarRow& row : rows_) {
    if (!row.is_cold && row.tab_index == tab_index) {
      return &row;
    }
  }
  return nullptr;
}

SidebarRow* FakeSidebarModel::FindByEntry(EntryId id) {
  for (SidebarRow& row : rows_) {
    if (row.entry_id == id) {
      return &row;
    }
  }
  return nullptr;
}

SidebarRow* FakeSidebarModel::FindByTitle(const std::u16string& title) {
  for (SidebarRow& row : rows_) {
    if (row.title == title) {
      return &row;
    }
  }
  return nullptr;
}

bool FakeSidebarModel::HasFolder(FolderId id) const {
  for (const FakeFolder& folder : folders_) {
    if (folder.id == id) {
      return true;
    }
  }
  return false;
}

void FakeSidebarModel::MakeEntry(int tab_index, SidebarSection section) {
  SidebarRow* found = FindByTabIndex(tab_index);
  if (!found) {
    return;
  }
  SidebarRow row = *found;
  row.section = section;
  row.entry_id = EntryId::Generate();
  std::erase_if(rows_, [tab_index](const SidebarRow& r) {
    return !r.is_cold && r.tab_index == tab_index;
  });
  auto pos = std::find_if(rows_.begin(), rows_.end(), [&](const SidebarRow& r) {
    return static_cast<int>(r.section) > static_cast<int>(section);
  });
  rows_.insert(pos, std::move(row));
  Reindex();
  Notify();
}

}  // namespace arcium
