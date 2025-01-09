#pragma once
#include "db_handler.hpp"
#include "handlers_list.hpp"
#include "hw_list.hpp"
#include <map>
#include <vector>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>
#include <sigc++/sigc++.h>
#include <cstdio>
#include <opencv2/opencv.hpp>
#include <fstream>
#include "marker_manager.hpp"

class ExperimentsList;

struct FullExperimentConfig {
    std::string name;
    std::string description;
    std::optional<std::reference_wrapper<Hardware>> receiver;
    std::optional<std::reference_wrapper<Hardware>> transmitter;
    std::optional<std::reference_wrapper<ReceiverHandler>> recvHandler;
    std::optional<std::reference_wrapper<PreprocessingHandler>> preprocHandler;
    nlohmann::json config;
};

class Experiment {
friend ExperimentsList;
public:

    const std::string& getName() const {
        return name;
    }

    void setName(std::string_view newName) {
        name = {newName.begin(), newName.end()};
        _updateSignal.emit(*this);
        _updateNameSignal.emit(*this);
    }

    const std::string& getDescription() const {
        return desc;
    }

    void setDescription(std::string_view newDesc) {
        desc = {newDesc.begin(), newDesc.end()};
        _updateSignal.emit(*this);
    }

    std::optional<std::reference_wrapper<Hardware>> getReceiver() {
        return receiver;
    }

    std::optional<std::reference_wrapper<Hardware>> getTransmitter() {
        return transmitter;
    }

    std::optional<std::reference_wrapper<ReceiverHandler>> getReceiverHandler() {
        return recvHandler;
    }

    std::optional<std::reference_wrapper<PreprocessingHandler>> getPreprocessor() {
        return preprocHandler;
    }

    nlohmann::json getConfig() {
        return userConfig;
    }

    void setConfig(nlohmann::json newConfig) {
        userConfig = newConfig;
        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement query(db, "UPDATE experiment SET config = @config WHERE id = @id");
        query.bind("@config", userConfig.dump());
        query.bind("@id", getDBIndex());
        query.exec();

        if(recvHandler)
            static_cast<ReceiverHandler&>(*recvHandler).set_settings(userConfig);
        if(preprocHandler)
            static_cast<PreprocessingHandler&>(*preprocHandler).set_settings(userConfig);
    }

    sigc::signal<void(Experiment&)> updateSignal() const {
        return _updateSignal;
    }

    sigc::signal<void(Experiment&)> updateNameSignal() const {
        return _updateNameSignal;
    }

    uint32_t addPoint(const HandlerBase::datatype& data) {
        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement packQuery(db, R"asdasd(
            INSERT INTO packet (marker, timestamp, experiment_id)
            VALUES (@marker, unixepoch(), @exp_id)
        )asdasd");
        packQuery.bind("@marker", MarkerManager::getInstance().getMarker());
        packQuery.bind("@exp_id", getDBIndex());
        packQuery.exec();
        uint32_t packIdx = db.getLastInsertRowid();

        SQLite::Statement measQuery(db, R"asdasd(
            INSERT INTO measurement (id_packet, num_sub, rx, tx, real_part, imag_part)
            VALUES (@packIdx, @subcar, @rx, @tx, @real, @imag)
        )asdasd");
        measQuery.bind("@packIdx", packIdx);
        for(uint32_t rx = 0; rx < data.first.size(); rx++) {
            measQuery.bind("@rx", rx);
            for(uint32_t tx = 0; tx < data.first[rx].size(); tx++) {
                measQuery.bind("@tx", tx);
                for(uint32_t subcar = 0; subcar < data.first[rx][tx].size(); subcar++) {
                    measQuery.bind("@subcar", subcar);
                    measQuery.bind("@real", data.first[rx][tx][subcar]);
                    measQuery.bind("@imag", data.second[rx][tx][subcar]);
                    measQuery.exec();
                    measQuery.reset();
                }
            }
        }

        return packIdx;
    }

