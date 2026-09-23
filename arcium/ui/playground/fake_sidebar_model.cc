// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/fake_sidebar_model.h"

#include "arcium/ui/sidebar/split_rows.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <utility>

#include "arcium/browser/model/folder.h"
#include "arcium/ui/sidebar/folder_tree.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
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

FakeSidebarModel::FakeSidebarModel() {
  // One space, marked active, exactly what every test and playground scene
  // written before spaces existed already assumes.
  SidebarSpace first;
  first.id = SpaceId::Generate();
  first.is_active = true;
  spaces_.push_back(std::move(first));
}
FakeSidebarModel::~FakeSidebarModel() = default;

void FakeSidebarModel::AddTab(const std::u16string& title,
                              const std::string& url,
                              SidebarSection section,
                              bool active) {
  // The first space, not necessarily the active one: every test and
  // playground scene that seeds rows this way was written before spaces and
  // means the first space by it. NewTab, which opens in the space on screen,
  // names that space instead.
  InsertTab(title, url, section, active, spaces_.front().id);
}

void FakeSidebarModel::InsertTab(const std::u16string& title,
                                 const std::string& url,
                                 SidebarSection section,
                                 bool active,
                                 SpaceId space) {
  SidebarRow row;
  row.title = title;
  row.url = GURL(url);
  row.section = section;
  row.favicon = SwatchFor(url);
  row.space = space;
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
  row.space = spaces_.front().id;
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

void FakeSidebarModel::SetUnloaded(int tab_index, bool unloaded) {
  if (SidebarRow* row = FindByTabIndex(tab_index)) {
    row->is_unloaded = unloaded && !row->is_active;
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

bool FakeSidebarModel::CanSplitRow(const SidebarRow& row) const {
  // The playground has no tab strip, so it stands in for the real rule with
  // the two halves it can answer: the row on screen cannot share with itself,
  // and a row already sharing has to be taken apart first. The real model
  // also refuses another space's row, which this fake has no page on screen
  // to compare against.
  return !row.split.has_value() && !row.is_active;
}

void FakeSidebarModel::SplitRowWithCurrentPage(const SidebarRow& row) {
  // The active row stands in for the page on screen, which the playground
  // does not have. Rows are matched by URL: the fake's rows are copies, so
  // the one handed over is never the one held.
  std::optional<size_t> active;
  std::optional<size_t> chosen;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].url == row.url) {
      chosen = i;
    } else if (rows_[i].is_active) {
      active = i;
    }
  }
  if (active && chosen) {
    SplitRows(*active, *chosen);
  }
}

void FakeSidebarModel::ToggleSplit() {
  for (SidebarRow& r : rows_) {
    r.split.reset();
  }
  Notify();
}

void FakeSidebarModel::EndSplit(const SidebarRow& row) {
  for (SidebarRow& r : rows_) {
    if (row.split.has_value() && r.split == row.split) {
      r.split.reset();
    }
  }
  Notify();
}

void FakeSidebarModel::CloseSplit(const SidebarRow& row) {
  if (!row.split.has_value()) {
    return;
  }
  // Copies, because each close rebuilds the list the loop would be reading.
  std::vector<SidebarRow> halves;
  for (const SidebarRow& r : rows_) {
    if (r.split == row.split) {
      halves.push_back(r);
    }
  }
  for (const SidebarRow& half : halves) {
    if (half.entry_id.is_valid()) {
      CloseEntryTab(half.entry_id);
    } else {
      CloseTab(half.tab_index);
    }
  }
}

std::optional<size_t> FakeSidebarModel::IndexOfDragged(EntryId entry,
                                                       int tab_index) const {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (entry.is_valid()
            ? rows_[i].entry_id == entry
            : !rows_[i].is_cold && rows_[i].tab_index == tab_index) {
      return i;
    }
  }
  return std::nullopt;
}

