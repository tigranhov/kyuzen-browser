// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/update_settings.h"

#include <string>

#include "arcium/browser/update/update_preference.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "content/public/browser/web_ui_data_source.h"

namespace arcium {
namespace {

base::DictValue Option(UpdateMode mode, const char* name) {
  return base::DictValue()
      .Set("value", static_cast<int>(mode))
      .Set("name", name);
}

}  // namespace

std::string UpdateModeOptionsJson() {
  base::ListValue options;
  // Asking first, because it is what a reader who has chosen nothing gets.
  options.Append(Option(UpdateMode::kAsk, "Ask me before installing"));
  options.Append(Option(UpdateMode::kAutomatic, "Install quietly"));
  options.Append(Option(UpdateMode::kOff, "Never check"));
  return base::WriteJson(options).value_or("[]");
}

void AddUpdateSettingStrings(content::WebUIDataSource* source) {
  source->AddString("kyuzenUpdateLabel", "Keep Kyuzen up to date");
  source->AddString("kyuzenUpdateModeOptions", UpdateModeOptionsJson());
}

}  // namespace arcium
