// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/update_settings.h"

#include <optional>
#include <string>

#include "arcium/browser/update/update_preference.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

TEST(UpdateSettingsTest, TheRowOffersEveryChoiceTheSettingHas) {
  // Built from the setting's own values rather than written out again, so
  // that a row and the browser can never disagree about what was chosen.
  std::optional<base::ListValue> parsed =
      base::JSONReader::ReadList(UpdateModeOptionsJson(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  const base::ListValue& options = *parsed;
  ASSERT_EQ(3u, options.size());

  const int values[] = {options[0].GetDict().FindInt("value").value_or(-1),
                        options[1].GetDict().FindInt("value").value_or(-1),
                        options[2].GetDict().FindInt("value").value_or(-1)};
  EXPECT_EQ(static_cast<int>(UpdateMode::kAsk), values[0])
      << "asking comes first, because it is what a new reader gets";
  EXPECT_EQ(static_cast<int>(UpdateMode::kAutomatic), values[1]);
  EXPECT_EQ(static_cast<int>(UpdateMode::kOff), values[2]);

  for (const base::Value& option : options) {
    const std::string* name = option.GetDict().FindString("name");
    ASSERT_TRUE(name);
    EXPECT_FALSE(name->empty());
  }
  EXPECT_NE(*options[0].GetDict().FindString("name"),
            *options[1].GetDict().FindString("name"));
}

}  // namespace
}  // namespace arcium