    std::vector<double> getPoints(uint32_t rx, uint32_t tx, uint32_t num_sub, bool ampl) const {
        std::vector<double> result;

        SQLite::Database& db = DB_Handler::get_db();

        SQLite::Statement infoQuery(db, R"asdasd(
            SELECT COUNT(packet.id)
            FROM measurement
            JOIN packet ON measurement.id_packet = packet.id
            WHERE packet.experiment_id = @exp_id
        )asdasd");
        infoQuery.bind("@exp_id", getDBIndex());
        if(!infoQuery.executeStep()) {
            std::cerr << "Something wrong on infoQuery in Experiment::getPoints" << std::endl;
            return result;
        }
        uint32_t packets_c = infoQuery.getColumn(0);
        result.reserve(packets_c);

        std::string queryStr = ampl ? "SELECT amplitude\n" : "SELECT phase\n";
        queryStr += R"asdasd(
            FROM processed_measurement
            INNER JOIN measurement ON id_measurement = measurement.id
            INNER JOIN packet ON measurement.id_packet = packet.id
            WHERE packet.experiment_id = @exp_id AND
                  measurement.rx = @rx AND
                  measurement.tx = @tx AND
                  measurement.num_sub = @num_sub
            ORDER BY packet.timestamp
        )asdasd";
        SQLite::Statement query(db, queryStr);
        query.bind("@exp_id", getDBIndex());
        query.bind("@rx", rx);
        query.bind("@tx", tx);
        query.bind("@num_sub", num_sub);
        while(query.executeStep()) {
            double val = query.getColumn(0);
            result.push_back(val);
        }
        return result;
    }

    uint32_t getPacketsCount() {
        SQLite::Database& db = DB_Handler::get_db();

        SQLite::Statement query(db, "SELECT COUNT(1) FROM packet WHERE experiment_id = @exp_id");
        query.bind("@exp_id", getDBIndex());
        if(!query.executeStep())
            return 0;
        else
            return query.getColumn(0).getUInt();
    }

    uint32_t getPhotosCount() {
        SQLite::Database& db = DB_Handler::get_db();

        SQLite::Statement query(db, "SELECT COUNT(1) FROM image WHERE experiment_id = @exp_id");
        query.bind("@exp_id", getDBIndex());
        if(!query.executeStep())
            return 0;
        else
            return query.getColumn(0).getUInt();
    }

    int32_t getDBIndex() const {
        return dbIdx;
    }

    void addPhoto(cv::Mat frame) {
        const auto p1 = std::chrono::system_clock::now();
        uint32_t time = std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count();

        std::string filename = MarkerManager::getInstance().getMarker() + "_" + std::to_string(time) + ".jpg";

        cv::imwrite(std::string("images/") + filename, frame);

        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement query(db, "INSERT INTO image (experiment_id, image_path, timestamp) VALUES (@expId, @path, @time)");
        query.bind("@expId", getDBIndex());
        query.bind("@path", filename);
        query.bind("@time", time);
        query.exec();
    }

    struct ExportFilters {
        bool ampl = false;
        bool phase = false;
        bool imag = false;
        bool real = false;
        bool image = false;
        std::optional<std::string> marker;
    };

    void exportData(std::string pathStr, ExportFilters filters, std::function<void(double)> progress_callback) {
        if(!filters.marker)
            filters.marker = "%";

        if(pathStr.back() != '/' && pathStr.back() != '\\')
            pathStr.push_back('/');

        std::filesystem::path path(pathStr);
        std::filesystem::create_directories(path);
        std::filesystem::create_directories(path / "photos");

        SQLite::Database& db = DB_Handler::get_db();

        SQLite::Statement tx_rx_query(db, R"asd(
            SELECT MAX(tx), MAX(rx), COUNT(measurement.id) FROM measurement
            INNER JOIN packet ON id_packet = packet.id
            INNER JOIN experiment ON experiment.id = packet.experiment_id
            WHERE experiment.id = @exp_id AND packet.marker LIKE @marker
        )asd");
        tx_rx_query.bind("@exp_id", getDBIndex());
        tx_rx_query.bind("@marker", *filters.marker);
        if(!tx_rx_query.executeStep())
            return;
        uint32_t tx_c = tx_rx_query.getColumn(0);
        uint32_t rx_c = tx_rx_query.getColumn(1);
        uint32_t packs_c = tx_rx_query.getColumn(2);

        auto getStreams = [tx_c, rx_c, path](std::string name) {
            uint32_t _tx_c = tx_c + 1;
            uint32_t _rx_c = rx_c + 1;
            std::vector<std::vector<std::ofstream>> result;
            for(uint32_t i = 0; i < _tx_c; i++) {
                result.emplace_back(_rx_c);
                for(uint32_t j = 0; j < _rx_c; j++) {
                    std::string filename = std::to_string(i) + "_" + std::to_string(j) + ".json";
                    std::filesystem::path filepath = path / name;
                    std::filesystem::create_directories(filepath);
                    result[i][j].open((filepath / filename).c_str());
                    result[i][j] << "[";
                }
            }
            return std::move(result);
        };

        std::vector<std::vector<std::ofstream>> amplStreams = filters.ampl ? getStreams("ampls") : std::vector<std::vector<std::ofstream>>{};
        std::vector<std::vector<std::ofstream>> phaseStreams = filters.phase ? getStreams("phase") : std::vector<std::vector<std::ofstream>>{};
        std::vector<std::vector<std::ofstream>> imagStreams = filters.real ? getStreams("imag") : std::vector<std::vector<std::ofstream>>{};
        std::vector<std::vector<std::ofstream>> realStreams = filters.imag ? getStreams("real") : std::vector<std::vector<std::ofstream>>{};

        SQLite::Statement dataQuery(db, R"asd(
            SELECT measurement.num_sub, measurement.rx, measurement.tx, amplitude, phase, measurement.real_part, measurement.imag_part, packet.id
            FROM processed_measurement
            INNER JOIN measurement ON processed_measurement.id_measurement = measurement.id
            INNER JOIN packet ON measurement.id_packet = packet.id
            INNER JOIN experiment ON packet.experiment_id = experiment.id
            WHERE experiment.id = @exp_id AND packet.marker LIKE @marker
            ORDER BY packet.id, measurement.num_sub
        )asd");
        dataQuery.bind("@exp_id", getDBIndex());
        dataQuery.bind("@marker", *filters.marker);

        const size_t guiStep = 10;
        int64_t last_packet = -1;
        size_t passed_packs = 0;
        bool firstTime = true;
        while(dataQuery.executeStep()) {
            uint32_t sub_car = dataQuery.getColumn(0);
            uint32_t rx = dataQuery.getColumn(1);
            uint32_t tx = dataQuery.getColumn(2);
            double ampl = dataQuery.getColumn(3);
            double phas = dataQuery.getColumn(4);
            uint32_t real = dataQuery.getColumn(5);
            uint32_t imag = dataQuery.getColumn(6);
            uint32_t pack_id = dataQuery.getColumn(7);

            bool newPack = last_packet != pack_id;
            last_packet = pack_id;

            auto outData = [newPack, firstTime, tx, rx](auto& stream, auto val) {
                if(newPack) {
                    if(!firstTime)
                        stream[tx][rx] << "]";
                    stream[tx][rx] << "," << std::endl << "[" << val;
                }
                else {
                    stream[tx][rx] << ", " << val;
                }
            };

            if(filters.ampl) outData(amplStreams, ampl);
            if(filters.phase) outData(phaseStreams, phas);
            if(filters.real) outData(realStreams, real);
            if(filters.imag) outData(imagStreams, imag);

            if(passed_packs % guiStep == 0) {
                try {
                    if(progress_callback)
                        progress_callback(static_cast<double>(passed_packs) / static_cast<double>(packs_c) / 2.0);
                    Glib::MainContext::get_default()->iteration(false);
                }
                catch(...){}
            }

            firstTime = false;
            passed_packs++;
        }

        SQLite::Statement imageCountQuery(db, R"asd(
            SELECT COUNT(image.id) FROM image
            WHERE image.experiment_id = @exp_id
        )asd");
        imageCountQuery.bind("@exp_id", getDBIndex());
        if(!imageCountQuery.executeStep())
            return;
        uint32_t images_c = imageCountQuery.getColumn(0);

        SQLite::Statement imageQuery(db, R"asd(
            SELECT image_path FROM image
            WHERE image.experiment_id = @exp_id
        )asd");
        imageQuery.bind("@exp_id", getDBIndex());
        size_t images_past = 0;
        const size_t imagesGuiStep = 5;
        while(imageQuery.executeStep()) {
            std::string imagePath = imageQuery.getColumn(0);

            try {
                std::filesystem::copy(std::string("./images/") + imagePath, path / (std::string("photos/") + imagePath));
            }
            catch(...){}

            if(images_past % imagesGuiStep == 0) {
                try {
                    if(progress_callback)
                        progress_callback(static_cast<double>(images_past) / static_cast<double>(imagesGuiStep) / 2.0 + 0.5);
                    Glib::MainContext::get_default()->iteration(false);
                }
                catch(...){}
            }
            images_past++;
        }

        auto closeBrackets = [](auto& stream) {
            for(auto& i : stream) {
                for(auto& j : i) {
                    j << "]" << std::endl << "]";
                    j.close();
                }
            }
        };
        if(filters.ampl) closeBrackets(amplStreams);
        if(filters.phase) closeBrackets(phaseStreams);
        if(filters.real) closeBrackets(realStreams);
        if(filters.imag) closeBrackets(imagStreams);
    }

    Experiment(Experiment&&) = default;

