// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/archive_store.h"

#include <string_view>

#include "base/i18n/case_conversion.h"
#include "base/strings/utf_string_conversions.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace arcium {

namespace {

// Registered in tools/metrics/histograms/metadata/sql/histograms.xml by
// patch 0110; sql::Database rejects an unregistered tag at compile time.
constexpr char kDatabaseTag[] = "ArciumArchive";

constexpr int kCurrentVersion = 1;

// Raw SQLite result codes. sql/database.h keeps sqlite3.h out of its own
// public headers so ordinary callers of sql::Database never need to depend
// on SQLite directly, and no feature code outside sql/'s own tests includes
// it either — so these are named locally rather than pulling in
// "third_party/sqlite/sqlite3.h" for three integers. Values confirmed
// against third_party/sqlite/src/amalgamation/sqlite3.h.
constexpr int kSqliteOk = 0;             // SQLITE_OK (line ~455)
constexpr int kSqliteCorrupt = 11;       // SQLITE_CORRUPT (line 461)
constexpr int kSqliteNotADatabase = 26;  // SQLITE_NOTADB (line 476)

// Case- and diacritic-insensitive fold of `text`, as UTF-8. SQLite's LIKE is
// already ASCII-case-insensitive on its own, so folding only matters for
// non-ASCII text (e.g. "ÖKONOMIE" vs "ökonomie") — but that is exactly the
// text a bare `LIKE` gets wrong.
std::string FoldToUtf8(std::u16string_view text) {
  return base::UTF16ToUTF8(base::i18n::FoldCase(text));
}

// Escapes '%', '_' and '\' so a needle built from arbitrary user text is
// matched literally by LIKE ... ESCAPE '\', rather than as a pattern.
std::string EscapeLikePattern(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    if (c == '%' || c == '_' || c == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

// Whether `sqlite_error_code` means the file is not a usable database any
// more, as opposed to something transient (busy, locked, out of memory) or
// unrecognised.
//
// This deliberately does NOT call sql::IsErrorCatastrophic(). That function
// is written for Chromium's error-callback path, where SQLite has already
// narrowed which codes can arrive by the time the callback runs. Read
// directly (sql/error_delegate_util.cc), it NOTREACHED()s — unconditionally
// fatal in this checkout — on SQLITE_OK/SQLITE_ROW/SQLITE_DONE,
// SQLITE_LOCKED, SQLITE_NOMEM, SQLITE_INTERRUPT, SQLITE_NOTFOUND,
// SQLITE_MISUSE, SQLITE_AUTH, SQLITE_RANGE, and any code it does not
// recognise. Open() below is a general path, not an error callback, and can
// genuinely surface every one of those from a concurrent connection, a
// low-memory device, or an unfamiliar SQLite build. So classify here
// instead: name only what actually means "this file is not a database any
// more," and fail open — return false — for everything else, including
// values this function has never seen.
bool IsFileUnusable(int sqlite_error_code) {
  // The primary result code lives in the low 8 bits; an extended code packs
  // detail into the high bits, e.g. SQLITE_CORRUPT_VTAB is
  // SQLITE_CORRUPT | (1 << 8) (third_party/sqlite/src/amalgamation/sqlite3.h,
  // "extended result code" definitions starting at line 485). This matters
  // here because sql::Database::GetErrorCode() returns
  // sqlite3_extended_errcode(), an extended code (sql/database.cc), not the
  // bare primary one — so the mask below is required, not defensive
  // decoration.
  switch (sqlite_error_code & 0xff) {
    case kSqliteCorrupt:
    case kSqliteNotADatabase:
      return true;
    default:
      return false;
  }
}

}  // namespace

// Exclusive locking (Chromium's default) has a second ArchiveStore opening
// the same file collide with one already open: the second connection cannot
// even take a shared lock to read the schema. Disabling it lets legitimate
// concurrent connections (defensive double-open, inspection tooling)
// coexist. That trade is only safe because Open() below now tells lock
// contention (SQLITE_BUSY/SQLITE_LOCKED) apart from real corruption and
// never destroys the file over the former — see Open()'s comment.
ArchiveStore::ArchiveStore()
    : db_(sql::DatabaseOptions().set_exclusive_locking(false),
          sql::Database::Tag(kDatabaseTag)) {}

ArchiveStore::~ArchiveStore() = default;

// static
bool ArchiveStore::IsFileUnusableForTesting(int sqlite_error_code) {
  return IsFileUnusable(sqlite_error_code);
}

void ArchiveStore::DetachFromSequence() {
  db_.DetachFromSequence();
}

bool ArchiveStore::Open(const base::FilePath& path) {
  int sqlite_error = kSqliteOk;
  if (db_.Open(path) && InitSchema(&sqlite_error)) {
    return true;
  }
  // Classify on the error InitSchema() itself captured, not on a fresh
  // db_.GetErrorCode() read. By the time InitSchema() returns, its local
  // sql::Transaction has already rolled back — either via
  // sql::Database::CommitTransaction's own internal recovery when a COMMIT
  // fails with the transaction still open, or via sql::Transaction's
  // destructor unwinding an abandoned Begin() — and a *successful* ROLLBACK
  // resets the connection's error code to SQLITE_OK. Re-reading the
  // connection here would then see SQLITE_OK for a genuine mid-schema
  // failure (e.g. a CREATE TABLE losing a write-lock race), which is exactly
  // the value IsFileUnusable() must never be tricked by.
  //
  // sqlite_error is only left at kSqliteOk when InitSchema() was never
  // reached at all, i.e. db_.Open() itself failed outright; in that case
  // there was no transaction to roll back, so reading the connection
  // directly is accurate.
  if (sqlite_error == kSqliteOk) {
    sqlite_error = db_.GetErrorCode();
  }
  if (!IsFileUnusable(sqlite_error)) {
    // Busy, locked, out of memory, an sqlite code this build has never
    // produced before, or a laundered SQLITE_OK — none of that means the
    // file is corrupt. Never destroy the user's archive over something that
    // might be transient: fail open instead, so the window still opens with
    // no archive for this session (list and search simply come back empty).
    db_.Close();
    return false;
  }
  // Genuinely unusable: nothing in the archive is worth a recovery path.
  // InitSchema() failing (the common case: e.g. SQLITE_NOTADB from a file
  // that isn't a database) always leaves the handle open, so Raze() clears
  // it in place — that keeps SQLite's own bookkeeping consistent and never
  // orphans a -journal/-wal sidecar the way deleting only the main file
  // would.
  if (db_.is_open()) {
    return db_.Raze() && InitSchema(&sqlite_error);
  }
  // db_.Open() itself failed outright (e.g. permission denied, disk full):
  // there is no handle to raze. Delete the file and any sidecars, then try
  // once more against a clean path.
  sql::Database::Delete(path);
  return db_.Open(path) && InitSchema(&sqlite_error);
}

bool ArchiveStore::InitSchema(int* sqlite_error) {
  static constexpr char kCreateTable[] =
      "CREATE TABLE IF NOT EXISTS archived_tabs("
      "  url TEXT NOT NULL,"
      "  title TEXT NOT NULL,"
      "  space_id TEXT NOT NULL,"
      "  archived_at INTEGER NOT NULL,"
      "  folded_url TEXT NOT NULL,"
      "  folded_title TEXT NOT NULL,"
      "  PRIMARY KEY (url, archived_at))";
  // Both indexes serve a query the UI actually makes: the archive list is
  // by space and recency, search is by recency across spaces.
  static constexpr char kCreateSpaceIndex[] =
      "CREATE INDEX IF NOT EXISTS idx_space_time"
      "  ON archived_tabs(space_id, archived_at DESC)";
  static constexpr char kCreateTimeIndex[] =
      "CREATE INDEX IF NOT EXISTS idx_time"
      "  ON archived_tabs(archived_at DESC)";
  // Named "arcium_meta", not "meta": sql::MetaTable reserves the bare "meta"
  // name and gates its own CREATE on DoesTableExist("meta"). Squatting on it
  // means a future switch to that helper would find an incompatible table,
  // skip creating its own, and fail on SetVersionNumber.
  static constexpr char kCreateMeta[] =
      "CREATE TABLE IF NOT EXISTS arcium_meta(version INTEGER NOT NULL)";

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    // Begin() failing never starts the transaction (sql::Transaction's
    // is_active_ stays false), so its destructor below runs no ROLLBACK and
    // db_.GetErrorCode() stays accurate even after this function returns.
    // Captured anyway for uniformity with the branches below.
    *sqlite_error = db_.GetErrorCode();
    return false;
  }
  if (!db_.Execute(kCreateTable) || !db_.Execute(kCreateSpaceIndex) ||
      !db_.Execute(kCreateTimeIndex) || !db_.Execute(kCreateMeta)) {
    // Capture now: `transaction` going out of scope on return will roll
    // back and reset the connection's error code to SQLITE_OK.
    *sqlite_error = db_.GetErrorCode();
    return false;
  }
  // Version row written on creation so a future migration has a floor to
  // read. Stage 2 only ever writes version 1.
  sql::Statement version(db_.GetUniqueStatement(
      "INSERT INTO arcium_meta(version) SELECT ? WHERE NOT "
      "EXISTS (SELECT 1 FROM arcium_meta)"));
  version.BindInt(0, kCurrentVersion);
  if (!version.Run()) {
    *sqlite_error = db_.GetErrorCode();
    return false;
  }
  if (!transaction.Commit()) {
    // Unlike the branches above, this capture is NOT known to be accurate.
    // sql::Transaction::Commit() calls sql::Database::CommitTransaction(),
    // which — when the COMMIT statement itself fails with the transaction
    // still open — runs its own internal DoRollback() and returns false, all
    // before this call returns control to us (sql/database.cc:1424-1486). A
    // successful ROLLBACK there resets the connection's error code to
    // SQLITE_OK, so by the time we read it below, it has already been
    // laundered exactly like the destructor-driven rollback that motivated
    // capturing at all. There is no seam in the public
    // sql::Transaction/sql::Database API to observe the code between
    // commit.Run() failing and that internal rollback running — both happen
    // inside one opaque call. In practice this narrows to SQLITE_BUSY (the
    // COMMIT itself losing a lock race), which IsFileUnusable() treats as
    // non-catastrophic regardless of whether the real code or a laundered
    // SQLITE_OK is what we see — so the outcome (fail open, leave the file
    // alone) is correct even though this specific capture is not trustworthy.
    *sqlite_error = db_.GetErrorCode();
    return false;
  }
  return true;
}

void ArchiveStore::Add(const ArchivedTab& tab) {
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO archived_tabs(url, title, space_id, "
      "archived_at, folded_url, folded_title) VALUES (?, ?, ?, ?, ?, ?)"));
  statement.BindString(0, tab.url.spec());
  statement.BindString(1, base::UTF16ToUTF8(tab.title));
  statement.BindString(2, tab.space_id.value());
  statement.BindTime(3, tab.archived_at);
  statement.BindString(4, FoldToUtf8(base::UTF8ToUTF16(tab.url.spec())));
  statement.BindString(5, FoldToUtf8(tab.title));
  statement.Run();
}

