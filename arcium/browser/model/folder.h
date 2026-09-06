// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_FOLDER_H_
#define ARCIUM_BROWSER_MODEL_FOLDER_H_

#include <string>

#include "arcium/browser/model/entry_id.h"

namespace arcium {

// Holds pinned entries. Not built on Chromium tab groups: a group holds live
// tabs, and a cold pinned entry has none.
struct Folder {
  FolderId id;
  SpaceId space_id;
  std::u16string name;
  bool collapsed = false;
  int position = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_FOLDER_H_
