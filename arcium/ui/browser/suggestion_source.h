// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SUGGESTION_SOURCE_H_
#define ARCIUM_UI_BROWSER_SUGGESTION_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "url/gurl.h"

class AutocompleteResult;
class Profile;

namespace arcium {

class TabSearchService;
struct SearchResult;

// A row the command box draws. Everything it needs and nothing it does not:
// the box never sees an AutocompleteMatch.
struct SuggestionRow {
  std::u16string title;
  std::u16string subtitle;
  GURL destination;
  // True when this is a tab the window already has open, possibly in another
  // space. The box switches to it rather than loading a second copy.
  bool is_open_tab = false;
};

// The pure half: a result in, rows out.
std::vector<SuggestionRow> RowsForResult(const AutocompleteResult& result);

// What the reader already put somewhere, then what the web remembers. A
// destination in both halves is offered once, from the first -- which is the
// half that knows whether it is already open.
std::vector<SuggestionRow> MergeSuggestions(std::vector<SuggestionRow> mine,
                                            std::vector<SuggestionRow> web);

// Chrome's own answers -- history, bookmarks, the search engine, open tabs --
// asked for on behalf of the command box. Nothing here exists until one of
// these does, which is when the box opens.
class SuggestionSource : public AutocompleteController::Observer {
 public:
  using RowsCallback =
      base::RepeatingCallback<void(std::vector<SuggestionRow>)>;

  // `tabs` may be null -- a window with no sidebar has no such service --
  // and then only Chrome's answers are offered. It must outlive this.
  SuggestionSource(Profile* profile, TabSearchService* tabs);
  SuggestionSource(const SuggestionSource&) = delete;
  SuggestionSource& operator=(const SuggestionSource&) = delete;
  ~SuggestionSource() override;

  // Asks for answers to `text`. `on_rows` is called each time the answers
  // change, which is more than once: the fast providers answer first.
  void Start(const std::u16string& text, RowsCallback on_rows);
  void Stop();

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

 private:
  // Both halves answer whenever they are ready, and each call back with the
  // merge of the latest of each, so the box fills in as answers arrive
  // instead of waiting for the slowest one.
  void OnOwnResults(std::vector<SearchResult> results);
  void DeliverRows();

  raw_ptr<Profile> profile_;
  raw_ptr<TabSearchService> tabs_;
  std::unique_ptr<AutocompleteController> controller_;
  RowsCallback on_rows_;
  std::vector<SuggestionRow> mine_;
  std::vector<SuggestionRow> web_;

  base::WeakPtrFactory<SuggestionSource> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SUGGESTION_SOURCE_H_