bool FakeSidebarModel::CanSplitByDrop(const SidebarRow& target,
                                      EntryId dragged_entry,
                                      int dragged_tab) const {
  // The fake has no tab strip or spaces to ask, so it keeps the rules it can
  // answer: not the same row, and nothing already sharing.
  if (target.split.has_value() || target.split_joins_previous ||
      target.split_joins_next) {
    return false;
  }
  const std::optional<size_t> dragged =
      IndexOfDragged(dragged_entry, dragged_tab);
  const std::optional<size_t> onto =
      IndexOfDragged(target.entry_id, target.tab_index);
  return dragged && onto && *dragged != *onto &&
         !rows_[*dragged].split.has_value();
}

void FakeSidebarModel::SplitByDrop(const SidebarRow& target,
                                   EntryId dragged_entry,
                                   int dragged_tab) {
  if (!CanSplitByDrop(target, dragged_entry, dragged_tab)) {
    return;
  }
  const std::optional<size_t> dragged =
      IndexOfDragged(dragged_entry, dragged_tab);
  const std::optional<size_t> onto =
      IndexOfDragged(target.entry_id, target.tab_index);
  // The two halves become the page on screen, and nothing else is.
  for (SidebarRow& r : rows_) {
    r.is_active = false;
  }
  rows_[*onto].is_cold = false;
  SplitRows(*onto, *dragged);
}

void FakeSidebarModel::LeaveSplit(EntryId entry, int tab_index) {
  const std::optional<size_t> index = IndexOfDragged(entry, tab_index);
  if (!index || !rows_[*index].split.has_value()) {
    return;
  }
  const split_tabs::SplitTabId split = *rows_[*index].split;
  for (SidebarRow& r : rows_) {
    if (r.split == split) {
      r.split.reset();
    }
  }
  Notify();
}

void FakeSidebarModel::MoveSplit(int tab_index, int before_tab) {
  SidebarRow* named = FindByTabIndex(tab_index);
  if (!named || !named->split.has_value()) {
    return;
  }
  const split_tabs::SplitTabId split = *named->split;
  // Lifted out in their order, then put back together before the row that
  // holds `before_tab`, or at the end.
  std::vector<SidebarRow> pair;
  std::vector<SidebarRow> rest;
  for (SidebarRow& r : rows_) {
    (r.split == split ? pair : rest).push_back(std::move(r));
  }
  auto at = std::find_if(rest.begin(), rest.end(), [&](const SidebarRow& r) {
    return !r.is_cold && r.tab_index == before_tab;
  });
  rest.insert(at, std::make_move_iterator(pair.begin()),
              std::make_move_iterator(pair.end()));
  rows_ = std::move(rest);
  Reindex();
  Notify();
}

void FakeSidebarModel::SplitRows(size_t first, size_t second) {
  if (first >= rows_.size() || second >= rows_.size() || first == second) {
    return;
  }
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  rows_[first].split = id;
  rows_[second].split = id;
  // Both halves of a split are on screen, so both rows are current.
  rows_[first].is_active = true;
  rows_[second].is_active = true;
  Notify();
}

std::vector<SidebarRow> FakeSidebarModel::rows() const {
  // Only the space on screen: the real model's rows() is scoped the same
  // way, filtering at the tab strip before a row is ever built.
  const SpaceId active = ActiveSpaceId();
  std::vector<SidebarRow> result;
  for (const SidebarRow& row : rows_) {
    if (row.space == active) {
      result.push_back(row);
    }
  }
  int active_tab_index = -1;
  for (const SidebarRow& row : result) {
    if (row.is_active && !row.is_cold) {
      active_tab_index = row.tab_index;
      break;
    }
  }
  GroupSplitRows(result, active_tab_index);
  return result;
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
  // In the space on screen, as the real model's new tab is. Tagged with any
  // other space it would be the active row of a list that does not draw it.
  InsertTab(u"New Tab", "about:blank", SidebarSection::kToday,
            /*active=*/true, ActiveSpaceId());
}

void FakeSidebarModel::ClearToday() {
  std::erase_if(rows_, [](const SidebarRow& r) {
    return r.section == SidebarSection::kToday;
  });
  Reindex();
  Notify();
}

void FakeSidebarModel::AddToFavorites(int tab_index) {
  MakeEntry(tab_index, SidebarSection::kFavorites,
            std::numeric_limits<int>::max());
}

void FakeSidebarModel::PinTab(int tab_index) {
  MakeEntry(tab_index, SidebarSection::kPinned,
            std::numeric_limits<int>::max());
}

