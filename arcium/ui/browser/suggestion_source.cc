// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/suggestion_source.h"

#include <set>
#include <utility>

#include "arcium/ui/browser/tab_search_service.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"

namespace arcium {

namespace {

// Enough that the reader's own tabs, pins and archived pages fill the box on
// their own when they match, without asking the archive for a page of rows
// nobody will scroll to.
constexpr int kOwnResultLimit = 8;

}  // namespace

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

std::vector<SuggestionRow> MergeSuggestions(std::vector<SuggestionRow> mine,
                                            std::vector<SuggestionRow> web) {
  std::set<GURL> seen;
  std::vector<SuggestionRow> merged;
  // What the reader already put somewhere outranks what they once passed
  // through, however well the second half scored it.
  for (std::vector<SuggestionRow>* half : {&mine, &web}) {
    for (SuggestionRow& row : *half) {
      if (!seen.insert(row.destination).second) {
        continue;
      }
      merged.push_back(std::move(row));
    }
  }
  return merged;
}

SuggestionSource::SuggestionSource(Profile* profile, TabSearchService* tabs)
    : profile_(profile), tabs_(tabs) {
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
  mine_.clear();
  web_.clear();
  if (text.empty()) {
    controller_->Stop(AutocompleteStopReason::kClobbered);
    on_rows_.Run({});
    return;
  }
  if (tabs_) {
    tabs_->Search(text, kOwnResultLimit,
                  base::BindOnce(&SuggestionSource::OnOwnResults,
                                 weak_factory_.GetWeakPtr()));
  }
  AutocompleteInput input(text, metrics::OmniboxEventProto::OTHER,
                          ChromeAutocompleteSchemeClassifier(profile_));
  controller_->Start(input);
}

void SuggestionSource::OnOwnResults(std::vector<SearchResult> results) {
  mine_.clear();
  for (const SearchResult& result : results) {
    if (!result.url.is_valid()) {
      continue;
    }
    SuggestionRow row;
    row.destination = result.url;
    row.title = result.title.empty() ? base::UTF8ToUTF16(result.url.spec())
                                     : result.title;
    row.subtitle = base::UTF8ToUTF16(result.url.spec());
    // A pinned entry and an archived page are pages to open; only a live tab
    // is a tab to switch to.
    row.is_open_tab = result.source == SearchResult::Source::kLiveTab;
    mine_.push_back(std::move(row));
  }
  DeliverRows();
}

void SuggestionSource::DeliverRows() {
  if (on_rows_) {
    on_rows_.Run(MergeSuggestions(mine_, web_));
  }
}

void SuggestionSource::Stop() {
  controller_->Stop(AutocompleteStopReason::kClobbered);
}

void SuggestionSource::OnResultChanged(AutocompleteController* controller,
                                       bool default_match_changed) {
  web_ = RowsForResult(controller->result());
  DeliverRows();
}

}  // namespace arcium
