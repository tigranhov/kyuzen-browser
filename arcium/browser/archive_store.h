// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_ARCHIVE_STORE_H_
#define ARCIUM_BROWSER_ARCHIVE_STORE_H_

#include <string>
#include <vector>

#include "arcium/browser/model/entry_id.h"
#include "base/files/file_path.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "url/gurl.h"

namespace arcium {

struct ArchivedTab {
  GURL url;
  std::u16string title;
  SpaceId space_id;
  base::Time archived_at;
};

// The archive grows without bound, so it is SQLite rather than part of the
// JSON model file. Every method blocks; the owner runs it on a background
// sequence and never calls it from the UI thread.
class ArchiveStore {
 public:
  ArchiveStore();
  ArchiveStore(const ArchiveStore&) = delete;
  ArchiveStore& operator=(const ArchiveStore&) = delete;
  ~ArchiveStore();

  // Creates the file and schema if absent. A file that is genuinely
  // unreadable (corruption, not lock contention from another live
  // connection) is razed and recreated: nothing in the archive is worth a
  // recovery path. A transient failure such as a concurrent connection
  // holding the file busy is NOT treated as corruption and does not touch
  // the file; Open() simply returns false so the caller runs with no
  // archive for this session. An unreadable archive must not stop the
  // browser from opening either way.
  [[nodiscard]] bool Open(const base::FilePath& path);

  // Whether the last Open() succeeded, so a reader on this sequence can tell
  // "the archive holds nothing" from "the archive could not be read". Every
  // other method of this class is a no-op or an empty result while this is
  // false, and the two are indistinguishable from the result alone — which is
  // how the archive list came to tell the user their tabs were never archived
  // when in fact the file would not open. False before Open() has run at all:
  // the owner posts Open() and the UI thread can ask sooner than that.
  bool is_open() const { return open_; }

  // sql::Database is sequence-affine and binds to whichever sequence first
  // touches it — which, for a store constructed on the UI thread, is the UI
  // thread. An owner that constructs the store here and then hands it to a
  // background sequence must call this in between, before the first posted
  // call. See ArciumProfileState.
  void DetachFromSequence();

  void Add(const ArchivedTab& tab);
  std::vector<ArchivedTab> ListRecent(SpaceId space_id, int limit);

  // At most one row per URL — the newest matching one — newest first. So
  // `limit` bounds DISTINCT URLS, not rows: a page archived five times is one
  // result carrying its most recent archived_at, not five.
  //
  // The archive keys rows by (url, archived_at) and Add() writes a fresh row
  // on every archiving, so one URL accumulates rows as the user closes and
  // reopens the same page. Handing all of them back is worse for the reader
  // (one page listed five times) and actively wrong for a caller that filters
  // the result and sized its request assuming one row per URL — it can have
  // its whole result filtered away while a match it wanted sits just outside
  // the LIMIT. TabSearchService is exactly that caller; see StoreFetchLimit
  // in tab_search_service.cc.
  //
  // Matching is on the folded shadow columns, which are case-folded only:
  // case-insensitive, not accent-insensitive. Ordering is by recency, never
  // by relevance.
  std::vector<ArchivedTab> Search(const std::u16string& query, int limit);

  void Remove(const GURL& url, base::Time archived_at);

  // Exposes the file-unusable classifier for a unit test that pins it
  // directly: the codes it must never crash on (SQLITE_BUSY, SQLITE_LOCKED,
  // SQLITE_NOMEM, an unrecognised code) are hard to reach through a real
  // sql::Database, since they name transient or environmental conditions,
  // not something a test can reliably provoke. Production code must reach
  // the classifier only through Open(); see archive_store.cc.
  static bool IsFileUnusableForTesting(int sqlite_error_code);

 private:
  // Creates the schema inside its own transaction. On failure, `*sqlite_error`
  // is set to the sqlite error code captured at the exact statement that
  // failed (Begin/Execute/Run/Commit) — the caller must not re-read
  // db_.GetErrorCode() afterwards instead, because by the time InitSchema()
  // returns, the local sql::Transaction has already rolled back (either via
  // sql::Database::CommitTransaction's own internal recovery for a failed
  // COMMIT, or via sql::Transaction's destructor for an abandoned Begin()),
  // and a successful ROLLBACK overwrites the connection's error code with
  // SQLITE_OK. See the Commit() branch in the .cc for why that capture is
  // itself not fully trustworthy either. `*sqlite_error` is left untouched
  // on success.
  bool InitSchema(int* sqlite_error);

  // Open()'s body. Open() is the wrapper that records the answer in `open_`,
  // so there is exactly one place the flag can be set and no early return can
  // skip it.
  [[nodiscard]] bool OpenInternal(const base::FilePath& path);

  sql::Database db_;
  // The result of the last OpenInternal(). See is_open().
  bool open_ = false;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_ARCHIVE_STORE_H_
