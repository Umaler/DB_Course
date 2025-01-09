#pragma once
#include <filesystem>
#include <string>
#include <cstdio>
#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

extern "C" void pre_hook(
    void *pCtx,                   /* Copy of third arg to preupdate_hook() */
    int op,                       /* SQLITE_UPDATE, DELETE or INSERT */
    char const *zDb,              /* Database name */
    char const *zName,            /* Table name */
    sqlite3_int64 iKey1           /* Rowid of row about to be deleted/updated */
    );

class DB_Handler {
public:
    static constexpr const std::string database_path = "database.db";

    static SQLite::Database& get_db() {
        struct Opener {
            bool applySQL = false;
            SQLite::Database db;
            Opener() :
                applySQL(!std::filesystem::exists(std::filesystem::path{database_path})),
                db(database_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
            {
                db.exec("PRAGMA synchronous=OFF;");
                db.exec("PRAGMA count_changes=OFF;");
                db.exec("PRAGMA journal_mode=MEMORY;");
                db.exec("PRAGMA temp_store=MEMORY;");
                db.exec("PRAGMA foreign_keys = ON;");
                sqlite3_update_hook(db.getHandle(), pre_hook, NULL);
            }
        };
        static Opener opener;
        if(opener.applySQL) {
            const char* schema_sql =
            R"asdasd(
            BEGIN TRANSACTION;
            CREATE TABLE IF NOT EXISTS "image" (
                "id"	INTEGER NOT NULL,
                "experiment_id"	INTEGER NOT NULL,
                "image_path"	TEXT NOT NULL,
                "timestamp"	INTEGER NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "processed_measurement" (
                "id"	INTEGER NOT NULL,
                "id_measurement"	INTEGER NOT NULL,
                "amplitude"	REAL NOT NULL,
                "phase"	REAL NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT),
                FOREIGN KEY("id_measurement") REFERENCES "measurement"("id") ON DELETE CASCADE
            );
            CREATE TABLE IF NOT EXISTS "hardware" (
                "id"	INTEGER NOT NULL,
                "name"	TEXT NOT NULL,
                "description"	TEXT,
                "antennas"	INTEGER NOT NULL,
                "subcarriers"	INTEGER NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "experiment" (
                "id"	INTEGER NOT NULL,
                "name"	TEXT NOT NULL,
                "description"	TEXT,
                "hardware_tx_id"	INTEGER,
                "hardware_rx_id"	INTEGER,
                "recv_handler"	TEXT,
                "preproc_handler"	TEXT,
                "config"	TEXT,
                FOREIGN KEY("hardware_tx_id") REFERENCES "hardware"("id") ON DELETE SET NULL,
                PRIMARY KEY("id" AUTOINCREMENT),
                FOREIGN KEY("hardware_rx_id") REFERENCES "hardware"("id") ON DELETE SET NULL
            );
            CREATE TABLE IF NOT EXISTS "packet" (
                "id"	INTEGER NOT NULL,
                "marker"	TEXT,
                "timestamp"	INTEGER,
                "experiment_id"	INTEGER NOT NULL,
                FOREIGN KEY("experiment_id") REFERENCES "experiment"("id") ON DELETE CASCADE,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "measurement" (
                "id"	INTEGER NOT NULL,
                "id_packet"	INTEGER NOT NULL,
                "num_sub"	INTEGER NOT NULL,
                "rx"	INTEGER NOT NULL,
                "tx"	INTEGER NOT NULL,
                "real_part"	INTEGER NOT NULL,
                "imag_part"	INTEGER NOT NULL,
                FOREIGN KEY("id_packet") REFERENCES "packet"("id") ON DELETE CASCADE,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TRIGGER process_measurement
            AFTER INSERT
            ON measurement
            BEGIN
                INSERT INTO processed_measurement (id_measurement, amplitude, phase)
                VALUES (NEW.id, SQRT(NEW.real_part * NEW.real_part + NEW.imag_part * NEW.imag_part), atan2(NEW.imag_part, NEW.real_part));
            END;
            COMMIT;
            )asdasd";
            opener.db.exec(schema_sql);
            opener.applySQL = false;
        }
        return opener.db;
    }
};

extern "C" void pre_hook(
    void *pCtx,                   /* Copy of third arg to preupdate_hook() */
    int op,                       /* SQLITE_UPDATE, DELETE or INSERT */
    char const *zDb,              /* Database name */
    char const *zName,            /* Table name */
    sqlite3_int64 iKey1           /* Rowid of row about to be deleted/updated */
)
{
    if(op != SQLITE_DELETE || std::string(zName) != std::string("image"))
        return;

    SQLite::Database& db = DB_Handler::get_db();
    SQLite::Statement query(db, "SELECT image_path FROM image WHERE id=@id");
    query.bind("@id", static_cast<int64_t>(iKey1));
    if(!query.executeStep())
        return;
    std::string filePath = query.getColumn(0);
    std::remove(filePath.c_str());
}