void FakeSidebarModel::MoveTabToSection(int tab_index,
                                        SidebarSection section,
                                        int position) {
  // Today is where the tab already is; the drop that would mean it is
  // MoveTab.
  if (section == SidebarSection::kToday) {
    return;
  }
  MakeEntry(tab_index, section, position);
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

void FakeSidebarModel::SetTabTitle(int tab_index,
                                   const GURL& expected_url,
                                   const std::u16string& title) {
  for (SidebarRow& row : rows_) {
    if (row.tab_index == tab_index && row.url == expected_url) {
      // The fake has no page title to fall back to, so an empty name is
      // stored as given; the real model restores the tab's own title.
      row.title = title;
      Notify();
      return;
    }
  }
}

void FakeSidebarModel::ReturnToPinnedUrl(EntryId id) {
  if (SidebarRow* row = FindByEntry(id)) {
    row->can_return_to_pinned_url = false;
    Notify();
  }
}

void FakeSidebarModel::MoveEntryToSection(EntryId id,
                                          SidebarSection section,
                                          int position) {
  SidebarRow* found = FindByEntry(id);
  if (!found) {
    return;
  }
  // Copied out before the erase below invalidates it.
  SidebarRow moved = *found;
  const size_t moved_at = static_cast<size_t>(found - rows_.data());
  std::erase_if(rows_, [id](const SidebarRow& r) { return r.entry_id == id; });

  if (section == SidebarSection::kToday) {
    // The entry goes, the page stays. A warm entry's tab falls back into
    // Today because nothing claims it any more; a cold entry's URL is opened
    // as a tab, which is what turns the row warm here. Either way there is a
    // live tab where the entry was, so nothing is lost.
    moved.entry_id = EntryId();
    moved.section = SidebarSection::kToday;
    moved.is_cold = false;
    moved.can_return_to_pinned_url = false;
    moved.folder_id.reset();
    // Where the insertion line was drawn, not the end: Today's order is the
    // tab strip's, so a drop into it places the tab. The entry's own row was
    // erased above, so this walk counts exactly the rows `position` counted.
    rows_.insert(SlotIn(SidebarSection::kToday, position), std::move(moved));
    Reindex();
    Notify();
    return;
  }

  // The real model moves a linked pinned pair as one when either is
  // reordered. The fake has no links, so two entries sharing a split inside
  // the section being reordered stand in for one, in their order.
  const std::optional<split_tabs::SplitTabId> split =
      moved.section == section ? moved.split : std::nullopt;
  std::vector<SidebarRow> together = {std::move(moved)};
  if (split.has_value()) {
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
      if (it->split == split && it->section == section &&
          it->entry_id.is_valid()) {
        // Rows before the moved one kept their index through the erase.
        const bool before = static_cast<size_t>(it - rows_.begin()) < moved_at;
        SidebarRow partner = std::move(*it);
        rows_.erase(it);
        together.insert(before ? together.begin() : together.end(),
                        std::move(partner));
        break;
      }
    }
  }
  for (SidebarRow& row : together) {
    row.section = section;
    // A favourite is a tile in the grid; it has nowhere to be indented to, so
    // it leaves any folder behind. ArciumModel::SetEntryKind does the same.
    if (section == SidebarSection::kFavorites) {
      row.folder_id.reset();
    }
  }
  rows_.insert(SlotIn(section, position),
               std::make_move_iterator(together.begin()),
               std::make_move_iterator(together.end()));
  Reindex();
  Notify();
}

std::vector<SidebarRow>::iterator FakeSidebarModel::SlotIn(
    SidebarSection section,
    int position) {
  // Walk to the `position`-th row of `section`, or to the end of the section
  // if it holds fewer, which is what ArciumModel::ReorderEntry's clamp does.
  int seen = 0;
  auto it = rows_.begin();
  while (it != rows_.end() &&
         (static_cast<int>(it->section) < static_cast<int>(section) ||
          (it->section == section && seen < position))) {
    if (it->section == section) {
      ++seen;
    }
    ++it;
  }
  return it;
}

