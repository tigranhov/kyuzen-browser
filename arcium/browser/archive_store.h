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

  void Add(const ArchivedTab& tab);
  std::vector<ArchivedTab> ListRecent(SpaceId space_id, int limit);
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

  sql::Database db_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_ARCHIVE_STORE_H_
