// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/thumbnail_database.h"

#include <algorithm>
#include <string>

#include "base/bind.h"
#include "base/debug/alias.h"
#include "base/file_util.h"
#include "base/format_macros.h"
#include "base/memory/ref_counted_memory.h"
#include "base/metrics/histogram.h"
#include "base/rand_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/history/url_database.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/dump_without_crashing.h"
#include "sql/recovery.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "third_party/sqlite/sqlite3.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#endif

// Description of database tables:
//
// icon_mapping
//   id               Unique ID.
//   page_url         Page URL which has one or more associated favicons.
//   icon_id          The ID of favicon that this mapping maps to.
//
// favicons           This table associates a row to each favicon for a
//                    |page_url| in the |icon_mapping| table. This is the
//                    default favicon |page_url|/favicon.ico plus any favicons
//                    associated via <link rel="icon_type" href="url">.
//                    The |id| matches the |icon_id| field in the appropriate
//                    row in the icon_mapping table.
//
//   id               Unique ID.
//   url              The URL at which the favicon file is located.
//   icon_type        The type of the favicon specified in the rel attribute of
//                    the link tag. The FAVICON type is used for the default
//                    favicon.ico favicon.
//
// favicon_bitmaps    This table contains the PNG encoded bitmap data of the
//                    favicons. There is a separate row for every size in a
//                    multi resolution bitmap. The bitmap data is associated
//                    to the favicon via the |icon_id| field which matches
//                    the |id| field in the appropriate row in the |favicons|
//                    table.
//
//  id                Unique ID.
//  icon_id           The ID of the favicon that the bitmap is associated to.
//  last_updated      The time at which this favicon was inserted into the
//                    table. This is used to determine if it needs to be
//                    redownloaded from the web.
//  image_data        PNG encoded data of the favicon.
//  width             Pixel width of |image_data|.
//  height            Pixel height of |image_data|.

namespace {

// For this database, schema migrations are deprecated after two
// years.  This means that the oldest non-deprecated version should be
// two years old or greater (thus the migrations to get there are
// older).  Databases containing deprecated versions will be cleared
// at startup.  Since this database is a cache, losing old data is not
// fatal (in fact, very old data may be expired immediately at startup
// anyhow).

// Version 7: 911a634d/r209424 by qsr@chromium.org on 2013-07-01
// Version 6: 610f923b/r152367 by pkotwicz@chromium.org on 2012-08-20
// Version 5: e2ee8ae9/r105004 by groby@chromium.org on 2011-10-12
// Version 4: 5f104d76/r77288 by sky@chromium.org on 2011-03-08 (deprecated)
// Version 3: 09911bf3/r15 by initial.commit on 2008-07-26 (deprecated)

// Version number of the database.
// NOTE(shess): When changing the version, add a new golden file for
// the new version and a test to verify that Init() works with it.
const int kCurrentVersionNumber = 7;
const int kCompatibleVersionNumber = 7;
const int kDeprecatedVersionNumber = 4;  // and earlier.

void FillIconMapping(const sql::Statement& statement,
                     const GURL& page_url,
                     history::IconMapping* icon_mapping) {
  icon_mapping->mapping_id = statement.ColumnInt64(0);
  icon_mapping->icon_id = statement.ColumnInt64(1);
  icon_mapping->icon_type =
      static_cast<chrome::IconType>(statement.ColumnInt(2));
  icon_mapping->icon_url = GURL(statement.ColumnString(3));
  icon_mapping->page_url = page_url;
}

enum InvalidStructureType {
  // NOTE(shess): Intentionally skip bucket 0 to account for
  // conversion from a boolean histogram.
  STRUCTURE_EVENT_FAVICON = 1,
  STRUCTURE_EVENT_VERSION4,
  STRUCTURE_EVENT_VERSION5,

