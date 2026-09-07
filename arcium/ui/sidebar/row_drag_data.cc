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
constexpr char kFormatString[] = "org.arcium.sidebar-row";

}  // namespace

// static
const ui::ClipboardFormatType& RowDragData::Format() {
  static const base::NoDestructor<ui::ClipboardFormatType> format(
      ui::ClipboardFormatType::CustomPlatformType(kFormatString));
  return *format;
}

void RowDragData::Write(ui::OSExchangeData* data) const {
  base::Pickle pickle;
  // The id as its string, because that is what EntryId is: parsing it back
  // through EntryId::FromString is what rejects a malformed payload.
  pickle.WriteString(entry_id.value());
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
  std::string id;
  RowDragData payload;
  if (!it.ReadString(&id) || !it.ReadInt(&payload.tab_index)) {
    return std::nullopt;
  }
  // An empty string is a row with no entry, which is legitimate; anything
  // else that is not a well-formed id reads back invalid, and the payload is
  // then a tab or nothing at all.
  payload.entry_id = EntryId::FromString(id);
  if (!payload.is_entry() && !payload.is_tab()) {
    return std::nullopt;
  }
  return payload;
}

}  // namespace arcium
