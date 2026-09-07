// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/tab_search_service.h"

#include <algorithm>
#include <set>
#include <utility>

#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/archive_service.h"
#include "base/functional/bind.h"
#include "base/i18n/string_search.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

namespace arcium {
namespace {

// Small integers, highest first. A title prefix is the strongest signal a
// short query gives: it is what the user is looking at while they type.
constexpr int kNoMatch = 0;
constexpr int kUrlSubstring = 1;
constexpr int kTitleSubstring = 2;
constexpr int kTitlePrefix = 3;

using Matcher = base::i18n::FixedPatternStringSearchIgnoringCaseAndAccents;

// Deliberately NOT base::i18n::FoldCase, which is what the archive's shadow
// columns use. Folding is case-only — u_strFoldCase with U_FOLD_CASE_DEFAULT
// maps "CAFE" to "cafe" and "Café" to "café", which do not match — while this
// compares at ICU's primary collation strength, where case and accent are both
// below the level being compared. See the FoldCaseAloneWouldNotMatchAnAccent
// test, which pins that difference so nobody simplifies this back.
//
// The consequence is one asymmetry worth naming: the archive half is
// case-insensitive but not accent-insensitive, because its folded_title and
// folded_url columns are what SQLite matches on. Fixing that is a schema
// change and belongs with the FTS migration ArchiveStore::Search already
// anticipates; until then the archive returns a subset of what it could, never
// a superset, so nothing wrong is ever shown.
int ScoreFor(Matcher& matcher, const std::u16string& title, const GURL& url) {
  size_t index = 0;
  size_t length = 0;
  if (matcher.Search(title, &index, &length)) {
    return index == 0 ? kTitlePrefix : kTitleSubstring;
  }
  // possibly_invalid_spec() rather than spec(): a tab can sit on a URL GURL
  // rejects, and searching it is better than a CHECK.
  if (matcher.Search(base::UTF8ToUTF16(url.possibly_invalid_spec()), &index,
                     &length)) {
    return kUrlSubstring;
  }
  return kNoMatch;
}

// The in-memory half of one query.
struct LocalPass {
  std::vector<SearchResult> results;
  // Every URL the user can already reach in this window — matching or not —
  // so the archive half can drop rows that would only offer to reopen
  // something already in front of them. Built from all live tabs and all
  // entries rather than from the matches, because a tab that does not match
  // the query is still a tab the user has.
  std::set<GURL> reachable;
};

// Live tabs first, then favourites, then pinned entries. That order is the
// stable-sort tie-break within one score, and it is the sidebar's own order.
LocalPass CollectLocal(TabStripModel* tab_strip_model,
                       const ArciumModel& model,
                       const TabBinding& binding,
                       Matcher& matcher) {
  LocalPass pass;

  if (tab_strip_model) {
    for (int i = 0; i < tab_strip_model->count(); ++i) {
      tabs::TabInterface* tab = tab_strip_model->GetTabAtIndex(i);
      TabUIHelper* const ui_helper = TabUIHelper::From(tab);
      const GURL url = ui_helper->GetVisibleURL();
      pass.reachable.insert(url);

      // IsClaimedByEntry, not TabBinding::IsBound. A tab bound to an entry the
      // model no longer holds — what ArciumModel::ReplaceAll leaves behind —
      // is a Today tab: filtering it out here would remove it from the live
      // pass while the entry pass cannot report it either, and the tab would
      // be unfindable.
      if (IsClaimedByEntry(model, binding, tab->GetHandle())) {
        continue;
      }

      // TabUIHelper rather than tabs::TabData::FromTabInterface, which the
      // sidebar uses: they agree on title and URL, but TabData also builds a
      // favicon ImageModel and takes two refcounted helpers per tab, and this
      // runs once per tab per keystroke.
      SearchResult result;
      result.source = SearchResult::Source::kLiveTab;
      result.title = ui_helper->GetTitle();
      result.url = url;
      result.tab_handle = tab->GetHandle();
      result.score = ScoreFor(matcher, result.title, result.url);
      if (result.score != kNoMatch) {
        pass.results.push_back(std::move(result));
      }
    }
  }

  // Stage 2 has exactly one space, and searching the space the window is
  // showing is what the command bar will want. Stage 3 has to revisit this
  // when a window can switch spaces.
  const SpaceId space_id = model.default_space_id();
  for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
    for (const TabEntry* entry : model.EntriesForKind(space_id, kind)) {
      pass.reachable.insert(entry->url);

      SearchResult result;
      result.source = SearchResult::Source::kEntry;
      result.title = entry->DisplayTitle();
      result.url = entry->url;
      result.entry_id = entry->id;
      result.score = ScoreFor(matcher, result.title, result.url);
      if (result.score != kNoMatch) {
        pass.results.push_back(std::move(result));
      }
    }
  }
  return pass;
}

// Score first, then source. std::stable_sort so the discovery order above
// survives inside one (score, source) bucket: two live tabs with the same
// title come back in strip order rather than in whatever order the sort felt
// like, which is what makes the results stable across keystrokes.
void RankAndTruncate(std::vector<SearchResult>& results, int limit) {
  std::stable_sort(results.begin(), results.end(),
                   [](const SearchResult& a, const SearchResult& b) {
                     if (a.score != b.score) {
                       return a.score > b.score;
                     }
                     return a.source < b.source;
                   });
  if (results.size() > static_cast<size_t>(limit)) {
    results.resize(static_cast<size_t>(limit));
  }
}

}  // namespace