private:
    Experiment() = default;
    Experiment(FullExperimentConfig conf) {
        name = conf.name;
        desc = conf.description;
        receiver = conf.receiver;
        transmitter = conf.transmitter;
        recvHandler = conf.recvHandler;
        preprocHandler = conf.preprocHandler;
    }

    sigc::signal<void(Experiment&)> _updateSignal;
    sigc::signal<void(Experiment&)> _updateNameSignal;

    std::string name;
    std::string desc;
    std::optional<std::reference_wrapper<Hardware>> receiver;
    std::optional<std::reference_wrapper<Hardware>> transmitter;
    std::optional<std::reference_wrapper<ReceiverHandler>> recvHandler;
    std::optional<std::reference_wrapper<PreprocessingHandler>> preprocHandler;
    nlohmann::json userConfig;

    int32_t dbIdx = -1;
};

class ExperimentsList {
public:

    struct Filter {
        std::optional<uint64_t> fromDate = std::nullopt;
        std::optional<uint64_t> upToDate = std::nullopt;
        std::optional<int32_t> recvIdx   = std::nullopt;
        std::optional<int32_t> transIdx  = std::nullopt;
    };

    static ExperimentsList& getInstance() {
        static ExperimentsList inst;
        return inst;
    }

    std::vector<Glib::ustring> getExperimentsNames() const {
        std::vector<Glib::ustring> result;
        result.reserve(experiments.size());
        for(const auto& i : experiments) {
            result.push_back(i.getName());
        }
        return result;
    }

