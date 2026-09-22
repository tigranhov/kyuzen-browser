// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_SPARKLE_BACKEND_H_
#define ARCIUM_BROWSER_UPDATE_SPARKLE_BACKEND_H_

#include <memory>

#include "arcium/browser/update/updater_backend.h"

namespace arcium {

// Loads Sparkle from inside the application bundle. Reads the framework from
// disk, so it runs off the UI thread. Returns false when the bundle carries no
// Sparkle, which is every build except a release, so that a development
// build never replaces itself with a published one.
bool LoadSparkleFramework();

// The updater, once the framework is loaded; null before that. UI thread.
std::unique_ptr<UpdaterBackend> MakeSparkleBackend();

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_SPARKLE_BACKEND_H_