  // Always keep this at the end.
  STRUCTURE_EVENT_MAX,
};

void RecordInvalidStructure(InvalidStructureType invalid_type) {
  UMA_HISTOGRAM_ENUMERATION("History.InvalidFaviconsDBStructure",
                            invalid_type, STRUCTURE_EVENT_MAX);
}

// Attempt to pass 2000 bytes of |debug_info| into a crash dump.
void DumpWithoutCrashing2000(const std::string& debug_info) {
  char debug_buf[2000];
  base::strlcpy(debug_buf, debug_info.c_str(), arraysize(debug_buf));
  base::debug::Alias(&debug_buf);

  logging::DumpWithoutCrashing();
}

void ReportCorrupt(sql::Connection* db, size_t startup_kb) {
  // Buffer for accumulating debugging info about the error.  Place
  // more-relevant information earlier, in case things overflow the
  // fixed-size buffer.
  std::string debug_info;

  base::StringAppendF(&debug_info, "SQLITE_CORRUPT, integrity_check:\n");

  // Check files up to 8M to keep things from blocking too long.
  const size_t kMaxIntegrityCheckSize = 8192;
  if (startup_kb > kMaxIntegrityCheckSize) {
    base::StringAppendF(&debug_info, "too big %" PRIuS "\n", startup_kb);
  } else {
    std::vector<std::string> messages;

    const base::TimeTicks before = base::TimeTicks::Now();
    db->IntegrityCheck(&messages);
    base::StringAppendF(&debug_info, "# %" PRIx64 " ms, %" PRIuS " records\n",
                        (base::TimeTicks::Now() - before).InMilliseconds(),
                        messages.size());

    // SQLite returns up to 100 messages by default, trim deeper to
    // keep close to the 2000-character size limit for dumping.
    //
    // TODO(shess): If the first 20 tend to be actionable, test if
    // passing the count to integrity_check makes it exit earlier.  In
    // that case it may be possible to greatly ease the size
    // restriction.
    const size_t kMaxMessages = 20;
    for (size_t i = 0; i < kMaxMessages && i < messages.size(); ++i) {
      base::StringAppendF(&debug_info, "%s\n", messages[i].c_str());
    }
  }

  DumpWithoutCrashing2000(debug_info);
}

void ReportError(sql::Connection* db, int error) {
  // Buffer for accumulating debugging info about the error.  Place
  // more-relevant information earlier, in case things overflow the
  // fixed-size buffer.
  std::string debug_info;

  // The error message from the failed operation.
  base::StringAppendF(&debug_info, "db error: %d/%s\n",
                      db->GetErrorCode(), db->GetErrorMessage());

  // System errno information.
  base::StringAppendF(&debug_info, "errno: %d\n", db->GetLastErrno());

  // SQLITE_ERROR reports seem to be attempts to upgrade invalid
  // schema, try to log that info.
  if (error == SQLITE_ERROR) {
    const char* kVersionSql = "SELECT value FROM meta WHERE key = 'version'";
    if (db->IsSQLValid(kVersionSql)) {
      sql::Statement statement(db->GetUniqueStatement(kVersionSql));
      if (statement.Step()) {
        debug_info += "version: ";
        debug_info += statement.ColumnString(0);
        debug_info += '\n';
      } else if (statement.Succeeded()) {
        debug_info += "version: none\n";
      } else {
        debug_info += "version: error\n";
      }
    } else {
      debug_info += "version: invalid\n";
    }

    debug_info += "schema:\n";

    // sqlite_master has columns:
    //   type - "index" or "table".
    //   name - name of created element.
    //   tbl_name - name of element, or target table in case of index.
    //   rootpage - root page of the element in database file.
    //   sql - SQL to create the element.
    // In general, the |sql| column is sufficient to derive the other
    // columns.  |rootpage| is not interesting for debugging, without
    // the contents of the database.  The COALESCE is because certain
    // automatic elements will have a |name| but no |sql|,
    const char* kSchemaSql = "SELECT COALESCE(sql, name) FROM sqlite_master";
    sql::Statement statement(db->GetUniqueStatement(kSchemaSql));
    while (statement.Step()) {
      debug_info += statement.ColumnString(0);
      debug_info += '\n';
    }
    if (!statement.Succeeded())
      debug_info += "error\n";
  }

  // TODO(shess): Think of other things to log.  Not logging the
  // statement text because the backtrace should suffice in most
  // cases.  The database schema is a possibility, but the
  // likelihood of recursive error callbacks makes that risky (same
  // reasoning applies to other data fetched from the database).

  DumpWithoutCrashing2000(debug_info);
}

// TODO(shess): If this proves out, perhaps lift the code out to
// chrome/browser/diagnostics/sqlite_diagnostics.{h,cc}.
void GenerateDiagnostics(sql::Connection* db,
                         size_t startup_kb,
                         int extended_error) {
  int error = (extended_error & 0xFF);

  // Infrequently report information about the error up to the crash
  // server.
  static const uint64 kReportsPerMillion = 50000;

  // Since some/most errors will not resolve themselves, only report
  // once per Chrome run.
  static bool reported = false;
  if (reported)
    return;

  uint64 rand = base::RandGenerator(1000000);
  if (error == SQLITE_CORRUPT) {
    // Once the database is known to be corrupt, it will generate a
    // stream of errors until someone fixes it, so give one chance.
    // Set first in case of errors in generating the report.
    reported = true;

    // Corrupt cases currently dominate, report them very infrequently.
    static const uint64 kCorruptReportsPerMillion = 10000;
    if (rand < kCorruptReportsPerMillion)
      ReportCorrupt(db, startup_kb);
  } else if (error == SQLITE_READONLY) {
    // SQLITE_READONLY appears similar to SQLITE_CORRUPT - once it
    // is seen, it is almost guaranteed to be seen again.
    reported = true;

    if (rand < kReportsPerMillion)
      ReportError(db, extended_error);
  } else {
    // Only set the flag when making a report.  This should allow
    // later (potentially different) errors in a stream of errors to
    // be reported.
    //
    // TODO(shess): Would it be worthwile to audit for which cases
    // want once-only handling?  Sqlite.Error.Thumbnail shows
    // CORRUPT and READONLY as almost 95% of all reports on these
    // channels, so probably easier to just harvest from the field.
    if (rand < kReportsPerMillion) {
      reported = true;
      ReportError(db, extended_error);
    }
  }
}

bool InitTables(sql::Connection* db) {
  const char kIconMappingSql[] =
      "CREATE TABLE IF NOT EXISTS icon_mapping"
      "("
      "id INTEGER PRIMARY KEY,"
      "page_url LONGVARCHAR NOT NULL,"
      "icon_id INTEGER"
      ")";
  if (!db->Execute(kIconMappingSql))
    return false;

  const char kFaviconsSql[] =
      "CREATE TABLE IF NOT EXISTS favicons"
      "("
      "id INTEGER PRIMARY KEY,"
      "url LONGVARCHAR NOT NULL,"
      // default icon_type FAVICON to be consistent with past migration.
      "icon_type INTEGER DEFAULT 1"
      ")";
  if (!db->Execute(kFaviconsSql))
    return false;

  const char kFaviconBitmapsSql[] =
      "CREATE TABLE IF NOT EXISTS favicon_bitmaps"
      "("
      "id INTEGER PRIMARY KEY,"
      "icon_id INTEGER NOT NULL,"
      "last_updated INTEGER DEFAULT 0,"
      "image_data BLOB,"
      "width INTEGER DEFAULT 0,"
      "height INTEGER DEFAULT 0"
      ")";
  if (!db->Execute(kFaviconBitmapsSql))
    return false;

  return true;
}

bool InitIndices(sql::Connection* db) {
  const char kIconMappingUrlIndexSql[] =
      "CREATE INDEX IF NOT EXISTS icon_mapping_page_url_idx"
      " ON icon_mapping(page_url)";
  const char kIconMappingIdIndexSql[] =
      "CREATE INDEX IF NOT EXISTS icon_mapping_icon_id_idx"
      " ON icon_mapping(icon_id)";
  if (!db->Execute(kIconMappingUrlIndexSql) ||
      !db->Execute(kIconMappingIdIndexSql)) {
    return false;
  }

  const char kFaviconsIndexSql[] =
      "CREATE INDEX IF NOT EXISTS favicons_url ON favicons(url)";
  if (!db->Execute(kFaviconsIndexSql))
    return false;

  const char kFaviconBitmapsIndexSql[] =
      "CREATE INDEX IF NOT EXISTS favicon_bitmaps_icon_id ON "
      "favicon_bitmaps(icon_id)";
  if (!db->Execute(kFaviconBitmapsIndexSql))
    return false;

  return true;
}

enum RecoveryEventType {
  RECOVERY_EVENT_RECOVERED = 0,
  RECOVERY_EVENT_FAILED_SCOPER,
  RECOVERY_EVENT_FAILED_META_VERSION_ERROR,
  RECOVERY_EVENT_FAILED_META_VERSION_NONE,
  RECOVERY_EVENT_FAILED_META_WRONG_VERSION6,  // obsolete
  RECOVERY_EVENT_FAILED_META_WRONG_VERSION5,
  RECOVERY_EVENT_FAILED_META_WRONG_VERSION,
  RECOVERY_EVENT_FAILED_RECOVER_META,
  RECOVERY_EVENT_FAILED_META_INSERT,  // obsolete
  RECOVERY_EVENT_FAILED_INIT,
  RECOVERY_EVENT_FAILED_RECOVER_FAVICONS,
  RECOVERY_EVENT_FAILED_FAVICONS_INSERT,
  RECOVERY_EVENT_FAILED_RECOVER_FAVICON_BITMAPS,
  RECOVERY_EVENT_FAILED_FAVICON_BITMAPS_INSERT,
  RECOVERY_EVENT_FAILED_RECOVER_ICON_MAPPING,
  RECOVERY_EVENT_FAILED_ICON_MAPPING_INSERT,
  RECOVERY_EVENT_RECOVERED_VERSION6,
  RECOVERY_EVENT_FAILED_META_INIT,

