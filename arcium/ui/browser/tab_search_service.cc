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
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/functional/bind.h"
#include "base/i18n/string_search.h"
#include "base/location.h"
#include "base/numerics/clamped_math.h"
#include "base/numerics/safe_conversions.h"
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
// The consequence is that the two halves of one query do not fold alike: the
// archive matches case-insensitively but not accent-insensitively. That is a
// caller-visible caveat rather than a note for whoever edits this function, so
// it is stated where a caller reads it — on TabSearchService::Search in the
// header — and not repeated here.
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

// How many rows to ask the store for when the caller wants `limit` results.
//
// ArchiveStore::Search applies its LIMIT by recency, in SQL, and the URL
// suppression in OnArchiveRead runs afterwards in C++. Asking for exactly
// `limit` rows therefore lets suppression empty the archive half completely:
// when the newest `limit` matching rows are all URLs the user already has
// open, every one of them is dropped and older, non-duplicate matches are
// never looked at at all.
//
// The over-fetch is additive rather than a multiplier because it can be
// exact. At most one row is suppressed per reachable URL, and there are at
// most (live tabs + entries) of those, so asking for limit + that many
// guarantees `limit` unsuppressed rows survive whenever the store holds them.
// A 2x or 4x factor would only make the hole rarer, and rare is how this one
// got as far as a review.
//
// "At most one row suppressed per reachable URL" is a fact about the store,
// not an assumption made here: ArchiveStore::Search returns at most one row
// per URL, so its LIMIT counts distinct URLs and this addition is in the same
// unit as the thing it is compensating for. That is load-bearing. The archive
// keys rows by (url, archived_at), so one URL accumulates a row per
// archiving; if those all reached this code, a single reachable URL could
// occupy the whole window on its own and be suppressed row by row, which is
// the failure the addition exists to prevent, reappearing one level down.
// Should that grouping ever be relaxed, this bound stops being exact — it
// does not merely get looser.
//
// The counts are read here rather than from the reachable set, which does not
// exist until the reply: both are O(1) — count() is a size, and entries() is
// the whole vector rather than the default space's slice precisely so no
// per-kind vector has to be built to count it — and both are upper bounds, so
// the fetch errs high. It cannot grow independently of the user either: every
// extra row it asks for stands for a tab or an entry already resident in
// memory.
int StoreFetchLimit(int limit,
                    TabStripModel* tab_strip_model,
                    const ArciumModel& model) {
  const size_t reachable_bound =
      (tab_strip_model ? static_cast<size_t>(tab_strip_model->count()) : 0u) +
      model.entries().size();
  return base::ClampAdd(limit, base::saturated_cast<int>(reachable_bound));
}

// Live tabs first, then favourites, then pinned entries. That order is the
// stable-sort tie-break within one score, and it is the sidebar's own order.
//
// `reachable`, when non-null, collects every URL the user can already reach
// from this window, in any space — matching or not — so the archive half can
// drop rows that would only offer to reopen something already there. It is
// built from every tab in the strip and every entry of every space rather than
// from the matches, because a tab that misses this query is still a tab the
// user has; ANonMatchingTabOrEntrySuppressesItsArchivedRow is what says so. An
// archived row whose URL matches another space's entry is dropped the same
// way: search finds that entry itself.
//
// Only the archive half reads it, so SearchLocal — the synchronous,
// per-keystroke path — passes null and pays neither the GURL copy nor the tree
// node per tab and per entry.
std::vector<SearchResult> CollectLocal(TabStripModel* tab_strip_model,
                                       const ArciumModel& model,
                                       const TabBinding& binding,
                                       Matcher& matcher,
                                       std::set<GURL>* reachable) {
  std::vector<SearchResult> results;

  if (tab_strip_model) {
    for (int i = 0; i < tab_strip_model->count(); ++i) {
      tabs::TabInterface* tab = tab_strip_model->GetTabAtIndex(i);
      TabUIHelper* const ui_helper = TabUIHelper::From(tab);
      const GURL url = ui_helper->GetVisibleURL();
      if (reachable) {
        reachable->insert(url);
      }

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
      result.space_id = SpaceOfTab(model, binding, tab->GetHandle());
      result.score = ScoreFor(matcher, result.title, result.url);
      if (result.score != kNoMatch) {
        results.push_back(std::move(result));
      }
    }
  }

  // Every space, not just the one the window is showing: tab search finds
  // another space's pinned and favourite entries exactly as it finds this
  // space's, matching the archive half, which has never filtered by space
  // either. `space.id` is only stamped onto the result so RankAndTruncate can
  // use it as a tie-break later — nothing here reads the window's active
  // space at all.
  for (const Space& space : model.spaces()) {
    for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
      for (const TabEntry* entry : model.EntriesForKind(space.id, kind)) {
        if (reachable) {
          reachable->insert(entry->url);
        }

        SearchResult result;
        result.source = SearchResult::Source::kEntry;
        result.title = entry->DisplayTitle();
        result.url = entry->url;
        result.entry_id = entry->id;
        result.space_id = entry->space_id;
        result.score = ScoreFor(matcher, result.title, result.url);
        if (result.score != kNoMatch) {
          results.push_back(std::move(result));
        }
      }
    }
  }
  return results;
}

