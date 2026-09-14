// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/suggestion_source.h"

#include <utility>

#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"

namespace arcium {

std::vector<SuggestionRow> RowsForResult(const AutocompleteResult& result) {
  std::vector<SuggestionRow> rows;
  for (const AutocompleteMatch& match : result) {
    if (!match.destination_url.is_valid()) {
      // A row that goes nowhere is a row that lies about being clickable.
      continue;
    }
    SuggestionRow row;
    row.destination = match.destination_url;
    row.is_open_tab = match.has_tab_match.value_or(false);
    row.title = match.description.empty() ? match.contents : match.description;
    row.subtitle =
        match.description.empty() ? std::u16string() : match.contents;
    rows.push_back(std::move(row));
  }
  return rows;
}

SuggestionSource::SuggestionSource(Profile* profile) : profile_(profile) {
  AutocompleteControllerConfig config;
  config.provider_types = AutocompleteClassifier::DefaultOmniboxProviders();
  // Tabs open in other spaces are offered without having to be asked for by
  // keyword: a space you are not looking at is still your browser.
  config.unscoped_open_tab_suggestions = true;
  controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile), config);
  controller_->AddObserver(this);
}

SuggestionSource::~SuggestionSource() {
  controller_->RemoveObserver(this);
}

void SuggestionSource::Start(const std::u16string& text, RowsCallback on_rows) {
  on_rows_ = std::move(on_rows);
  if (text.empty()) {
    controller_->Stop(AutocompleteStopReason::kClobbered);
    on_rows_.Run({});
    return;
  }
  AutocompleteInput input(text, metrics::OmniboxEventProto::OTHER,
                          ChromeAutocompleteSchemeClassifier(profile_));
  controller_->Start(input);
}

void SuggestionSource::Stop() {
  controller_->Stop(AutocompleteStopReason::kClobbered);
}

void SuggestionSource::OnResultChanged(AutocompleteController* controller,
                                       bool default_match_changed) {
  if (on_rows_) {
    on_rows_.Run(RowsForResult(controller->result()));
  }
}

}  // namespace arcium
