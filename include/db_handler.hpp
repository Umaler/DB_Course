#pragma once
#include <filesystem>
#include <string>
#include <SQLiteCpp/SQLiteCpp.h>

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
            }
        };
        static Opener opener;
        if(opener.applySQL) {
            const char* schema_sql =
            R"(
            BEGIN TRANSACTION;
            CREATE TABLE IF NOT EXISTS "user" (
                "id"    INTEGER NOT NULL,
                "name"    TEXT NOT NULL,
                "pc_name"    TEXT NOT NULL,
                "os_name"    TEXT NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "user_interaction" (
                "id"    INTEGER NOT NULL,
                "id_user"    INTEGER NOT NULL,
                "id_experiment"    INTEGER NOT NULL,
                "start_timestamp"    INTEGER NOT NULL,
                "end_timestamp"    INTEGER NOT NULL,
                FOREIGN KEY("id_user") REFERENCES "user"("id"),
                FOREIGN KEY("id_experiment") REFERENCES "experiment"("id") ON DELETE CASCADE,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "experiment" (
                "id"    INTEGER NOT NULL,
                "name"    TEXT NOT NULL,
                "description"    TEXT,
                "hardware_tx_id"    INTEGER,
                "hardware_rx_id"    INTEGER,
                "software_config_id"    INTEGER NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT),
                FOREIGN KEY("hardware_tx_id") REFERENCES "hardware"("id") ON DELETE SET NULL,
                FOREIGN KEY("hardware_rx_id") REFERENCES "hardware"("id") ON DELETE SET NULL
            );
            CREATE TABLE IF NOT EXISTS "image" (
                "id"    INTEGER NOT NULL,
                "experiment_id"    INTEGER NOT NULL,
                "image_path"    TEXT NOT NULL,
                "timestamp"    INTEGER NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "packet" (
                "id"    INTEGER NOT NULL,
                "marker"    TEXT,
                "timestamp"    INTEGER,
                "experiment_id"    INTEGER NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "measurement" (
                "id"    INTEGER NOT NULL,
                "id_packet"    INTEGER NOT NULL,
                "num_sub"    INTEGER NOT NULL,
                "rx"    INTEGER NOT NULL,
                "tx"    INTEGER NOT NULL,
                "real_part"    INTEGER NOT NULL,
                "imag_part"    INTEGER NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "processed_measurement" (
                "id"    INTEGER NOT NULL,
                "id_measurement"    INTEGER NOT NULL,
                "amplitude"    REAL NOT NULL,
                "phase"    REAL NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT),
                FOREIGN KEY("id_measurement") REFERENCES "measurement"("id") ON DELETE CASCADE
            );
            CREATE TABLE IF NOT EXISTS "hardware" (
                "id"    INTEGER NOT NULL,
                "name"    TEXT NOT NULL,
                "description"    TEXT,
                "antennas"    INTEGER NOT NULL,
                "subcarriers"    INTEGER NOT NULL,
                PRIMARY KEY("id" AUTOINCREMENT)
            );
            CREATE TABLE IF NOT EXISTS "software_config" (
                "id"    INTEGER NOT NULL,
                "input"    TEXT,
                "preprocessing"    TEXT,
                "plot_processing"    TEXT,
                "db_processing"    TEXT,
                "config"    TEXT,
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
            )";
            opener.db.exec(schema_sql);
            opener.applySQL = false;
        }
        return opener.db;
    }
};
