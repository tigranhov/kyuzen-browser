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

  // Creates the file and schema if absent. A file that is not a usable
  // database is razed and recreated: an unreadable archive must not stop the
  // browser, and there is nothing in it worth a recovery path.
  bool Open(const base::FilePath& path);

  void Add(const ArchivedTab& tab);
  std::vector<ArchivedTab> ListRecent(SpaceId space_id, int limit);
  std::vector<ArchivedTab> Search(const std::u16string& query, int limit);
  void Remove(const GURL& url, base::Time archived_at);

 private:
  bool InitSchema();

  sql::Database db_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_ARCHIVE_STORE_H_