SearchResult::SearchResult() = default;
SearchResult::SearchResult(const SearchResult&) = default;
SearchResult::SearchResult(SearchResult&&) = default;
SearchResult& SearchResult::operator=(const SearchResult&) = default;
SearchResult& SearchResult::operator=(SearchResult&&) = default;
SearchResult::~SearchResult() = default;

TabSearchService::TabSearchService(TabStripModel* tab_strip_model,
                                   ArciumModel* model,
                                   TabBinding* binding,
                                   ArchiveService* archive_service)
    : tab_strip_model_(tab_strip_model),
      model_(model),
      binding_(binding),
      archive_service_(archive_service) {}

TabSearchService::~TabSearchService() = default;

std::vector<SearchResult> TabSearchService::SearchLocal(
    const std::u16string& query,
    int limit) const {
  if (query.empty() || limit <= 0) {
    return {};
  }
  Matcher matcher(query);
  LocalPass pass = CollectLocal(tab_strip_model_, *model_, *binding_, matcher);
  RankAndTruncate(pass.results, limit);
  return std::move(pass.results);
}

void TabSearchService::Search(const std::u16string& query,
                              int limit,
                              ResultsCallback callback) {
  if (query.empty() || limit <= 0) {
    // Posted, not run here, so every caller sees exactly one order of events.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::vector<SearchResult>()));
    return;
  }

  // The local passes run in OnArchiveRead, not here: a result computed now and
  // delivered after the archive read would name tabs by state that has moved
  // on underneath it.
  if (archive_service_) {
    // ArchiveService owns the store and the sequence it is bound to, and this
    // goes through the same async read path the archive list uses. A second
    // wrapper over that one sequence-affine sql::Database is how the sequence
    // rule gets broken by accident.
    archive_service_->RequestSearch(
        query, limit,
        base::BindOnce(&TabSearchService::OnArchiveRead,
                       weak_factory_.GetWeakPtr(), query, limit,
                       std::move(callback)));
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabSearchService::OnArchiveRead,
                                weak_factory_.GetWeakPtr(), query, limit,
                                std::move(callback), ArchiveReadResult()));
}

void TabSearchService::OnArchiveRead(std::u16string query,
                                     int limit,
                                     ResultsCallback callback,
                                     ArchiveReadResult archive) {
  Matcher matcher(query);
  LocalPass pass = CollectLocal(tab_strip_model_, *model_, *binding_, matcher);

  // ArchiveReadResult::readable is deliberately dropped. The archive list
  // needs it because "nothing archived" and "the file would not open" are
  // different things to tell the user; a ranked result list says neither, and
  // an unreadable archive simply contributes no rows.
  for (ArchivedTab& row : archive.tabs) {
    if (pass.reachable.contains(row.url)) {
      continue;
    }
    SearchResult result;
    result.source = SearchResult::Source::kArchive;
    result.title = std::move(row.title);
    result.url = std::move(row.url);
    result.archived_at = row.archived_at;
    // The store already decided this row matches, on its folded columns. If
    // the primary-strength matcher disagrees — it should not, since it matches
    // strictly more than a case fold does — keep the row at the lowest score
    // rather than dropping it: an archived tab the archive found and the list
    // then hid would be exactly as unfindable as no row at all.
    result.score =
        std::max(ScoreFor(matcher, result.title, result.url), kUrlSubstring);
    pass.results.push_back(std::move(result));
  }

  RankAndTruncate(pass.results, limit);
  std::move(callback).Run(std::move(pass.results));
}

}  // namespace arcium