  // Always keep this at the end.
  RECOVERY_EVENT_MAX,
};

void RecordRecoveryEvent(RecoveryEventType recovery_event) {
  UMA_HISTOGRAM_ENUMERATION("History.FaviconsRecovery",
                            recovery_event, RECOVERY_EVENT_MAX);
}

// Recover the database to the extent possible, razing it if recovery
// is not possible.
// TODO(shess): This is mostly just a safe proof of concept.  In the
// real world, this database is probably not worthwhile recovering, as
// opposed to just razing it and starting over whenever corruption is
// detected.  So this database is a good test subject.
void RecoverDatabaseOrRaze(sql::Connection* db, const base::FilePath& db_path) {
  // NOTE(shess): This code is currently specific to the version
  // number.  I am working on simplifying things to loosen the
  // dependency, meanwhile contact me if you need to bump the version.
  DCHECK_EQ(7, kCurrentVersionNumber);

  // TODO(shess): Reset back after?
  db->reset_error_callback();

  // For histogram purposes.
  size_t favicons_rows_recovered = 0;
  size_t favicon_bitmaps_rows_recovered = 0;
  size_t icon_mapping_rows_recovered = 0;
  int64 original_size = 0;
  file_util::GetFileSize(db_path, &original_size);

  scoped_ptr<sql::Recovery> recovery = sql::Recovery::Begin(db, db_path);
  if (!recovery) {
    // TODO(shess): Unable to create recovery connection.  This
    // implies something substantial is wrong.  At this point |db| has
    // been poisoned so there is nothing really to do.
    //
    // Possible responses are unclear.  If the failure relates to a
    // problem somehow specific to the temporary file used to back the
    // database, then an in-memory database could possibly be used.
    // This could potentially allow recovering the main database, and
    // might be simple to implement w/in Begin().
    RecordRecoveryEvent(RECOVERY_EVENT_FAILED_SCOPER);
    return;
  }

  // Setup the meta recovery table, and check that the version number
  // is covered by the recovery code.
  // TODO(shess): sql::Recovery should provide a helper to handle meta.
  int version = 0;  // For reporting which version was recovered.
  {
    const char kRecoverySql[] =
        "CREATE VIRTUAL TABLE temp.recover_meta USING recover"
        "("
        "corrupt.meta,"
        "key TEXT NOT NULL,"
        "value TEXT"  // Really?  Never int?
        ")";
    if (!recovery->db()->Execute(kRecoverySql)) {
      // TODO(shess): Failure to create the recover_meta table could
      // mean that the main database is too corrupt to access, or that
      // the meta table doesn't exist.
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_RECOVER_META);
      return;
    }

    {
      const char kRecoveryVersionSql[] =
          "SELECT value FROM recover_meta WHERE key = 'version'";
      sql::Statement recovery_version(
          recovery->db()->GetUniqueStatement(kRecoveryVersionSql));
      if (!recovery_version.Step()) {
        if (!recovery_version.Succeeded()) {
          RecordRecoveryEvent(RECOVERY_EVENT_FAILED_META_VERSION_ERROR);
          // TODO(shess): An error while processing the statement is
          // probably not recoverable.
        } else {
          RecordRecoveryEvent(RECOVERY_EVENT_FAILED_META_VERSION_NONE);
          // TODO(shess): If a positive version lock cannot be achieved,
          // the database could still be recovered by optimistically
          // attempting to copy things.  In the limit, the schema found
          // could be inspected.  Less clear is whether optimistic
          // recovery really makes sense.
        }
        recovery_version.Clear();
        sql::Recovery::Rollback(recovery.Pass());
        return;
      }
      version = recovery_version.ColumnInt(0);

      // Recovery code is generally schema-dependent.  Version 7 and
      // version 6 are very similar, so can be handled together.
      // Track version 5, to see whether it's worth writing recovery
      // code for.
      if (version != 7 && version != 6) {
        if (version == 5) {
          RecordRecoveryEvent(RECOVERY_EVENT_FAILED_META_WRONG_VERSION5);
        } else {
          RecordRecoveryEvent(RECOVERY_EVENT_FAILED_META_WRONG_VERSION);
        }
        recovery_version.Clear();
        sql::Recovery::Rollback(recovery.Pass());
        return;
      }
    }

    // Either version 6 or version 7 recovers to current.
    sql::MetaTable recover_meta_table;
    if (!recover_meta_table.Init(recovery->db(), kCurrentVersionNumber,
                                 kCompatibleVersionNumber)) {
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_META_INIT);
      return;
    }
  }

  // Create a fresh version of the database.  The recovery code uses
  // conflict-resolution to handle duplicates, so the indices are
  // necessary.
  if (!InitTables(recovery->db()) || !InitIndices(recovery->db())) {
    // TODO(shess): Unable to create the new schema in the new
    // database.  The new database should be a temporary file, so
    // being unable to work with it is pretty unclear.
    //
    // What are the potential responses, even?  The recovery database
    // could be opened as in-memory.  If the temp database had a
    // filesystem problem and the temp filesystem differs from the
    // main database, then that could fix it.
    sql::Recovery::Rollback(recovery.Pass());
    RecordRecoveryEvent(RECOVERY_EVENT_FAILED_INIT);
    return;
  }

  // Setup favicons table.
  {
    // Version 6 had the |sizes| column, version 7 removed it.  The
    // recover virtual table treats more columns than expected as an
    // error, but if _fewer_ columns are present, they can be treated
    // as NULL.  SQLite requires this because ALTER TABLE adds columns
    // to the schema, but not to the actual table storage.
    const char kRecoverySql[] =
        "CREATE VIRTUAL TABLE temp.recover_favicons USING recover"
        "("
        "corrupt.favicons,"
        "id ROWID,"
        "url TEXT NOT NULL,"
        "icon_type INTEGER,"
        "sizes TEXT"
        ")";
    if (!recovery->db()->Execute(kRecoverySql)) {
      // TODO(shess): Failure to create the recovery table probably
      // means unrecoverable.
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_RECOVER_FAVICONS);
      return;
    }

    // TODO(shess): Check if the DEFAULT 1 will just cover the
    // COALESCE().  Either way, the new code has a literal 1 rather
    // than a NULL, right?
    const char kCopySql[] =
        "INSERT OR REPLACE INTO main.favicons "
        "SELECT id, url, COALESCE(icon_type, 1) FROM recover_favicons";
    if (!recovery->db()->Execute(kCopySql)) {
      // TODO(shess): The recover_favicons table should mask problems
      // with the source file, so this implies failure to write to the
      // recovery database.
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_FAVICONS_INSERT);
      return;
    }
    favicons_rows_recovered = recovery->db()->GetLastChangeCount();
  }

  // Setup favicons_bitmaps table.
  {
    const char kRecoverySql[] =
        "CREATE VIRTUAL TABLE temp.recover_favicons_bitmaps USING recover"
        "("
        "corrupt.favicon_bitmaps,"
        "id ROWID,"
        "icon_id INTEGER STRICT NOT NULL,"
        "last_updated INTEGER,"
        "image_data BLOB,"
        "width INTEGER,"
        "height INTEGER"
        ")";
    if (!recovery->db()->Execute(kRecoverySql)) {
      // TODO(shess): Failure to create the recovery table probably
      // means unrecoverable.
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_RECOVER_FAVICON_BITMAPS);
      return;
    }

    const char kCopySql[] =
        "INSERT OR REPLACE INTO main.favicon_bitmaps "
        "SELECT id, icon_id, COALESCE(last_updated, 0), image_data, "
        " COALESCE(width, 0), COALESCE(height, 0) "
        "FROM recover_favicons_bitmaps";
    if (!recovery->db()->Execute(kCopySql)) {
      // TODO(shess): The recover_faviconbitmaps table should mask
      // problems with the source file, so this implies failure to
      // write to the recovery database.
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_FAVICON_BITMAPS_INSERT);
      return;
    }
    favicon_bitmaps_rows_recovered = recovery->db()->GetLastChangeCount();
  }

  // Setup icon_mapping table.
  {
    const char kRecoverySql[] =
        "CREATE VIRTUAL TABLE temp.recover_icon_mapping USING recover"
        "("
        "corrupt.icon_mapping,"
        "id ROWID,"
        "page_url TEXT STRICT NOT NULL,"
        "icon_id INTEGER STRICT"
        ")";
    if (!recovery->db()->Execute(kRecoverySql)) {
      // TODO(shess): Failure to create the recovery table probably
      // means unrecoverable.
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_RECOVER_ICON_MAPPING);
      return;
    }

    const char kCopySql[] =
        "INSERT OR REPLACE INTO main.icon_mapping "
        "SELECT id, page_url, icon_id FROM recover_icon_mapping";
    if (!recovery->db()->Execute(kCopySql)) {
      // TODO(shess): The recover_icon_mapping table should mask
      // problems with the source file, so this implies failure to
      // write to the recovery database.
      sql::Recovery::Rollback(recovery.Pass());
      RecordRecoveryEvent(RECOVERY_EVENT_FAILED_ICON_MAPPING_INSERT);
      return;
    }
    icon_mapping_rows_recovered = recovery->db()->GetLastChangeCount();
  }

  // TODO(shess): Is it possible/likely to have broken foreign-key
  // issues with the tables?
  // - icon_mapping.icon_id maps to no favicons.id
  // - favicon_bitmaps.icon_id maps to no favicons.id
  // - favicons.id is referenced by no icon_mapping.icon_id
  // - favicons.id is referenced by no favicon_bitmaps.icon_id
  // This step is possibly not worth the effort necessary to develop
  // and sequence the statements, as it is basically a form of garbage
  // collection.

  ignore_result(sql::Recovery::Recovered(recovery.Pass()));

  // Track the size of the recovered database relative to the size of
  // the input database.  The size should almost always be smaller,
  // unless the input database was empty to start with.  If the
  // percentage results are very low, something is awry.
  int64 final_size = 0;
  if (original_size > 0 &&
      file_util::GetFileSize(db_path, &final_size) &&
      final_size > 0) {
    int percentage = static_cast<int>(original_size * 100 / final_size);
    UMA_HISTOGRAM_PERCENTAGE("History.FaviconsRecoveredPercentage",
                             std::max(100, percentage));
  }

  // Using 10,000 because these cases mostly care about "none
  // recovered" and "lots recovered".  More than 10,000 rows recovered
  // probably means there's something wrong with the profile.
  UMA_HISTOGRAM_COUNTS_10000("History.FaviconsRecoveredRowsFavicons",
                             favicons_rows_recovered);
  UMA_HISTOGRAM_COUNTS_10000("History.FaviconsRecoveredRowsFaviconBitmaps",
                             favicon_bitmaps_rows_recovered);
  UMA_HISTOGRAM_COUNTS_10000("History.FaviconsRecoveredRowsIconMapping",
                             icon_mapping_rows_recovered);

  if (version == 6) {
    RecordRecoveryEvent(RECOVERY_EVENT_RECOVERED_VERSION6);
  } else {
    RecordRecoveryEvent(RECOVERY_EVENT_RECOVERED);
  }
}