    Experiment& getExperimentByIdx(size_t idx) {
        return experiments.at(idx);
    }

    std::vector<Experiment>& getExperiments() {
        return experiments;
    }

    void deleteExperiment(const Experiment& exp) {
        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement imagesQuery(db, "SELECT image_path FROM image WHERE experiment_id=@id");
        imagesQuery.bind("@id", exp.dbIdx);
        while(imagesQuery.executeStep()) {
            if(!std::remove(imagesQuery.getColumn(0).getText()))
                std::cerr << "Failed to delete file " << imagesQuery.getColumn(0).getText() << std::endl;
        }

        SQLite::Statement query(db, "DELETE FROM experiment WHERE id=@id");
        query.bind("@id", exp.dbIdx);
        query.exec();
        updateList(lastUsedFilter);
    }

    void deleteExperiment(size_t idx) {
        deleteExperiment(experiments.at(idx));
    }

    void addExperiment(FullExperimentConfig expConf) {
        Experiment exp(expConf);

        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement query(db, R"asd(
            INSERT INTO experiment (name, description, hardware_tx_id, hardware_rx_id, recv_handler, preproc_handler, config)
            VALUES (@name, @desc, @tx_id, @rx_id, @recv_hand, @preproc_hand, @config)
        )asd");
        query.bind("@name", expConf.name);
        query.bind("@desc", expConf.description);
        if(expConf.transmitter)
            query.bind("@tx_id", expConf.transmitter->get().getDBId());
        else
            query.bind("@tx_id");

        if(expConf.receiver)
            query.bind("@rx_id", expConf.receiver->get().getDBId());
        else
            query.bind("@rx_id");

        if(expConf.recvHandler)
            query.bind("@recv_hand", expConf.recvHandler->get().getName());
        else
            query.bind("@recv_hand");

        if(expConf.preprocHandler)
            query.bind("@preproc_hand", expConf.preprocHandler->get().getName());
        else
            query.bind("@preproc_hand");

        query.bind("@config", expConf.config.dump());
        query.exec();
        exp.dbIdx = db.getLastInsertRowid();
        experiments.emplace_back(std::move(exp));

        _updateSignal.emit();
    }