std::vector<ArchivedTab> ArchiveStore::ListRecent(SpaceId space_id, int limit) {
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT url, title, space_id, archived_at FROM archived_tabs"
      " WHERE space_id = ? ORDER BY archived_at DESC LIMIT ?"));
  statement.BindString(0, space_id.value());
  statement.BindInt(1, limit);

  std::vector<ArchivedTab> tabs;
  while (statement.Step()) {
    ArchivedTab tab;
    tab.url = GURL(statement.ColumnString(0));
    tab.title = base::UTF8ToUTF16(statement.ColumnString(1));
    tab.space_id = SpaceId::FromString(statement.ColumnString(2));
    tab.archived_at = statement.ColumnTime(3);
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

std::vector<ArchivedTab> ArchiveStore::Search(const std::u16string& query,
                                              int limit) {
  // Matched against folded_title/folded_url rather than a bare LIKE on
  // title/url: SQLite's LIKE is already ASCII-case-insensitive on its own
  // (so lower() on an ASCII column would be redundant), but it leaves
  // non-ASCII text untouched — "ÖKONOMIE" vs "ökonomie" would not match.
  // FTS would be a schema to migrate to later if a plain scan gets too slow;
  // the archive is small enough that it isn't yet.
  const std::string needle = "%" + EscapeLikePattern(FoldToUtf8(query)) + "%";
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT url, title, space_id, archived_at FROM archived_tabs"
      " WHERE folded_title LIKE ? ESCAPE '\\' OR folded_url LIKE ? ESCAPE '\\'"
      " ORDER BY archived_at DESC LIMIT ?"));
  statement.BindString(0, needle);
  statement.BindString(1, needle);
  statement.BindInt(2, limit);

  std::vector<ArchivedTab> tabs;
  while (statement.Step()) {
    ArchivedTab tab;
    tab.url = GURL(statement.ColumnString(0));
    tab.title = base::UTF8ToUTF16(statement.ColumnString(1));
    tab.space_id = SpaceId::FromString(statement.ColumnString(2));
    tab.archived_at = statement.ColumnTime(3);
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

void ArchiveStore::Remove(const GURL& url, base::Time archived_at) {
  sql::Statement statement(db_.GetUniqueStatement(
      "DELETE FROM archived_tabs WHERE url = ? AND archived_at = ?"));
  statement.BindString(0, url.spec());
  statement.BindTime(1, archived_at);
  statement.Run();
}

}  // namespace arcium