void DatabaseErrorCallback(sql::Connection* db,
                           const base::FilePath& db_path,
                           size_t startup_kb,
                           int extended_error,
                           sql::Statement* stmt) {
  // TODO(shess): Assert that this is running on a safe thread.
  // AFAICT, should be the history thread, but at this level I can't
  // see how to reach that.

  // TODO(shess): For now, don't report on beta or stable so as not to
  // overwhelm the crash server.  Once the big fish are fried,
  // consider reporting at a reduced rate on the bigger channels.
  chrome::VersionInfo::Channel channel = chrome::VersionInfo::GetChannel();
  if (channel != chrome::VersionInfo::CHANNEL_STABLE &&
      channel != chrome::VersionInfo::CHANNEL_BETA) {
    GenerateDiagnostics(db, startup_kb, extended_error);
  }

  // Attempt to recover corrupt databases.
  int error = (extended_error & 0xFF);
  if (error == SQLITE_CORRUPT ||
      error == SQLITE_CANTOPEN ||
      error == SQLITE_NOTADB) {
    RecoverDatabaseOrRaze(db, db_path);
  }

  // The default handling is to assert on debug and to ignore on release.
  if (!sql::Connection::ShouldIgnoreSqliteError(extended_error))
    DLOG(FATAL) << db->GetErrorMessage();
}

}  // namespace