std::vector<SidebarFolder> FakeSidebarModel::folders() const {
  std::vector<FolderInput> input;
  for (const FakeFolder& folder : folders_) {
    FolderInput in;
    in.id = folder.id;
    in.parent_id = folder.parent_id;
    in.position = folder.position;
    in.name = folder.name;
    in.collapsed = folder.collapsed;
    for (const SidebarRow& row : rows_) {
      if (row.folder_id == folder.id) {
        ++in.direct_entry_count;
      }
    }
    input.push_back(std::move(in));
  }
  // The same flattening the browser model uses, so a view test cannot pass
  // here against an order the real sidebar never produces.
  return BuildSidebarFolders(std::move(input));
}

void FakeSidebarModel::SetFolderParent(FolderId id,
                                       std::optional<FolderId> parent_id) {
  if (!CanMoveFolderTo(id, parent_id)) {
    return;
  }
  for (FakeFolder& folder : folders_) {
    if (folder.id == id) {
      folder.parent_id = parent_id;
      // Last among its new siblings, then renumbered -- ArciumModel's rule.
      folder.position = std::numeric_limits<int>::max();
      NormaliseFolderPositions();
      Notify();
      return;
    }
  }
}

bool FakeSidebarModel::CanMoveFolderTo(
    FolderId id,
    std::optional<FolderId> parent_id) const {
  // Answered from the drawn tree rather than by mirroring ArciumModel's own
  // walk. A second implementation on purpose: this fake exists so a view test
  // does not need a browser, and a bug the two share would be invisible to
  // both.
  return CanMoveFolderInTree(folders(), id, parent_id);
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
  if (!CanCreateFolderWithEntry(id)) {
    return FolderId();
  }
  FakeFolder folder;
  folder.id = FolderId::Generate();
  folder.name = name;
  // Inside the folder the entry is already in, so a folder made from a nested
  // row appears where that row was drawn. Set before the position count
  // below, which counts the siblings this folder will actually have.
  folder.parent_id = row->folder_id;
  // The same rule ArciumModel::AddFolder uses: the next free position among
  // the folders that share this one's parent.
  folder.position = static_cast<int>(std::count_if(
      folders_.begin(), folders_.end(), [&folder](const FakeFolder& existing) {
        return existing.parent_id == folder.parent_id;
      }));
  folders_.push_back(folder);
  row->folder_id = folder.id;
  Notify();
  return folder.id;
}

bool FakeSidebarModel::CanCreateFolderWithEntry(EntryId id) const {
  const SidebarRow* row = FindByEntry(id);
  if (!row || row->section != SidebarSection::kPinned) {
    return false;
  }
  if (!row->folder_id.has_value()) {
    return true;
  }
  // Through folders(), which is where this fake's depths are computed, rather
  // than by walking parents here: a second implementation of depth is a
  // second thing that can disagree with the real model.
  for (const SidebarFolder& folder : folders()) {
    if (folder.id == *row->folder_id) {
      return folder.depth + 1 <= kMaxFolderDepth - 1;
    }
  }
  return false;
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
  std::optional<FolderId> parent;
  bool found = false;
  for (const FakeFolder& folder : folders_) {
    if (folder.id == id) {
      parent = folder.parent_id;
      found = true;
      break;
    }
  }
  if (!found) {
    return;
  }
  std::erase_if(folders_, [id](const FakeFolder& f) { return f.id == id; });
  // Up one level, entries and subfolders alike -- ArciumModel::RemoveFolder's
  // rule. For a top-level folder that is the top level, which is what this
  // did before there was another level to move to.
  for (FakeFolder& folder : folders_) {
    if (folder.parent_id == id) {
      folder.parent_id = parent;
    }
  }
  for (SidebarRow& row : rows_) {
    if (row.folder_id == id) {
      row.folder_id = parent;
    }
  }
  NormaliseFolderPositions();
  Notify();
}

void FakeSidebarModel::SetArchiveTimeout(ArchiveTimeout timeout) {
  archive_timeout_ = timeout;
  Notify();
}

ArchiveTimeout FakeSidebarModel::archive_timeout() const {
  return archive_timeout_;
}