    void updateList(Filter filter) {
        experiments.clear();
        std::string requestStr = "SELECT id, name, description, hardware_tx_id, hardware_rx_id, config, recv_handler, preproc_handler FROM experiment";
        std::vector<std::string> whatToDo;

        if(filter.fromDate)
            whatToDo.push_back("\n id IN (SELECT experiment.id FROM experiment JOIN packet ON packet.experiment_id = experiment.id WHERE timestamp > @min_time)");
        if(filter.upToDate)
            whatToDo.push_back("\n id IN (SELECT experiment.id FROM experiment JOIN packet ON packet.experiment_id = experiment.id WHERE timestamp < @max_time)");
        if(filter.transIdx)
            whatToDo.push_back("\n hardware_tx_id = @transIdx");
        if(filter.recvIdx)
            whatToDo.push_back("\n hardware_rx_id = @recvIdx");

        if(whatToDo.size() > 0) {
            requestStr += "\nWHERE";
            for(size_t i = 0; i < whatToDo.size() - 1; i++) {
                requestStr += whatToDo[i];
                requestStr += " AND ";
            }
            requestStr += whatToDo.back();
        }

        SQLite::Database& db = DB_Handler::get_db();
        SQLite::Statement query(db, requestStr);

        if(filter.fromDate) query.bind("@min_time", static_cast<uint32_t>(*filter.fromDate));
        if(filter.upToDate) query.bind("@max_time", static_cast<uint32_t>(*filter.upToDate));
        if(filter.transIdx) query.bind("@transIdx", static_cast<uint32_t>(*filter.transIdx));
        if(filter.recvIdx)  query.bind("@recvIdx",  static_cast<uint32_t>(*filter.recvIdx));

        HW_List& hw_list = HW_List::get_instance();

        experiments.clear();
        while(query.executeStep()) {
            Experiment exp;
            exp.dbIdx = query.getColumn(0);
            exp.name = query.getColumn(1).getString();
            exp.desc = query.getColumn(2).isNull() ? "" : query.getColumn(2).getString();
            if(!query.getColumn(3).isNull())
                exp.receiver = hw_list.get_hardware_by_db_idx(query.getColumn(3));
            if(!query.getColumn(4).isNull())
                exp.transmitter = hw_list.get_hardware_by_db_idx(query.getColumn(4));
            std::string configStr = query.getColumn(5).isNull() ? "" : query.getColumn(5).getString();
            try {
                exp.userConfig = nlohmann::json::parse(configStr);
            }
            catch(const nlohmann::json::exception& ex) {}
            std::string recv_hand_name = query.getColumn(6).isNull() ? "" : query.getColumn(6).getString();
            try {
                exp.recvHandler = HandlersList::getInstance().getRecvHandler(recv_hand_name);
            }
            catch(const std::out_of_range& ex) {}
            std::string preprocc_hand_name = query.getColumn(7).isNull() ? "" : query.getColumn(7).getString();
            try {
                exp.preprocHandler = HandlersList::getInstance().getPreprocHandler(recv_hand_name);
            }
            catch(const std::out_of_range& ex) {}
            experiments.emplace_back(std::move(exp));
        }

        lastUsedFilter = filter;
        _updateSignal.emit();
    }

    sigc::signal<void()> updateSignal() const {
        return _updateSignal;
    }

private:
    Filter lastUsedFilter;
    ExperimentsList() = default;

    void addLocalExperiment(FullExperimentConfig config, size_t dbIdx) {
        Experiment exp(config);
        exp.dbIdx = dbIdx;
        experiments.emplace_back(std::move(exp));
    }

    std::vector<Experiment> experiments;

    sigc::signal<void()> _updateSignal;

};