namespace history {

ThumbnailDatabase::IconMappingEnumerator::IconMappingEnumerator() {
}

ThumbnailDatabase::IconMappingEnumerator::~IconMappingEnumerator() {
}

bool ThumbnailDatabase::IconMappingEnumerator::GetNextIconMapping(
    IconMapping* icon_mapping) {
  if (!statement_.Step())
    return false;
  FillIconMapping(statement_, GURL(statement_.ColumnString(4)), icon_mapping);
  return true;
}

ThumbnailDatabase::ThumbnailDatabase() {
}

ThumbnailDatabase::~ThumbnailDatabase() {
  // The DBCloseScoper will delete the DB and the cache.
}

sql::InitStatus ThumbnailDatabase::Init(const base::FilePath& db_name) {
  // TODO(shess): Consider separating database open from schema setup.
  // With that change, this code could Raze() from outside the
  // transaction, rather than needing RazeAndClose() in InitImpl().

  // Retry failed setup in case the recovery system fixed things.
  const size_t kAttempts = 2;

  sql::InitStatus status = sql::INIT_FAILURE;
  for (size_t i = 0; i < kAttempts; ++i) {
    status = InitImpl(db_name);
    if (status == sql::INIT_OK)
      return status;

    meta_table_.Reset();
    db_.Close();
  }
  return status;
}

void ThumbnailDatabase::ComputeDatabaseMetrics() {
  sql::Statement favicon_count(
      db_.GetCachedStatement(SQL_FROM_HERE, "SELECT COUNT(*) FROM favicons"));
  UMA_HISTOGRAM_COUNTS_10000(
      "History.NumFaviconsInDB",
      favicon_count.Step() ? favicon_count.ColumnInt(0) : 0);
}

void ThumbnailDatabase::BeginTransaction() {
  db_.BeginTransaction();
}

void ThumbnailDatabase::CommitTransaction() {
  db_.CommitTransaction();
}

void ThumbnailDatabase::RollbackTransaction() {
  db_.RollbackTransaction();
}

void ThumbnailDatabase::Vacuum() {
  DCHECK(db_.transaction_nesting() == 0) <<
      "Can not have a transaction when vacuuming.";
  ignore_result(db_.Execute("VACUUM"));
}

void ThumbnailDatabase::TrimMemory(bool aggressively) {
  db_.TrimMemory(aggressively);
}

bool ThumbnailDatabase::GetFaviconBitmapIDSizes(
    chrome::FaviconID icon_id,
    std::vector<FaviconBitmapIDSize>* bitmap_id_sizes) {
  DCHECK(icon_id);
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT id, width, height FROM favicon_bitmaps WHERE icon_id=?"));
  statement.BindInt64(0, icon_id);

  bool result = false;
  while (statement.Step()) {
    result = true;
    if (!bitmap_id_sizes)
      return result;

    FaviconBitmapIDSize bitmap_id_size;
    bitmap_id_size.bitmap_id = statement.ColumnInt64(0);
    bitmap_id_size.pixel_size = gfx::Size(statement.ColumnInt(1),
                                          statement.ColumnInt(2));
    bitmap_id_sizes->push_back(bitmap_id_size);
  }
  return result;
}

bool ThumbnailDatabase::GetFaviconBitmaps(
    chrome::FaviconID icon_id,
    std::vector<FaviconBitmap>* favicon_bitmaps) {
  DCHECK(icon_id);
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT id, last_updated, image_data, width, height FROM favicon_bitmaps "
      "WHERE icon_id=?"));
  statement.BindInt64(0, icon_id);

  bool result = false;
  while (statement.Step()) {
    result = true;
    if (!favicon_bitmaps)
      return result;

    FaviconBitmap favicon_bitmap;
    favicon_bitmap.bitmap_id = statement.ColumnInt64(0);
    favicon_bitmap.icon_id = icon_id;
    favicon_bitmap.last_updated =
        base::Time::FromInternalValue(statement.ColumnInt64(1));
    if (statement.ColumnByteLength(2) > 0) {
      scoped_refptr<base::RefCountedBytes> data(new base::RefCountedBytes());
      statement.ColumnBlobAsVector(2, &data->data());
      favicon_bitmap.bitmap_data = data;
    }
    favicon_bitmap.pixel_size = gfx::Size(statement.ColumnInt(3),
                                          statement.ColumnInt(4));
    favicon_bitmaps->push_back(favicon_bitmap);
  }
  return result;
}

bool ThumbnailDatabase::GetFaviconBitmap(
    FaviconBitmapID bitmap_id,
    base::Time* last_updated,
    scoped_refptr<base::RefCountedMemory>* png_icon_data,
    gfx::Size* pixel_size) {
  DCHECK(bitmap_id);
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT last_updated, image_data, width, height FROM favicon_bitmaps "
      "WHERE id=?"));
  statement.BindInt64(0, bitmap_id);

  if (!statement.Step())
    return false;

  if (last_updated)
    *last_updated = base::Time::FromInternalValue(statement.ColumnInt64(0));

  if (png_icon_data && statement.ColumnByteLength(1) > 0) {
    scoped_refptr<base::RefCountedBytes> data(new base::RefCountedBytes());
    statement.ColumnBlobAsVector(1, &data->data());
    *png_icon_data = data;
  }

  if (pixel_size) {
    *pixel_size = gfx::Size(statement.ColumnInt(2),
                            statement.ColumnInt(3));
  }
  return true;
}

FaviconBitmapID ThumbnailDatabase::AddFaviconBitmap(
    chrome::FaviconID icon_id,
    const scoped_refptr<base::RefCountedMemory>& icon_data,
    base::Time time,
    const gfx::Size& pixel_size) {
  DCHECK(icon_id);
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO favicon_bitmaps (icon_id, image_data, last_updated, width, "
      "height) VALUES (?, ?, ?, ?, ?)"));
  statement.BindInt64(0, icon_id);
  if (icon_data.get() && icon_data->size()) {
    statement.BindBlob(1, icon_data->front(),
                       static_cast<int>(icon_data->size()));
  } else {
    statement.BindNull(1);
  }
  statement.BindInt64(2, time.ToInternalValue());
  statement.BindInt(3, pixel_size.width());
  statement.BindInt(4, pixel_size.height());

  if (!statement.Run())
    return 0;
  return db_.GetLastInsertRowId();
}

bool ThumbnailDatabase::SetFaviconBitmap(
    FaviconBitmapID bitmap_id,
    scoped_refptr<base::RefCountedMemory> bitmap_data,
    base::Time time) {
  DCHECK(bitmap_id);
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "UPDATE favicon_bitmaps SET image_data=?, last_updated=? WHERE id=?"));
  if (bitmap_data.get() && bitmap_data->size()) {
    statement.BindBlob(0, bitmap_data->front(),
                       static_cast<int>(bitmap_data->size()));
  } else {
    statement.BindNull(0);
  }
  statement.BindInt64(1, time.ToInternalValue());
  statement.BindInt64(2, bitmap_id);

  return statement.Run();
}

bool ThumbnailDatabase::SetFaviconBitmapLastUpdateTime(
    FaviconBitmapID bitmap_id,
    base::Time time) {
  DCHECK(bitmap_id);
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "UPDATE favicon_bitmaps SET last_updated=? WHERE id=?"));
  statement.BindInt64(0, time.ToInternalValue());
  statement.BindInt64(1, bitmap_id);
  return statement.Run();
}

