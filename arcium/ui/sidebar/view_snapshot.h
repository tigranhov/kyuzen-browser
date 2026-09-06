// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_VIEW_SNAPSHOT_H_
#define ARCIUM_UI_SIDEBAR_VIEW_SNAPSHOT_H_

#include "base/functional/callback_forward.h"

namespace base {
class FilePath;
}

namespace views {
class View;
}

namespace arcium {

// Paints the widget that hosts `view` offscreen at `scale` and writes the
// part covering `view` as a PNG. Layers that are not Views (web contents,
// video) come out blank. Used by --snapshot in the playground and by the
// --arcium-snapshot debugging switch in the browser; screen capture needs a
// macOS permission that automated runs do not have. The file is written on
// the thread pool (the UI thread must not block); `done` runs on the calling
// sequence with the result.
void WriteViewSnapshot(views::View* view,
                       const base::FilePath& path,
                       float scale,
                       base::OnceCallback<void(bool)> done);

// Logs class, bounds and visibility of `view` and its descendants.
void LogViewHierarchy(const views::View* view);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_VIEW_SNAPSHOT_H_