// Score first, then whether a result is in the window's active space, then
// source. std::stable_sort so the discovery order above survives inside one
// (score, active, source) bucket: two live tabs with the same title in the
// same space come back in strip order rather than in whatever order the sort
// felt like, which is what makes the results stable across keystrokes.
//
// The active-space tie-break is what lets activating a result switch the
// window there: among two results that score the same, the one already in
// the space on screen sorts first, and switching spaces moves a
// same-scoring result from the far side of the tie to the near side without
// changing which results exist at all. See the space caveat on
// TabSearchService::Search.
void RankAndTruncate(std::vector<SearchResult>& results,
                     int limit,
                     SpaceId active_space) {
  std::stable_sort(
      results.begin(), results.end(),
      [active_space](const SearchResult& a, const SearchResult& b) {
        if (a.score != b.score) {
          return a.score > b.score;
        }
        const bool a_active = a.space_id == active_space;
        const bool b_active = b.space_id == active_space;
        if (a_active != b_active) {
          return a_active;
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
                                   ArchiveService* archive_service,
                                   SpaceSwitcher* switcher)
    : tab_strip_model_(tab_strip_model),
      model_(model),
      binding_(binding),
      archive_service_(archive_service),
      switcher_(switcher) {}

TabSearchService::~TabSearchService() = default;

SpaceId TabSearchService::active_space() const {
  return switcher_ ? switcher_->active_space() : model_->default_space_id();
}

std::vector<SearchResult> TabSearchService::SearchLocal(
    const std::u16string& query,
    int limit) const {
  if (query.empty() || limit <= 0) {
    return {};
  }
  Matcher matcher(query);
  // No reachable set: nothing on this path reads it, and building one would
  // cost a GURL copy and a tree node per tab and per entry, per keystroke.
  std::vector<SearchResult> results = CollectLocal(
      tab_strip_model_, *model_, *binding_, matcher, /*reachable=*/nullptr);
  RankAndTruncate(results, limit, active_space());
  return results;
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
    // Over-fetch, so that dropping rows the user can already reach cannot
    // empty the archive half. See StoreFetchLimit. The reply still truncates
    // to `limit`, so the caller's contract is unchanged.
    archive_service_->RequestSearch(
        query, StoreFetchLimit(limit, tab_strip_model_, *model_),
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
  std::set<GURL> reachable;
  std::vector<SearchResult> results =
      CollectLocal(tab_strip_model_, *model_, *binding_, matcher, &reachable);

  // ArchiveReadResult::readable is deliberately dropped. The archive list
  // needs it because "nothing archived" and "the file would not open" are
  // different things to tell the user; a ranked result list says neither, and
  // an unreadable archive simply contributes no rows.
  for (ArchivedTab& row : archive.tabs) {
    if (reachable.contains(row.url)) {
      continue;
    }
    SearchResult result;
    result.source = SearchResult::Source::kArchive;
    result.title = std::move(row.title);
    result.url = std::move(row.url);
    result.space_id = row.space_id;
    result.archived_at = row.archived_at;
    // The store already decided this row matches, on its folded columns. If
    // the primary-strength matcher disagrees — it should not, since it matches
    // strictly more than a case fold does — keep the row at the lowest score
    // rather than dropping it: an archived tab the archive found and the list
    // then hid would be exactly as unfindable as no row at all.
    result.score =
        std::max(ScoreFor(matcher, result.title, result.url), kUrlSubstring);
    results.push_back(std::move(result));
  }

  RankAndTruncate(results, limit, active_space());
  std::move(callback).Run(std::move(results));
}

}  // namespace arcium