bool ThumbnailDatabase::DeleteFaviconBitmap(FaviconBitmapID bitmap_id) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM favicon_bitmaps WHERE id=?"));
  statement.BindInt64(0, bitmap_id);
  return statement.Run();
}

bool ThumbnailDatabase::SetFaviconOutOfDate(chrome::FaviconID icon_id) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "UPDATE favicon_bitmaps SET last_updated=? WHERE icon_id=?"));
  statement.BindInt64(0, 0);
  statement.BindInt64(1, icon_id);

  return statement.Run();
}

chrome::FaviconID ThumbnailDatabase::GetFaviconIDForFaviconURL(
    const GURL& icon_url,
    int required_icon_type,
    chrome::IconType* icon_type) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT id, icon_type FROM favicons WHERE url=? AND (icon_type & ? > 0) "
      "ORDER BY icon_type DESC"));
  statement.BindString(0, URLDatabase::GURLToDatabaseURL(icon_url));
  statement.BindInt(1, required_icon_type);

  if (!statement.Step())
    return 0;  // not cached

  if (icon_type)
    *icon_type = static_cast<chrome::IconType>(statement.ColumnInt(1));
  return statement.ColumnInt64(0);
}

bool ThumbnailDatabase::GetFaviconHeader(chrome::FaviconID icon_id,
                                         GURL* icon_url,
                                         chrome::IconType* icon_type) {
  DCHECK(icon_id);

  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT url, icon_type FROM favicons WHERE id=?"));
  statement.BindInt64(0, icon_id);

  if (!statement.Step())
    return false;  // No entry for the id.

  if (icon_url)
    *icon_url = GURL(statement.ColumnString(0));
  if (icon_type)
    *icon_type = static_cast<chrome::IconType>(statement.ColumnInt(1));

  return true;
}

chrome::FaviconID ThumbnailDatabase::AddFavicon(
    const GURL& icon_url,
    chrome::IconType icon_type) {

  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO favicons (url, icon_type) VALUES (?, ?)"));
  statement.BindString(0, URLDatabase::GURLToDatabaseURL(icon_url));
  statement.BindInt(1, icon_type);

  if (!statement.Run())
    return 0;
  return db_.GetLastInsertRowId();
}

chrome::FaviconID ThumbnailDatabase::AddFavicon(
    const GURL& icon_url,
    chrome::IconType icon_type,
    const scoped_refptr<base::RefCountedMemory>& icon_data,
    base::Time time,
    const gfx::Size& pixel_size) {
  chrome::FaviconID icon_id = AddFavicon(icon_url, icon_type);
  if (!icon_id || !AddFaviconBitmap(icon_id, icon_data, time, pixel_size))
    return 0;

  return icon_id;
}

bool ThumbnailDatabase::DeleteFavicon(chrome::FaviconID id) {
  sql::Statement statement;
  statement.Assign(db_.GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM favicons WHERE id = ?"));
  statement.BindInt64(0, id);
  if (!statement.Run())
    return false;

  statement.Assign(db_.GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM favicon_bitmaps WHERE icon_id = ?"));
  statement.BindInt64(0, id);
  return statement.Run();
}

bool ThumbnailDatabase::GetIconMappingsForPageURL(
    const GURL& page_url,
    int required_icon_types,
    std::vector<IconMapping>* filtered_mapping_data) {
  std::vector<IconMapping> mapping_data;
  if (!GetIconMappingsForPageURL(page_url, &mapping_data))
    return false;

  bool result = false;
  for (std::vector<IconMapping>::iterator m = mapping_data.begin();
       m != mapping_data.end(); ++m) {
    if (m->icon_type & required_icon_types) {
      result = true;
      if (!filtered_mapping_data)
        return result;

      // Restrict icon type of subsequent matches to |m->icon_type|.
      // |m->icon_type| is the largest IconType in |mapping_data| because
      // |mapping_data| is sorted in descending order of IconType.
      required_icon_types = m->icon_type;

      filtered_mapping_data->push_back(*m);
    }
  }
  return result;
}

bool ThumbnailDatabase::GetIconMappingsForPageURL(
    const GURL& page_url,
    std::vector<IconMapping>* mapping_data) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT icon_mapping.id, icon_mapping.icon_id, favicons.icon_type, "
      "favicons.url "
      "FROM icon_mapping "
      "INNER JOIN favicons "
      "ON icon_mapping.icon_id = favicons.id "
      "WHERE icon_mapping.page_url=? "
      "ORDER BY favicons.icon_type DESC"));
  statement.BindString(0, URLDatabase::GURLToDatabaseURL(page_url));

  bool result = false;
  while (statement.Step()) {
    result = true;
    if (!mapping_data)
      return result;

    IconMapping icon_mapping;
    FillIconMapping(statement, page_url, &icon_mapping);
    mapping_data->push_back(icon_mapping);
  }
  return result;
}

IconMappingID ThumbnailDatabase::AddIconMapping(const GURL& page_url,
                                                chrome::FaviconID icon_id) {
  const char kSql[] =
      "INSERT INTO icon_mapping (page_url, icon_id) VALUES (?, ?)";
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE, kSql));
  statement.BindString(0, URLDatabase::GURLToDatabaseURL(page_url));
  statement.BindInt64(1, icon_id);

  if (!statement.Run())
    return 0;

  return db_.GetLastInsertRowId();
}

bool ThumbnailDatabase::UpdateIconMapping(IconMappingID mapping_id,
                                          chrome::FaviconID icon_id) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "UPDATE icon_mapping SET icon_id=? WHERE id=?"));
  statement.BindInt64(0, icon_id);
  statement.BindInt64(1, mapping_id);

  return statement.Run();
}

bool ThumbnailDatabase::DeleteIconMappings(const GURL& page_url) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM icon_mapping WHERE page_url = ?"));
  statement.BindString(0, URLDatabase::GURLToDatabaseURL(page_url));

  return statement.Run();
}

bool ThumbnailDatabase::DeleteIconMapping(IconMappingID mapping_id) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM icon_mapping WHERE id=?"));
  statement.BindInt64(0, mapping_id);

  return statement.Run();
}

bool ThumbnailDatabase::HasMappingFor(chrome::FaviconID id) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT id FROM icon_mapping "
      "WHERE icon_id=?"));
  statement.BindInt64(0, id);

  return statement.Step();
}