void FakeSidebarModel::AddArchived(const std::u16string& title,
                                   const std::string& url,
                                   base::Time archived_at) {
  ArchivedRow row;
  row.url = GURL(url);
  row.title = title;
  row.archived_at = archived_at;
  archived_.push_back(std::move(row));
}

void FakeSidebarModel::SetHasArchive(bool has_archive) {
  has_archive_ = has_archive;
}

void FakeSidebarModel::SetArchiveReadable(bool readable) {
  archive_readable_ = readable;
}

void FakeSidebarModel::SetHoldArchiveReplies(bool hold) {
  hold_archive_replies_ = hold;
}

void FakeSidebarModel::DeliverHeldArchiveReplies() {
  std::vector<std::pair<int, ArchivedRowsCallback>> held;
  held.swap(held_replies_);
  for (auto& [limit, callback] : held) {
    --pending_archive_requests_;
    std::move(callback).Run(RowsFor(limit), archive_readable_);
  }
}

bool FakeSidebarModel::has_archive() const {
  return has_archive_;
}

void FakeSidebarModel::RequestArchivedRows(int limit,
                                           ArchivedRowsCallback callback) {
  // Posted, and through a WeakPtr, exactly as SidebarTabModel does it. Note
  // this is the one place this fake is deliberately *not* synchronous: see
  // the class comment.
  ++pending_archive_requests_;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&FakeSidebarModel::DeliverArchivedRows,
                     weak_factory_.GetWeakPtr(), limit, std::move(callback)));
}

void FakeSidebarModel::DeliverArchivedRows(int limit,
                                           ArchivedRowsCallback callback) {
  if (hold_archive_replies_) {
    // Still pending: it has not been answered, it is only parked. The real
    // reply is a background SQLite read and can take arbitrarily long.
    held_replies_.emplace_back(limit, std::move(callback));
    return;
  }
  --pending_archive_requests_;
  std::move(callback).Run(RowsFor(limit), archive_readable_);
}

std::vector<ArchivedRow> FakeSidebarModel::RowsFor(int limit) const {
  if (!archive_readable_) {
    // An archive that will not open lists nothing, whatever was put in it.
    return {};
  }
  std::vector<ArchivedRow> rows = archived_;
  if (limit >= 0 && rows.size() > static_cast<size_t>(limit)) {
    rows.resize(static_cast<size_t>(limit));
  }
  return rows;
}

void FakeSidebarModel::ReopenArchived(const GURL& url, base::Time archived_at) {
  // The real model opens a tab and deletes the row. There is no tab strip
  // here, so it records the call and deletes the row — which is the half the
  // list can see.
  for (const ArchivedRow& row : archived_) {
    if (row.url == url && row.archived_at == archived_at) {
      reopened_.push_back(row);
      break;
    }
  }
  std::erase_if(archived_, [&](const ArchivedRow& row) {
    return row.url == url && row.archived_at == archived_at;
  });
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
  // Numbered among siblings -- same parent -- the way
  // ArciumModel::NormalisePositions does, so a nested folder's position is a
  // place in its own list.
  std::map<std::optional<FolderId>, std::vector<FakeFolder*>> by_parent;
  for (FakeFolder& folder : folders_) {
    by_parent[folder.parent_id].push_back(&folder);
  }
  for (auto& [parent, siblings] : by_parent) {
    std::stable_sort(siblings.begin(), siblings.end(),
                     [](const FakeFolder* a, const FakeFolder* b) {
                       return a->position < b->position;
                     });
    for (size_t i = 0; i < siblings.size(); ++i) {
      siblings[i]->position = static_cast<int>(i);
    }
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

const SidebarRow* FakeSidebarModel::FindByEntry(EntryId id) const {
  return const_cast<FakeSidebarModel*>(this)->FindByEntry(id);
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

void FakeSidebarModel::MakeEntry(int tab_index,
                                 SidebarSection section,
                                 int position) {
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
  // The tab's row is gone before the walk, so `position` counts the entries
  // that were already in the section — the gap the insertion indicator was
  // drawn in. SidebarTabModel gets the same answer by appending and then
  // reordering.
  rows_.insert(SlotIn(section, position), std::move(row));
  Reindex();
  Notify();
}

}  // namespace arcium
