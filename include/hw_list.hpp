#pragma once

#include <vector>
#include <glibmm/ustring.h>
#include <sigc++/sigc++.h>
#include "db_handler.hpp"
#include <optional>

class Hardware {
friend class HW_List;
public:

    struct Settings {
        Glib::ustring name;
        Glib::ustring description;
        unsigned int antennas = 0;
        unsigned int sub_cars = 0;
    };

    void set_settings(const Settings& settings) {
        this->settings = settings;
        _update_signal.emit(*this);
    }

    uint32_t getDBId() const {
        return _idx;
    }

    const Settings& get_settings() const {
        return settings;
    }

    sigc::signal<void(Hardware&)> update_signal() const {
        return _update_signal;
    }

private:
    Hardware(size_t idx) :
        _idx(idx)
    {}

    size_t _idx = 0;
    sigc::signal<void(Hardware&)> _update_signal;
    Settings settings;

};

class HW_List {
public:

    static HW_List& get_instance() {
        static HW_List l;
        return l;
    }

    std::vector<Glib::ustring> get_names() const {
        std::vector<Glib::ustring> result;
        result.reserve(hardwares.size());
        for(const auto& i : hardwares) {
            result.push_back(i.settings.name);
        }
        return result;
    }

    Hardware& get_hardware(size_t idx) {
        return hardwares.at(idx);
    }

    Hardware& get_hardware_by_db_idx(int32_t idx) {
        return get_hardware(dbToLocalIdx[idx]);
    }

    void add_hardware(Hardware::Settings params = Hardware::Settings(), std::optional<size_t> db_idx = std::nullopt) {
        Hardware new_hw(hardwares.size());
        new_hw.set_settings(params);
        new_hw.update_signal().connect([this](Hardware& hw) {
            try {
                SQLite::Database& db = DB_Handler::get_db();
                SQLite::Statement query {
                    db,
                    R"(
                    UPDATE hardware
                    SET name = @name, description = @desc, antennas = @anten, subcarriers = @sub
                    WHERE id = @id
                    )"
                };
                Hardware::Settings cs = hw.get_settings();
                query.bind("@name", cs.name);
                query.bind("@desc", cs.description);
                query.bind("@anten", cs.antennas);
                query.bind("@sub", cs.sub_cars);
                query.bind("@id", static_cast<int64_t>(hw._idx));
                query.exec();
                _signal_update.emit();
            }
            catch(const SQLite::Exception& ex) {
                std::cerr << ex.what() << std::endl;
            }
        }
        );


        if(!db_idx) {
            SQLite::Database& db = DB_Handler::get_db();
            SQLite::Statement query {
                db,
                R"asdasdasd(
                INSERT INTO hardware (name, description, antennas, subcarriers)
                VALUES (@name, @desc, @anten, @sub)
                )asdasdasd"
            };
            query.bind("@name", params.name);
            query.bind("@desc", params.description);
            query.bind("@anten", params.antennas);
            query.bind("@sub", params.sub_cars);
            query.exec();

            db_idx = db.getLastInsertRowid();
        }
        new_hw._idx = *db_idx;
        hardwares.push_back(new_hw);
        dbToLocalIdx[new_hw._idx] = hardwares.size() - 1;

        _signal_update.emit();
    }

    void delete_hardware(size_t idx) {
        Hardware hw = hardwares.at(idx);
        hardwares.erase(hardwares.begin() + idx);
        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement query {
            db,
            "DELETE FROM hardware WHERE id = @id"
        };
        query.bind("@id", static_cast<uint32_t>(hw._idx));
        query.exec();
        _signal_update.emit();
    }

    sigc::signal<void()> signal_update() const {
        return _signal_update;
    }

private:
    HW_List() {
        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement query {
            db,
            "SELECT * FROM hardware"
        };
        while(query.executeStep()) {
            int64_t id          = query.getColumn(0);
            std::string name    = query.getColumn(1);
            std::string desc    = query.getColumn(2);
            int64_t antennas    = query.getColumn(3);
            int64_t subcarriers = query.getColumn(4);

            add_hardware(Hardware::Settings{name, desc, antennas, subcarriers}, id);
        }
    }

    sigc::signal<void()> _signal_update;
    std::vector<Hardware> hardwares;
    std::map<int32_t, size_t> dbToLocalIdx;

};