bool ThumbnailDatabase::CloneIconMappings(const GURL& old_page_url,
                                          const GURL& new_page_url) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT icon_id FROM icon_mapping "
      "WHERE page_url=?"));
  if (!statement.is_valid())
    return false;

  // Do nothing if there are existing bindings
  statement.BindString(0, URLDatabase::GURLToDatabaseURL(new_page_url));
  if (statement.Step())
    return true;

  statement.Assign(db_.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO icon_mapping (page_url, icon_id) "
        "SELECT ?, icon_id FROM icon_mapping "
        "WHERE page_url = ?"));

  statement.BindString(0, URLDatabase::GURLToDatabaseURL(new_page_url));
  statement.BindString(1, URLDatabase::GURLToDatabaseURL(old_page_url));
  return statement.Run();
}

bool ThumbnailDatabase::InitIconMappingEnumerator(
    chrome::IconType type,
    IconMappingEnumerator* enumerator) {
  DCHECK(!enumerator->statement_.is_valid());
  enumerator->statement_.Assign(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT icon_mapping.id, icon_mapping.icon_id, favicons.icon_type, "
             "favicons.url, icon_mapping.page_url "
         "FROM icon_mapping JOIN favicons ON ("
              "icon_mapping.icon_id = favicons.id) "
         "WHERE favicons.icon_type = ?"));
  enumerator->statement_.BindInt(0, type);
  return enumerator->statement_.is_valid();
}

bool ThumbnailDatabase::RetainDataForPageUrls(
    const std::vector<GURL>& urls_to_keep) {
  sql::Transaction transaction(&db_);
  if (!transaction.Begin())
    return false;

  // temp.icon_id_mapping generates new icon ids as consecutive
  // integers starting from 1, and maps them to the old icon ids.
  {
    const char kIconMappingCreate[] =
        "CREATE TEMP TABLE icon_id_mapping "
        "("
        "new_icon_id INTEGER PRIMARY KEY,"
        "old_icon_id INTEGER NOT NULL UNIQUE"
        ")";
    if (!db_.Execute(kIconMappingCreate))
      return false;

    // Insert the icon ids for retained urls, skipping duplicates.
    const char kIconMappingSql[] =
        "INSERT OR IGNORE INTO temp.icon_id_mapping (old_icon_id) "
        "SELECT icon_id FROM icon_mapping WHERE page_url = ?";
    sql::Statement statement(db_.GetUniqueStatement(kIconMappingSql));
    for (std::vector<GURL>::const_iterator
             i = urls_to_keep.begin(); i != urls_to_keep.end(); ++i) {
      statement.BindString(0, URLDatabase::GURLToDatabaseURL(*i));
      if (!statement.Run())
        return false;
      statement.Reset(true);
    }
  }

  const char kRenameIconMappingTable[] =
      "ALTER TABLE icon_mapping RENAME TO old_icon_mapping";
  const char kCopyIconMapping[] =
      "INSERT INTO icon_mapping (page_url, icon_id) "
      "SELECT old.page_url, mapping.new_icon_id "
      "FROM old_icon_mapping AS old "
      "JOIN temp.icon_id_mapping AS mapping "
      "ON (old.icon_id = mapping.old_icon_id)";
  const char kDropOldIconMappingTable[] = "DROP TABLE old_icon_mapping";

  const char kRenameFaviconsTable[] =
      "ALTER TABLE favicons RENAME TO old_favicons";
  const char kCopyFavicons[] =
      "INSERT INTO favicons (id, url, icon_type) "
      "SELECT mapping.new_icon_id, old.url, old.icon_type "
      "FROM old_favicons AS old "
      "JOIN temp.icon_id_mapping AS mapping "
      "ON (old.id = mapping.old_icon_id)";
  const char kDropOldFaviconsTable[] = "DROP TABLE old_favicons";

  const char kRenameFaviconBitmapsTable[] =
      "ALTER TABLE favicon_bitmaps RENAME TO old_favicon_bitmaps";
  const char kCopyFaviconBitmaps[] =
      "INSERT INTO favicon_bitmaps "
      "  (icon_id, last_updated, image_data, width, height) "
      "SELECT mapping.new_icon_id, old.last_updated, "
      "    old.image_data, old.width, old.height "
      "FROM old_favicon_bitmaps AS old "
      "JOIN temp.icon_id_mapping AS mapping "
      "ON (old.icon_id = mapping.old_icon_id)";
  const char kDropOldFaviconBitmapsTable[] =
      "DROP TABLE old_favicon_bitmaps";

  // Rename existing tables to new location.
  if (!db_.Execute(kRenameIconMappingTable) ||
      !db_.Execute(kRenameFaviconsTable) ||
      !db_.Execute(kRenameFaviconBitmapsTable)) {
    return false;
  }

  // Initialize the replacement tables.  At this point the old indices
  // still exist (pointing to the old_* tables), so do not initialize
  // the indices.
  if (!InitTables(&db_))
    return false;

  // Copy all of the data over.
  if (!db_.Execute(kCopyIconMapping) ||
      !db_.Execute(kCopyFavicons) ||
      !db_.Execute(kCopyFaviconBitmaps)) {
    return false;
  }

  // Drop the old_* tables, which also drops the indices.
  if (!db_.Execute(kDropOldIconMappingTable) ||
      !db_.Execute(kDropOldFaviconsTable) ||
      !db_.Execute(kDropOldFaviconBitmapsTable)) {
    return false;
  }

  // Recreate the indices.
  // TODO(shess): UNIQUE indices could fail due to duplication.  This
  // could happen in case of corruption.
  if (!InitIndices(&db_))
    return false;

  const char kIconMappingDrop[] = "DROP TABLE temp.icon_id_mapping";
  if (!db_.Execute(kIconMappingDrop))
    return false;

  return transaction.Commit();
}

sql::InitStatus ThumbnailDatabase::OpenDatabase(sql::Connection* db,
                                                const base::FilePath& db_name) {
  size_t startup_kb = 0;
  int64 size_64;
  if (file_util::GetFileSize(db_name, &size_64))
    startup_kb = static_cast<size_t>(size_64 / 1024);

  db->set_histogram_tag("Thumbnail");
  db->set_error_callback(base::Bind(&DatabaseErrorCallback,
                                    db, db_name, startup_kb));

  // Thumbnails db now only stores favicons, so we don't need that big a page
  // size or cache.
  db->set_page_size(2048);
  db->set_cache_size(32);

  // Run the database in exclusive mode. Nobody else should be accessing the
  // database while we're running, and this will give somewhat improved perf.
  db->set_exclusive_locking();

  if (!db->Open(db_name))
    return sql::INIT_FAILURE;

  return sql::INIT_OK;
}

