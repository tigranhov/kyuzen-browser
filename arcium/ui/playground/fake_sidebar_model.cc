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

void FakeSidebarModel::SetLoading(int tab_index, bool loading) {
  rows_[tab_index].is_loading = loading;
  Notify();
}

void FakeSidebarModel::SetAudible(int tab_index, bool audible) {
  rows_[tab_index].is_audible = audible;
  Notify();
}

std::vector<SidebarRow> FakeSidebarModel::rows() const {
  return rows_;
}

void FakeSidebarModel::ActivateTab(int tab_index) {
  for (SidebarRow& r : rows_) {
    r.is_active = r.tab_index == tab_index;
  }
  Notify();
}

void FakeSidebarModel::CloseTab(int tab_index) {
  if (tab_index < 0 || tab_index >= static_cast<int>(rows_.size())) {
    return;
  }
  const bool was_active = rows_[tab_index].is_active;
  rows_.erase(rows_.begin() + tab_index);
  Reindex();
  if (was_active && !rows_.empty()) {
    rows_[std::min<size_t>(tab_index, rows_.size() - 1)].is_active = true;
  }
  Notify();
}

void FakeSidebarModel::MoveTab(int from_index, int to_index) {
  if (from_index < 0 || to_index < 0 ||
      from_index >= static_cast<int>(rows_.size()) ||
      to_index >= static_cast<int>(rows_.size())) {
    return;
  }
  SidebarRow row = std::move(rows_[from_index]);
  rows_.erase(rows_.begin() + from_index);
  rows_.insert(rows_.begin() + to_index, std::move(row));
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

void FakeSidebarModel::Reindex() {
  for (size_t i = 0; i < rows_.size(); ++i) {
    rows_[i].tab_index = static_cast<int>(i);
  }
}

}  // namespace arcium
