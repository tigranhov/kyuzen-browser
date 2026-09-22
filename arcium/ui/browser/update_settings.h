// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_UPDATE_SETTINGS_H_
#define ARCIUM_UI_BROWSER_UPDATE_SETTINGS_H_

#include <string>

namespace content {
class WebUIDataSource;
}

namespace arcium {

// The three choices the settings row offers, as the page's dropdown wants
// them: a name to show and the value to store. Built from the setting's own
// values, so that the row and the browser cannot come to disagree.
std::string UpdateModeOptionsJson();

// The row's text, for the settings page to read (patch 0270).
void AddUpdateSettingStrings(content::WebUIDataSource* source);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_UPDATE_SETTINGS_H_