sql::InitStatus ThumbnailDatabase::InitImpl(const base::FilePath& db_name) {
  sql::InitStatus status = OpenDatabase(&db_, db_name);
  if (status != sql::INIT_OK)
    return status;

  // Clear databases which are too old to process.
  DCHECK_LT(kDeprecatedVersionNumber, kCurrentVersionNumber);
  sql::MetaTable::RazeIfDeprecated(&db_, kDeprecatedVersionNumber);

  // TODO(shess): Sqlite.Version.Thumbnail shows versions 22, 23, and
  // 25.  Future versions are not destroyed because that could lead to
  // data loss if the profile is opened by a later channel, but
  // perhaps a heuristic like >kCurrentVersionNumber+3 could be used.

  // Scope initialization in a transaction so we can't be partially initialized.
  sql::Transaction transaction(&db_);
  if (!transaction.Begin())
    return sql::INIT_FAILURE;

  // TODO(shess): Failing Begin() implies that something serious is
  // wrong with the database.  Raze() may be in order.

#if defined(OS_MACOSX)
  // Exclude the thumbnails file from backups.
  base::mac::SetFileBackupExclusion(db_name);
#endif

  // thumbnails table has been obsolete for a long time, remove any
  // detrious.
  ignore_result(db_.Execute("DROP TABLE IF EXISTS thumbnails"));

  // At some point, operations involving temporary tables weren't done
  // atomically and users have been stranded.  Drop those tables and
  // move on.
  // TODO(shess): Prove it?  Audit all cases and see if it's possible
  // that this implies non-atomic update, and should thus be handled
  // via the corruption handler.
  ignore_result(db_.Execute("DROP TABLE IF EXISTS temp_favicons"));
  ignore_result(db_.Execute("DROP TABLE IF EXISTS temp_favicon_bitmaps"));
  ignore_result(db_.Execute("DROP TABLE IF EXISTS temp_icon_mapping"));

  // Create the tables.
  if (!meta_table_.Init(&db_, kCurrentVersionNumber,
                        kCompatibleVersionNumber) ||
      !InitTables(&db_) ||
      !InitIndices(&db_)) {
    return sql::INIT_FAILURE;
  }

  // Version check. We should not encounter a database too old for us to handle
  // in the wild, so we try to continue in that case.
  if (meta_table_.GetCompatibleVersionNumber() > kCurrentVersionNumber) {
    LOG(WARNING) << "Thumbnail database is too new.";
    return sql::INIT_TOO_NEW;
  }

  int cur_version = meta_table_.GetVersionNumber();

  if (!db_.DoesColumnExist("favicons", "icon_type")) {
    LOG(ERROR) << "Raze because of missing favicon.icon_type";
    RecordInvalidStructure(STRUCTURE_EVENT_VERSION4);

    db_.RazeAndClose();
    return sql::INIT_FAILURE;
  }

  if (cur_version < 7 && !db_.DoesColumnExist("favicons", "sizes")) {
    LOG(ERROR) << "Raze because of missing favicon.sizes";
    RecordInvalidStructure(STRUCTURE_EVENT_VERSION5);

    db_.RazeAndClose();
    return sql::INIT_FAILURE;
  }

  if (cur_version == 5) {
    ++cur_version;
    if (!UpgradeToVersion6())
      return CantUpgradeToVersion(cur_version);
  }

  if (cur_version == 6) {
    ++cur_version;
    if (!UpgradeToVersion7())
      return CantUpgradeToVersion(cur_version);
  }

  LOG_IF(WARNING, cur_version < kCurrentVersionNumber) <<
      "Thumbnail database version " << cur_version << " is too old to handle.";

  // Initialization is complete.
  if (!transaction.Commit())
    return sql::INIT_FAILURE;

  // Raze the database if the structure of the favicons database is not what
  // it should be. This error cannot be detected via the SQL error code because
  // the error code for running SQL statements against a database with missing
  // columns is SQLITE_ERROR which is not unique enough to act upon.
  // TODO(pkotwicz): Revisit this in M27 and see if the razing can be removed.
  // (crbug.com/166453)
  if (IsFaviconDBStructureIncorrect()) {
    LOG(ERROR) << "Raze because of invalid favicon db structure.";
    RecordInvalidStructure(STRUCTURE_EVENT_FAVICON);

    db_.RazeAndClose();
    return sql::INIT_FAILURE;
  }

  return sql::INIT_OK;
}

sql::InitStatus ThumbnailDatabase::CantUpgradeToVersion(int cur_version) {
  LOG(WARNING) << "Unable to update to thumbnail database to version " <<
               cur_version << ".";
  db_.Close();
  return sql::INIT_FAILURE;
}

bool ThumbnailDatabase::UpgradeToVersion6() {
  // Move bitmap data from favicons to favicon_bitmaps.
  bool success =
      db_.Execute("INSERT INTO favicon_bitmaps (icon_id, last_updated, "
                  "image_data, width, height)"
                  "SELECT id, last_updated, image_data, 0, 0 FROM favicons") &&
      db_.Execute("CREATE TABLE temp_favicons ("
                  "id INTEGER PRIMARY KEY,"
                  "url LONGVARCHAR NOT NULL,"
                  "icon_type INTEGER DEFAULT 1,"
                  // default icon_type FAVICON to be consistent with
                  // past migration.
                  "sizes LONGVARCHAR)") &&
      db_.Execute("INSERT INTO temp_favicons (id, url, icon_type) "
                  "SELECT id, url, icon_type FROM favicons") &&
      db_.Execute("DROP TABLE favicons") &&
      db_.Execute("ALTER TABLE temp_favicons RENAME TO favicons");
  // NOTE(shess): v7 will re-create the index.
  if (!success)
    return false;

  meta_table_.SetVersionNumber(6);
  meta_table_.SetCompatibleVersionNumber(std::min(6, kCompatibleVersionNumber));
  return true;
}

bool ThumbnailDatabase::UpgradeToVersion7() {
  // Sizes column was never used, remove it.
  bool success =
      db_.Execute("CREATE TABLE temp_favicons ("
                  "id INTEGER PRIMARY KEY,"
                  "url LONGVARCHAR NOT NULL,"
                  // default icon_type FAVICON to be consistent with
                  // past migration.
                  "icon_type INTEGER DEFAULT 1)") &&
      db_.Execute("INSERT INTO temp_favicons (id, url, icon_type) "
                  "SELECT id, url, icon_type FROM favicons") &&
      db_.Execute("DROP TABLE favicons") &&
      db_.Execute("ALTER TABLE temp_favicons RENAME TO favicons") &&
      db_.Execute("CREATE INDEX IF NOT EXISTS favicons_url ON favicons(url)");

  if (!success)
    return false;

  meta_table_.SetVersionNumber(7);
  meta_table_.SetCompatibleVersionNumber(std::min(7, kCompatibleVersionNumber));
  return true;
}

bool ThumbnailDatabase::IsFaviconDBStructureIncorrect() {
  return !db_.IsSQLValid("SELECT id, url, icon_type FROM favicons");
}

}  // namespace history
