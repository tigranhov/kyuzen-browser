// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/archive_store.h"

#include <string_view>

#include "base/i18n/case_conversion.h"
#include "base/strings/utf_string_conversions.h"
#include "sql/error_delegate_util.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace arcium {

namespace {

// Registered in tools/metrics/histograms/metadata/sql/histograms.xml by
// patch 0110; sql::Database rejects an unregistered tag at compile time.
constexpr char kDatabaseTag[] = "ArciumArchive";

constexpr int kCurrentVersion = 1;

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

bool ArchiveStore::Open(const base::FilePath& path) {
  if (db_.Open(path) && InitSchema()) {
    return true;
  }
  if (!sql::IsErrorCatastrophic(db_.GetErrorCode())) {
    // A concurrent connection surfaces here as a bare SQLITE_BUSY or
    // SQLITE_LOCKED (sql::Database runs with a zero busy timeout), not
    // corruption. Never destroy the user's archive over lock contention:
    // fail open instead, so the window still opens with no archive for this
    // session (list and search simply come back empty).
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
    return db_.Raze() && InitSchema();
  }
  // db_.Open() itself failed outright (e.g. permission denied, disk full):
  // there is no handle to raze. Delete the file and any sidecars, then try
  // once more against a clean path.
  sql::Database::Delete(path);
  return db_.Open(path) && InitSchema();
}

bool ArchiveStore::InitSchema() {
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
    return false;
  }
  if (!db_.Execute(kCreateTable) || !db_.Execute(kCreateSpaceIndex) ||
      !db_.Execute(kCreateTimeIndex) || !db_.Execute(kCreateMeta)) {
    return false;
  }
  // Version row written on creation so a future migration has a floor to
  // read. Stage 2 only ever writes version 1.
  sql::Statement version(db_.GetUniqueStatement(
      "INSERT INTO arcium_meta(version) SELECT ? WHERE NOT "
      "EXISTS (SELECT 1 FROM arcium_meta)"));
  version.BindInt(0, kCurrentVersion);
  if (!version.Run()) {
    return false;
  }
  return transaction.Commit();
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
