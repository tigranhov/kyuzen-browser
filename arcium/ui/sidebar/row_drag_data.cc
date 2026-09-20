// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/row_drag_data.h"

#include <string>

#include "base/no_destructor.h"
#include "base/pickle.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/os_exchange_data.h"

namespace arcium {

namespace {

// Reverse-DNS, the shape every platform's custom drag type wants.
constexpr char kFormatString[] = "io.github.tigranhov.yohaku.sidebar-row";

}  // namespace

// static
const ui::ClipboardFormatType& RowDragData::Format() {
  static const base::NoDestructor<ui::ClipboardFormatType> format(
      ui::ClipboardFormatType::CustomPlatformType(kFormatString));
  return *format;
}

void RowDragData::Write(ui::OSExchangeData* data) const {
  base::Pickle pickle;
  // Each id as its string, because that is what a TypedId is: parsing it back
  // through FromString is what rejects a malformed payload.
  pickle.WriteString(entry_id.value());
  pickle.WriteString(folder_id.value());
  pickle.WriteInt(tab_index);
  data->SetPickledData(Format(), pickle);
}

// static
std::optional<RowDragData> RowDragData::Read(const ui::OSExchangeData& data) {
  std::optional<base::Pickle> pickle = data.GetPickledData(Format());
  if (!pickle.has_value()) {
    return std::nullopt;
  }
  base::PickleIterator it(*pickle);
  std::string entry;
  std::string folder;
  RowDragData payload;
  if (!it.ReadString(&entry) || !it.ReadString(&folder) ||
      !it.ReadInt(&payload.tab_index)) {
    return std::nullopt;
  }
  // An empty string is a row that is not that kind of thing, which is
  // legitimate; anything else malformed reads back invalid and is treated the
  // same way.
  payload.entry_id = EntryId::FromString(entry);
  payload.folder_id = FolderId::FromString(folder);
  // Exactly one thing, or nothing worth carrying. A payload naming both is
  // not something any source here writes, so it is a corrupt one.
  if (payload.is_entry() && payload.is_folder()) {
    return std::nullopt;
  }
  if (!payload.is_entry() && !payload.is_folder() && !payload.is_tab()) {
    return std::nullopt;
  }
  return payload;
}

}  // namespace arcium
