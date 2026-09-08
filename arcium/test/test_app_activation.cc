// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/test_app_activation.h"

namespace arcium::test {

// Only macOS promotes a test binary to a foreground application, so only the
// macOS build has anything to undo.
void SuppressTestAppActivation() {}

}  // namespace arcium::test
