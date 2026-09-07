// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_TAB_SEARCH_SERVICE_H_
#define ARCIUM_UI_BROWSER_TAB_SEARCH_SERVICE_H_

#include <string>
#include <vector>

#include "arcium/browser/model/entry_id.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/tabs/public/tab_interface.h"
#include "url/gurl.h"

class TabStripModel;

namespace arcium {

class ArchiveService;
class ArciumModel;
class TabBinding;
struct ArchiveReadResult;

// One hit, from whichever of the three places a tab can be.
struct SearchResult {
  // Ordered by how cheap the result is to act on, because that is also the
  // ranking tie-break: a tab you can switch to beats one that must be opened,
  // which beats one that must be un-archived.
  enum class Source { kLiveTab, kEntry, kArchive };

  SearchResult();
  SearchResult(const SearchResult&);
  SearchResult(SearchResult&&);
  SearchResult& operator=(const SearchResult&);
  SearchResult& operator=(SearchResult&&);
  ~SearchResult();

  Source source = Source::kLiveTab;
  std::u16string title;
  GURL url;

  // Valid only for kEntry.
  EntryId entry_id;

  // Set only for kLiveTab, and a handle rather than a strip index on purpose.
  // Every result reaches its caller across an async boundary, and a strip
  // index taken before the archive read is not the index the tab has when the
  // answer arrives — a tab closed or moved in between would silently rename
  // the result to a different tab. A handle is weak: the tab either is still
  // there or reads as null. Stage 4 turns it into an index at the moment it
  // acts, with TabStripModel::GetIndexOfTab.
  tabs::TabHandle tab_handle;

  // Set only for kArchive, and needed with `url` to name the row again (the
  // archive's primary key is the pair).
  base::Time archived_at;

  int score = 0;
};

// Searches the three places a tab can be — this window's strip, the profile's
// persistent entries, and the archive — and returns one ranked list.
//
// The engine only. Stage 4's command bar becomes its front end; nothing here
// draws anything or observes anything, so an instance that is never asked a
// question does no work at all and holds no per-tab state.
//
// `tab_strip_model`, `model`, `binding` and `archive_service` must outlive
// this object. Deliberately not a TabStripModelObserver: it reads the strip
// only inside a call, so a registration would exist purely to paper over an
// ownership mistake. Stage 4 owns it where SidebarTabModel is owned, next to
// the strip whose lifetime already bounds both.
class TabSearchService {
 public:
  using ResultsCallback = base::OnceCallback<void(std::vector<SearchResult>)>;

  // `archive_service` may be null — the playground and the tests have none —
  // and its store may be null off the record. Search works either way, with
  // the archive contributing nothing.
  TabSearchService(TabStripModel* tab_strip_model,
                   ArciumModel* model,
                   TabBinding* binding,
                   ArchiveService* archive_service);
  TabSearchService(const TabSearchService&) = delete;
  TabSearchService& operator=(const TabSearchService&) = delete;
  ~TabSearchService();

  // The two in-memory sources only: this window's live tabs and the profile's
  // entries. Synchronous because both are already on this thread and neither
  // touches disk — no archive, by construction rather than by convention.
  //
  // There is deliberately no synchronous entry point that reaches the archive.
  // ArchiveStore blocks on SQLite, "no sync I/O ever" covers reads, and a
  // synchronous method documented as "tests only" is a method the next caller
  // writes from the header.
  std::vector<SearchResult> SearchLocal(const std::u16string& query,
                                        int limit) const;

  // All three sources. The only path that reaches the archive, and the one a
  // UI caller wants.
  //
  // `callback` runs on the calling sequence and only ever on a later turn of
  // the run loop, including when there is no archive at all — a caller that
  // can be answered from inside its own call has a second order of events to
  // be correct in. It is dropped without running if this service has gone by
  // then.
  //
  // The live and entry passes run when the answer is assembled, not when the
  // read is posted, so no result names a tab by anything that could have gone
  // stale while the archive was read.
  //
  // Three caveats about the archive half, none of them visible in the results
  // themselves, so a caller has to be told rather than shown:
  //
  //   * It is limited by RECENCY, NOT BY SCORE. ArchiveStore::Search takes the
  //     newest matching rows and the ranking here re-sorts whatever it gets,
  //     so an old row with a title-prefix match can fall outside that window
  //     and never be seen while a recent URL-only match survives. The store
  //     fetch deliberately over-asks — enough that dropping rows the user can
  //     already reach cannot empty the archive half — but that only widens the
  //     window; it does not order it by score. Ordering it properly means
  //     ranking in SQL, which is the FTS migration ArchiveStore::Search
  //     already anticipates.
  //
  //   * It is case-insensitive but NOT accent-insensitive, while the live and
  //     entry halves are both. The store matches on its folded_title and
  //     folded_url columns, and base::i18n::FoldCase is case-only, so a query
  //     of "cafe" finds a live tab titled "Café" but not an archived one. The
  //     archive therefore returns a subset of what it could and never a
  //     superset — nothing wrong is ever shown, some things are missing.
  //     Fixing it is a schema change, and also the FTS migration.
  //
  //   * It is NOT SCOPED TO A SPACE, while the live and entry halves are both
  //     scoped to ArciumModel::default_space_id(). ArchiveStore::Search has no
  //     space filter, so it searches every space. Stage 2 has exactly one
  //     space, so the two halves agree today and nothing is wrong. Stage 3 has
  //     to pick one answer for both when a window can switch spaces.
  void Search(const std::u16string& query, int limit, ResultsCallback callback);

 private:
  // The reply from ArchiveService's read. Takes the query rather than the
  // rows' word for it: the store matched on its folded shadow columns, and the
  // rows still have to be scored against the same ranking as everything else.
  void OnArchiveRead(std::u16string query,
                     int limit,
                     ResultsCallback callback,
                     ArchiveReadResult archive);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> model_;
  raw_ptr<TabBinding> binding_;
  raw_ptr<ArchiveService> archive_service_;

  base::WeakPtrFactory<TabSearchService> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_TAB_SEARCH_SERVICE_H_
