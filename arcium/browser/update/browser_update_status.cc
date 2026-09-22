// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/browser_update_status.h"

namespace arcium {
namespace {

UpdateStatus* g_status = nullptr;

}  // namespace

UpdateStatus* GetBrowserUpdateStatus() {
  return g_status;
}

void SetBrowserUpdateStatus(UpdateStatus* status) {
  g_status = status;
}

}  // namespace arcium
