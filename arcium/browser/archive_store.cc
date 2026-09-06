// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/archive_store.h"

#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace arcium {

namespace {

// Registered in tools/metrics/histograms/metadata/sql/histograms.xml by
// patch 0110; sql::Database rejects an unregistered tag at compile time.
constexpr char kDatabaseTag[] = "ArciumArchive";

constexpr int kCurrentVersion = 1;

}  // namespace

// Exclusive locking (Chromium's default) has a second ArchiveStore opening
// the same file collide with one already open: the second connection cannot
// even take a shared lock to read the schema, InitSchema() sees that as a
// corrupt file, and the recovery path in Open() razes and recreates it. The
// archive is not performance sensitive enough to be worth that footgun.
ArchiveStore::ArchiveStore()
    : db_(sql::DatabaseOptions().set_exclusive_locking(false),
          sql::Database::Tag(kDatabaseTag)) {}

ArchiveStore::~ArchiveStore() = default;

bool ArchiveStore::Open(const base::FilePath& path) {
  if (!db_.Open(path) || !InitSchema()) {
    // Nothing in the archive is worth a recovery path, and an unreadable file
    // must not stop the browser opening.
    db_.Close();
    base::DeleteFile(path);
    return db_.Open(path) && InitSchema();
  }
  return true;
}

bool ArchiveStore::InitSchema() {
  static constexpr char kCreateTable[] =
      "CREATE TABLE IF NOT EXISTS archived_tabs("
      "  url TEXT NOT NULL,"
      "  title TEXT NOT NULL,"
      "  space_id TEXT NOT NULL,"
      "  archived_at INTEGER NOT NULL,"
      "  PRIMARY KEY (url, archived_at))";
  // Both indexes serve a query the UI actually makes: the archive list is
  // by space and recency, search is by recency across spaces.
  static constexpr char kCreateSpaceIndex[] =
      "CREATE INDEX IF NOT EXISTS idx_space_time"
      "  ON archived_tabs(space_id, archived_at DESC)";
  static constexpr char kCreateTimeIndex[] =
      "CREATE INDEX IF NOT EXISTS idx_time"
      "  ON archived_tabs(archived_at DESC)";
  static constexpr char kCreateMeta[] =
      "CREATE TABLE IF NOT EXISTS meta(version INTEGER NOT NULL)";

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
  sql::Statement version(
      db_.GetUniqueStatement("INSERT INTO meta(version) SELECT ? WHERE NOT "
                             "EXISTS (SELECT 1 FROM meta)"));
  version.BindInt(0, kCurrentVersion);
  if (!version.Run()) {
    return false;
  }
  return transaction.Commit();
}

void ArchiveStore::Add(const ArchivedTab& tab) {
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO archived_tabs(url, title, space_id, "
      "archived_at) VALUES (?, ?, ?, ?)"));
  statement.BindString(0, tab.url.spec());
  statement.BindString(1, base::UTF16ToUTF8(tab.title));
  statement.BindString(2, tab.space_id.value());
  statement.BindTime(3, tab.archived_at);
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
  // LIKE with a lowercased needle rather than FTS: the archive is small
  // enough that a scan is cheap, and FTS would be a schema to migrate later.
  const std::string needle =
      "%" + base::ToLowerASCII(base::UTF16ToUTF8(query)) + "%";
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT url, title, space_id, archived_at FROM archived_tabs"
      " WHERE lower(title) LIKE ? OR lower(url) LIKE ?"
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
