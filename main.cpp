#include <iostream>
#include <gtkmm.h>
#include <string_view>
#include <array>

#include "experiments_list.hpp"
#include "hw_list.hpp"
#include "ExtendablePlot.hpp"

namespace
{
Gtk::Window* pMainWindow = nullptr;
Glib::RefPtr<Gtk::Application> app;
Glib::RefPtr<Gtk::Builder> pBuilder;
size_t main_window_selected_exp = GTK_INVALID_LIST_POSITION;

Glib::RefPtr<ExtendablePlot> plot;
std::shared_ptr<DataSet> dataToDraw;

ReceiverHandler* curRecvHandler = nullptr;
PreprocessingHandler* curPreprocessor = nullptr;

cv::VideoCapture camera;

template<typename T>
auto getWidget(std::string_view name) {
    auto widget = pBuilder->get_widget<T>(name.data());
    if(widget == nullptr) {
        std::cerr << "No widget " << name << std::endl;
    }
    return widget;
}

void update_extra_info() {
    if(main_window_selected_exp == GTK_INVALID_LIST_POSITION)
        return;

    try {
        Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(main_window_selected_exp);

        auto setEntryText = [](std::string_view name, Glib::ustring text) {
            getWidget<Gtk::Entry>(name.data())->get_buffer()->set_text(text);
        };

        if(exp.getReceiver())
            setEntryText("extra_info_recv_entry", exp.getReceiver()->get().get_settings().name);
        if(exp.getTransmitter())
            setEntryText("extra_info_trans_entry", exp.getTransmitter()->get().get_settings().name);
        if(exp.getReceiverHandler())
            setEntryText("extra_info_recv_handler_entry", exp.getReceiverHandler()->get().getName());
        if(exp.getPreprocessor())
            setEntryText("extra_info_preproc_entry", exp.getPreprocessor()->get().getName());

        setEntryText("extra_info_entries_count_entry", std::to_string(exp.getPacketsCount()));
        setEntryText("extra_info_photos_count_entry", std::to_string(exp.getPhotosCount()));
    }
    catch(const std::out_of_range& ex) {
        std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
    }
    catch(const std::exception& ex) {
        std::cerr << "Exception during pipeline working: " << ex.what() << std::endl;
    }
    catch(...) {
        std::cerr << "Unknown exception during pipeline working" << std::endl;
    }
}

template<typename T>
auto getObject(std::string_view name) {
    auto widget = pBuilder->get_object<T>(name.data());
    if(widget == nullptr) {
        std::cerr << "No object " << name << std::endl;
    }
    return widget;
}

bool pipelineWorker() {
    HandlerBase::datatype data;

    if (curRecvHandler == nullptr)
        return true;
    auto mbData = curRecvHandler->tryCollect();
    if(mbData)
        data = std::move(*mbData);

    if(curPreprocessor != nullptr) {
        auto mbProcessedData = curPreprocessor->process(data);
        if(mbProcessedData)
            data = std::move(*mbProcessedData);
    }

    if(main_window_selected_exp == GTK_INVALID_LIST_POSITION)
        return true;

    try {
        Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(main_window_selected_exp);
        exp.addPoint(data);

        uint32_t subcar = getWidget<Gtk::SpinButton>("main_window_subcar_sb")->get_value_as_int();
        uint32_t rx = getWidget<Gtk::SpinButton>("main_window_recv_ant_sb")->get_value_as_int();
        uint32_t tx = getWidget<Gtk::SpinButton>("main_window_trans_ant_sb")->get_value_as_int();
        bool selectedAmpl = getWidget<Gtk::DropDown>("main_window_drawed_data_type")->get_selected() == 0;

        auto& selectedPart = selectedAmpl ? data.first : data.second;
        if(rx < selectedPart.size() && tx < selectedPart[rx].size() && subcar < selectedPart[rx][tx].size()) {
            int real = data.first[rx][tx][subcar];
            int imag = data.second[rx][tx][subcar];

            double val;
            if(selectedAmpl)
                val = std::sqrt(real * real + imag * imag);
            else
                val = std::atan2(imag, real);
            dataToDraw->addDataPoint(val);
        }
    }
    catch(const std::out_of_range& ex) {
        std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
    }
    catch(const std::exception& ex) {
        std::cerr << "Exception during pipeline working: " << ex.what() << std::endl;
    }
    catch(...) {
        std::cerr << "Unknown exception during pipeline working" << std::endl;
    }

    return true;
}

bool camera_worker() {
    if(!getWidget<Gtk::ToggleButton>("main_window_start_recv")->get_active())
        return true;

    cv::Mat frame;
    camera >> frame;
    if(main_window_selected_exp == GTK_INVALID_LIST_POSITION)
        return true;

    try {
        Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(main_window_selected_exp);
        exp.addPhoto(frame);
    }
    catch(const std::out_of_range& ex) {
        std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
    }
    catch(const std::exception& ex) {
        std::cerr << "Exception during pipeline working: " << ex.what() << std::endl;
    }
    catch(...) {
        std::cerr << "Unknown exception during pipeline working" << std::endl;
    }
    return true;
}

void experiment_window_process() {
    auto updateHW = []() {
        auto recv_dd = getWidget<Gtk::DropDown>("new_exp_experiment_receiver_dd");
        auto trans_dd = getWidget<Gtk::DropDown>("new_exp_experiment_transmitter_dd");
        HW_List& hw_list = HW_List::get_instance();
        recv_dd->set_model(Gtk::StringList::create(hw_list.get_names()));
        trans_dd->set_model(Gtk::StringList::create(hw_list.get_names()));
    };
    updateHW();
    HW_List::get_instance().signal_update().connect(updateHW);

    auto updateHandlers = []() {
        auto recv_dd = getWidget<Gtk::DropDown>("new_exp_experiment_recvHandler_dd");
        auto preproc_dd = getWidget<Gtk::DropDown>("new_exp_experiment_preproc_dd");
        HandlersList& handlers_list = HandlersList::getInstance();
        recv_dd->set_model(Gtk::StringList::create(handlers_list.getRecvNames()));
        preproc_dd->set_model(Gtk::StringList::create(handlers_list.getPreprocNames()));
    };
    updateHandlers();

    getWidget<Gtk::Button>("new_exp_add_button")->signal_clicked().connect([](){
        FullExperimentConfig expConfig;
        expConfig.name = getWidget<Gtk::Entry>("new_exp_name_entry")->get_buffer()->get_text();
        expConfig.description = getObject<Gtk::TextBuffer>("experiment_desc_buf")->get_text();

        size_t recv_idx = getWidget<Gtk::DropDown>("new_exp_experiment_receiver_dd")->get_selected();
        if(recv_idx != GTK_INVALID_LIST_POSITION) {
            try {
                expConfig.receiver = HW_List::get_instance().get_hardware(recv_idx);
            }
            catch(const std::exception& ex) {
                std::cerr << "Incorrect selected receiver! " << ex.what() << std::endl;
            }
        }

        size_t trans_idx = getWidget<Gtk::DropDown>("new_exp_experiment_transmitter_dd")->get_selected();
        if(trans_idx != GTK_INVALID_LIST_POSITION) {
            try {
                expConfig.transmitter = HW_List::get_instance().get_hardware(trans_idx);
            }
            catch(const std::exception& ex) {
                std::cerr << "Incorrect selected transmitter! " << ex.what() << std::endl;
            }
        }

        size_t recvHand_idx = getWidget<Gtk::DropDown>("new_exp_experiment_recvHandler_dd")->get_selected();
        if(recvHand_idx != GTK_INVALID_LIST_POSITION) {
            try {
                expConfig.recvHandler = HandlersList::getInstance().getRecvHandler(recvHand_idx);
            }
            catch(const std::exception& ex) {
                std::cerr << "Incorrect selected recv handler! " << ex.what() << std::endl;
            }
        }

        size_t preprocHand_idx = getWidget<Gtk::DropDown>("new_exp_experiment_preproc_dd")->get_selected();
        if(preprocHand_idx != GTK_INVALID_LIST_POSITION) {
            try {
                expConfig.preprocHandler = HandlersList::getInstance().getPreprocHandler(preprocHand_idx);
            }
            catch(const std::exception& ex) {
                std::cerr << "Incorrect selected recv handler! " << ex.what() << std::endl;
            }
        }

        ExperimentsList::getInstance().addExperiment(expConfig);
    });
}

void stopDataCollecting() {
    getWidget<Gtk::ToggleButton>("main_window_start_recv")->set_active(false);
    HandlersList::getInstance().pauseAll();
}

void updateMainWindow() {
    uint64_t date_from = getWidget<Gtk::Calendar>("main_window_date_from")->get_date().to_unix();
    date_from += getWidget<Gtk::SpinButton>("main_window_hours_from_sb")->get_value_as_int() * 3600;
    date_from += getWidget<Gtk::SpinButton>("main_window_minutes_from_sb")->get_value_as_int() * 60;
    bool lower_date_bound = getWidget<Gtk::CheckButton>("main_window_date_from_filter_cb")->get_active();

    uint64_t date_to = getWidget<Gtk::Calendar>("main_window_date_to")->get_date().to_unix();
    date_to += getWidget<Gtk::SpinButton>("main_window_hours_to_sb")->get_value_as_int() * 3600;
    date_to += getWidget<Gtk::SpinButton>("main_window_minutes_to_sb")->get_value_as_int() * 60;
    bool upper_date_bound = getWidget<Gtk::CheckButton>("main_window_date_to_filter_cb")->get_active();

    ExperimentsList::Filter filter;
    if(lower_date_bound) filter.fromDate = date_from;
    if(upper_date_bound) filter.upToDate = date_to;

    size_t recv_idx = getWidget<Gtk::DropDown>("main_window_recv_filter_dd")->get_selected();
    if(recv_idx != GTK_INVALID_LIST_POSITION) {
        try {
            if(getWidget<Gtk::CheckButton>("main_window_recv_filter_cb")->get_active())
                filter.recvIdx = HW_List::get_instance().get_hardware(recv_idx).getDBId();
        }
        catch(const std::exception& ex) {
            std::cerr << "Incorrect selected receiver! " << ex.what() << std::endl;
        }
    }

    size_t trans_idx = getWidget<Gtk::DropDown>("main_window_trans_filter_dd")->get_selected();
    if(trans_idx != GTK_INVALID_LIST_POSITION) {
        try {
            if(getWidget<Gtk::CheckButton>("main_window_trans_filter_cb")->get_active())
                filter.transIdx = HW_List::get_instance().get_hardware(trans_idx).getDBId();
        }
        catch(const std::exception& ex) {
            std::cerr << "Incorrect selected transmitter! " << ex.what() << std::endl;
        }
    }

    ExperimentsList::getInstance().updateList(filter);
}

void import_window_process() {
    static bool continue_import = false;

    getWidget<Gtk::Button>("import_cancel_button")->signal_clicked().connect([&continue_import]() {
        continue_import = false;
    });

    auto path_button = getWidget<Gtk::Button>("import_get_path_button");
    path_button->signal_clicked().connect([](){
        auto fc = Gtk::FileDialog::create();
        fc->set_modal();
        fc->open([fc](const Glib::RefPtr<Gio::AsyncResult>& result){
            try {
                auto path_entry = getWidget<Gtk::Entry>("import_get_path_entry");
                path_entry->get_buffer()->set_text(fc->open_finish(result)->get_path());
            }
            catch (const Gtk::DialogError& err)
            {}
            catch (const Glib::Error& err)
            {
                std::cerr << "Unexpected exception. " << err.what() << std::endl;
            }
        });
    });

    auto on_hw_update = []() {
        auto recv_dd = getWidget<Gtk::DropDown>("import_recv_dd");
        auto trans_dd = getWidget<Gtk::DropDown>("import_trans_dd");
        HW_List& hw_list = HW_List::get_instance();
        recv_dd->set_model(Gtk::StringList::create(hw_list.get_names()));
        trans_dd->set_model(Gtk::StringList::create(hw_list.get_names()));
    };
    on_hw_update();
    HW_List::get_instance().signal_update().connect([on_hw_update](){
        on_hw_update();
    });

    auto final_button = getWidget<Gtk::Button>("import_window_import_button");
    final_button->signal_clicked().connect([](){
        try {
            continue_import = true;
            HW_List& hw_list = HW_List::get_instance();

            std::string path = getWidget<Gtk::Entry>("import_get_path_entry")->get_buffer()->get_text();
            std::string expName = getWidget<Gtk::Entry>("import_name_entry")->get_buffer()->get_text();
            std::string expDesc = getObject<Gtk::TextBuffer>("import_desc_buffer")->get_text();

            size_t rawRecvIdx = getWidget<Gtk::DropDown>("import_recv_dd")->get_selected();
            bool receiverSelected = rawRecvIdx != GTK_INVALID_LIST_POSITION;
            int32_t recvIdx = receiverSelected ? hw_list.get_hardware(rawRecvIdx).getDBId() : -1;

            size_t rawTransIdx = getWidget<Gtk::DropDown>("import_trans_dd")->get_selected();
            bool transieverSelected = rawTransIdx != GTK_INVALID_LIST_POSITION;
            int32_t transIdx = transieverSelected ? hw_list.get_hardware(rawTransIdx).getDBId() : -1;

            SQLite::Database& db = DB_Handler::get_db();
            struct Attacher {
                Attacher(const std::string path) {
                    SQLite::Statement attachState(DB_Handler::get_db(), "ATTACH DATABASE @path AS abase;");
                    attachState.bind("@path", path);
                    attachState.exec();
                }

                ~Attacher() {
                    SQLite::Statement detactState(DB_Handler::get_db(), "DETACH DATABASE abase;");
                    detactState.exec();
                }
            } attacher(path);

            SQLite::Transaction commit(db);

            SQLite::Statement newExpState(db, "INSERT INTO experiment (name, description, hardware_tx_id, hardware_rx_id) VALUES (@name, @desc, @tx_id, @rx_id)");
            newExpState.bind("@name", expName);
            newExpState.bind("@desc", expDesc);
            if(transieverSelected)
                newExpState.bind("@tx_id", transIdx);
            else
                newExpState.bind("@tx_id", "NULL");

            if(receiverSelected)
                newExpState.bind("@rx_id", recvIdx);
            else
                newExpState.bind("@rx_id", "NULL");

            newExpState.exec();
            uint32_t expId = db.getLastInsertRowid();

            constexpr int prefixes = 3;
            std::array<char, prefixes> antPrefixes {'f', 's', 't'};
            SQLite::Statement packetsSelect(db, "SELECT * from abase.packet ORDER BY id");
            SQLite::Statement movePacket(db, "INSERT INTO packet (marker, timestamp, experiment_id) VALUES (@marker, @time, @exp_id)");
            SQLite::Statement getOldMeas(db, "SELECT * FROM abase.measurement WHERE id_packet = @cur_pack");
            SQLite::Statement moveRawData(db, "INSERT INTO measurement (id_packet, num_sub, rx, tx, real_part, imag_part) VALUES (@id, @sub, @rx, @tx, @real, @imag)");
            uint32_t packets_n = db.execAndGet("SELECT COUNT(1) FROM abase.packet;");
            uint32_t packets_pasted = 0;
            getWidget<Gtk::Window>("import_progress_window")->set_visible(true);
            auto pbar = getWidget<Gtk::LevelBar>("import_progress_bar");

            while(packetsSelect.executeStep()) {
                packets_pasted++;
                pbar->set_value(static_cast<double>(packets_pasted) / static_cast<double>(packets_n));
                uint32_t old_pack_idx = packetsSelect.getColumn(0);
                if(old_pack_idx == 0)
                    continue;

                std::string old_marker = std::to_string(uint32_t(packetsSelect.getColumn(1)));
                std::string old_desc = packetsSelect.getColumn(2);

                movePacket.bind("@marker", std::string("Старый эксперимент:\n") + old_marker + "\n" + old_desc);
                std::istringstream ss(old_desc);
                std::tm t = {};
                ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
                if(ss.fail() || std::mktime(&t) < 0)
                    movePacket.bind("@time");
                else
                    movePacket.bind("@time", static_cast<int64_t>(std::mktime(&t)));
                movePacket.bind("@exp_id", expId);
                movePacket.exec();
                uint32_t new_pack_idx = db.getLastInsertRowid();

                getOldMeas.bind("@cur_pack", old_pack_idx);

                while(getOldMeas.executeStep()) {
                    uint32_t oldMeasId = getOldMeas.getColumn(0);
                    for(int i = 0; i < prefixes; i++) {
                        for(int j = 0; j < prefixes; j++) {
                            moveRawData.bind("@id", new_pack_idx);
                            moveRawData.bind("@sub", static_cast<uint32_t>(getOldMeas.getColumn(2)));
                            moveRawData.bind("@rx", i);
                            moveRawData.bind("@tx", j);
                            moveRawData.bind("@real", static_cast<int32_t>(getOldMeas.getColumn((i * prefixes + j) * 2 + 3)));
                            moveRawData.bind("@imag", static_cast<int32_t>(getOldMeas.getColumn((i * prefixes + j) * 2 + 4)));
                            moveRawData.exec();
                            moveRawData.reset();
                        }
                    }
                }

                movePacket.reset();
                getOldMeas.reset();
                if(!continue_import)
                    break;
                Glib::MainContext::get_default()->iteration(false);
            }

            commit.commit();
            updateMainWindow();
        }
        catch(const std::exception& ex)
        {
            std::cerr << "Error during importing experiment: " << ex.what() << std::endl;
        }
        catch(...)
        {
            std::cerr << "Unknown error during importing experiment: "<< std::endl;
        }

        getWidget<Gtk::Window>("import_progress_window")->hide();
        getWidget<Gtk::Window>("import_window")->hide();
    });
}

void hardware_window_process()
{
    static size_t selected_item = GTK_INVALID_LIST_POSITION;

    auto draw_selected = [](guint pos) {
        auto wname = getWidget<Gtk::Entry>("hw_entry_name");
        auto want = getWidget<Gtk::SpinButton>("hw_antennas_sb");
        auto wsub = getWidget<Gtk::SpinButton>("hw_subcar_sb");
        auto wdesc = getObject<Gtk::TextBuffer>("hw_desc_buffer");

        Hardware::Settings curSettings;
        try {
            curSettings = HW_List::get_instance().get_hardware(pos).get_settings();
        }
        catch(const std::range_error& ex) {
            std::cerr << ex.what() << std::endl;
            return;
        }
        wname->get_buffer()->set_text(curSettings.name);
        want->set_value(curSettings.antennas);
        wsub->set_value(curSettings.sub_cars);
        wdesc->set_text(curSettings.description);
    };

    auto get_and_draw_selected = [pBuilder, draw_selected]() {
        auto hw_selector = getObject<Gtk::SingleSelection>("hw_selector");
        hw_selector->set_model(Gtk::StringList::create(HW_List::get_instance().get_names()));
        if(selected_item != GTK_INVALID_LIST_POSITION)
            draw_selected(selected_item);
    };

    HW_List::get_instance().signal_update().connect(get_and_draw_selected);

    auto hw_list_view = getWidget<Gtk::ListView>("hw_list_view");
    hw_list_view->signal_activate().connect([&selected_item, draw_selected](size_t pos) {
        selected_item = pos;
        draw_selected(pos);
    });

    auto new_bt = getWidget<Gtk::Button>("hw_new_button");
    new_bt->signal_clicked().connect([](){
        HW_List::get_instance().add_hardware({"Новое оборудование", "", 0, 0});
    });

    auto update_list = []() {
        auto hw_selector = getObject<Gtk::SingleSelection>("hw_selector");
        hw_selector->set_model(Gtk::StringList::create(HW_List::get_instance().get_names()));
    };

    auto apply_bt = getWidget<Gtk::Button>("hw_apply_button");
    apply_bt->signal_clicked().connect([update_list](){
        auto wname = getWidget<Gtk::Entry>("hw_entry_name");
        auto want = getWidget<Gtk::SpinButton>("hw_antennas_sb");
        auto wsub = getWidget<Gtk::SpinButton>("hw_subcar_sb");
        auto wdesc = getObject<Gtk::TextBuffer>("hw_desc_buffer");

        Hardware::Settings curSettings;
        curSettings.name = wname->get_buffer()->get_text();
        curSettings.antennas = want->get_value();
        curSettings.sub_cars = wsub->get_value();
        curSettings.description = wdesc->get_text();

        if(selected_item != GTK_INVALID_LIST_POSITION)
            HW_List::get_instance().get_hardware(selected_item).set_settings(curSettings);
    });

    auto cancel_bt = getWidget<Gtk::Button>("hw_cancel_button");
    cancel_bt->signal_clicked().connect([get_and_draw_selected]() {
        get_and_draw_selected();
    });

    auto delete_bt = getWidget<Gtk::Button>("hw_delete_button");
    delete_bt->signal_clicked().connect([&selected_item, update_list]() {
        if(selected_item != GTK_INVALID_LIST_POSITION) {
            size_t buf = selected_item;
            selected_item = GTK_INVALID_LIST_POSITION;
            HW_List::get_instance().delete_hardware(buf);

            getWidget<Gtk::Entry>("hw_entry_name")->get_buffer()->set_text("");
            getWidget<Gtk::SpinButton>("hw_antennas_sb")->set_value(0);
            getWidget<Gtk::SpinButton>("hw_subcar_sb")->set_value(0);
            getObject<Gtk::TextBuffer>("hw_desc_buffer")->set_text("");
        }
    });

    update_list();
}

void updatePlot() {
    if(main_window_selected_exp == GTK_INVALID_LIST_POSITION)
        return;

    size_t pos = main_window_selected_exp;
    try {
        Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(pos);

        uint32_t subcar = getWidget<Gtk::SpinButton>("main_window_subcar_sb")->get_value_as_int();
        uint32_t rx = getWidget<Gtk::SpinButton>("main_window_recv_ant_sb")->get_value_as_int();
        uint32_t tx = getWidget<Gtk::SpinButton>("main_window_trans_ant_sb")->get_value_as_int();
        bool selectedAmpl = getWidget<Gtk::DropDown>("main_window_drawed_data_type")->get_selected() == 0;

        std::vector<double> points = exp.getPoints(rx, tx, subcar, selectedAmpl);
        dataToDraw->clear();
        dataToDraw->addDataWithoutX(points.begin(), points.end());
    }
    catch(const std::out_of_range& ex) {
        std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
    }
    catch(const std::exception& ex) {
        std::cerr << "Exception during updating plot: " << ex.what() << std::endl;
    }
    catch(...) {
        std::cerr << "Unknown exception during updating plot" << std::endl;
    }
}

void export_window_process() {
    auto path_button = getWidget<Gtk::Button>("export_select_dir_button");
    path_button->signal_clicked().connect([](){
        auto fc = Gtk::FileDialog::create();
        fc->set_modal();
        fc->select_folder([fc](const Glib::RefPtr<Gio::AsyncResult>& result){
            try {
                auto path_entry = getWidget<Gtk::Entry>("export_dir_entry");
                path_entry->get_buffer()->set_text(fc->select_folder_finish(result)->get_path());
            }
            catch (const Gtk::DialogError& err)
            {}
            catch (const Glib::Error& err)
            {
                std::cerr << "Unexpected exception. " << err.what() << std::endl;
            }
        });
    });

    getWidget<Gtk::Button>("export_start_bn")->signal_clicked().connect([](){
        Experiment::ExportFilters filters;
        auto getBool = [](std::string_view name) {
            return getWidget<Gtk::CheckButton>(name.data())->get_active();
        };

        filters.ampl = getBool("export_ampl_bn");
        filters.phase = getBool("export_phase_bn");
        filters.real = getBool("export_real_bn");
        filters.imag = getBool("export_imag_bn");
        filters.image = getBool("export_pictures_bn");

        if(getBool("export_marker_bn")) {
            filters.marker = getWidget<Gtk::Entry>("export_marker_entry")->get_buffer()->get_text();
        }

        if(main_window_selected_exp == GTK_INVALID_LIST_POSITION)
            return;

        try {
            Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(main_window_selected_exp);
            exp.exportData(getWidget<Gtk::Entry>("export_dir_entry")->get_buffer()->get_text(), filters, [](double pct) {
                getWidget<Gtk::Window>("import_progress_window")->set_visible(true);
                auto pbar = getWidget<Gtk::LevelBar>("import_progress_bar");
                pbar->set_value(pct);
            });
            getWidget<Gtk::Window>("import_progress_window")->set_visible(false);
        }
        catch(const std::out_of_range& ex) {
            std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
        }
        catch(const std::exception& ex) {
            std::cerr << "Exception during exporting: " << ex.what() << std::endl;
        }
        catch(...) {
            std::cerr << "Unknown exception during exporting" << std::endl;
        }
    });

}

void main_window_process(Glib::RefPtr<Gtk::Builder> pBuilder)
{

    //auto glarea_box = refBuilder->get_widget<Gtk::Box>("main_glarea_box");
    auto conButtonWindow = [pBuilder](std::string_view bname, std::string_view wname) {
        auto button = pBuilder->get_widget<Gtk::Button>(bname.data());
        if(button == nullptr) {
            std::cerr << "No button " << bname;
            return;
        }

        auto window = pBuilder->get_widget<Gtk::Window>(wname.data());
        if(window == nullptr) {
            std::cerr << "No window " << wname;
            return;
        }

        button->signal_clicked().connect(
            [button, window]() {
                window->set_visible(true);
            }
        );
    };

    auto exp_list_view = getWidget<Gtk::ListView>("experiments_list_view");
    exp_list_view->signal_activate().connect([](size_t pos) {
        main_window_selected_exp = pos;

        if(pos == GTK_INVALID_LIST_POSITION)
            return;

        try {
            Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(pos);
            auto setEntryText = [](std::string_view entryName, Glib::ustring text) {
                getWidget<Gtk::Entry>(entryName.data())->get_buffer()->set_text(text);
            };

            setEntryText("main_exp_name_entry", exp.getName());
            getObject<Gtk::TextBuffer>("main_exp_desc_buffer")->set_text(exp.getDescription());
            getObject<Gtk::TextBuffer>("main_window_json_buf")->set_text(exp.getConfig().dump(4));
            stopDataCollecting();

            if(exp.getReceiverHandler())
                curRecvHandler = &(exp.getReceiverHandler()->get());
            else
                curRecvHandler = nullptr;

            if(exp.getPreprocessor())
                curPreprocessor = &(exp.getPreprocessor()->get());
            else
                curPreprocessor = nullptr;

            updatePlot();
            update_extra_info();
        }
        catch(const std::out_of_range& ex) {
            std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
        }
        catch(const std::exception& ex) {
            std::cerr << "Exception during updating info about new selected experiment: " << ex.what() << std::endl;
        }
        catch(...) {
            std::cerr << "Unknown exception during updating info about new selected experiment" << std::endl;
        }
    });

    getWidget<Gtk::Button>("main_window_update_list")->signal_clicked().connect(&updateMainWindow);

    ExperimentsList::getInstance().updateSignal().connect([](){
        auto clearEntry = [](std::string_view name) {
            getWidget<Gtk::Entry>(name.data())->get_buffer()->set_text("");
        };
        clearEntry("main_exp_name_entry");
        getObject<Gtk::TextBuffer>("main_exp_desc_buffer")->set_text("");
        stopDataCollecting();

        std::vector<Glib::ustring> names = ExperimentsList::getInstance().getExperimentsNames();
        getObject<Gtk::SingleSelection>("main_window_exp_list_selection")->set_model(Gtk::StringList::create(names));

        main_window_selected_exp = GTK_INVALID_LIST_POSITION;
    });

    getWidget<Gtk::Button>("main_window_delete_bn")->signal_clicked().connect([](){
        try {
            if(main_window_selected_exp == GTK_INVALID_LIST_POSITION)
                return;

            ExperimentsList::getInstance().deleteExperiment(main_window_selected_exp);
        }
        catch(const std::out_of_range& ex) {
            std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
        }
        catch(const std::exception& ex) {
            std::cerr << "Exception during deleting experiment: " << ex.what() << std::endl;
        }
        catch(...) {
            std::cerr << "Unknown exception during deleting experiment" << std::endl;
        }
    });

    getWidget<Gtk::Button>("main_window_cancel_json")->signal_clicked().connect([](){
        size_t pos = main_window_selected_exp;

        if(pos == GTK_INVALID_LIST_POSITION)
            return;

        try {
            Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(pos);

            getObject<Gtk::TextBuffer>("main_window_json_buf")->set_text(exp.getConfig().dump(4));
        }
        catch(const std::out_of_range& ex) {
            std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
        }
        catch(const std::exception& ex) {
            std::cerr << "Exception during canceling settings: " << ex.what() << std::endl;
        }
        catch(...) {
            std::cerr << "Unknown exception during canceling settings" << std::endl;
        }
    });

    getWidget<Gtk::Button>("main_window_accept_json")->signal_clicked().connect([](){
        size_t pos = main_window_selected_exp;

        if(pos == GTK_INVALID_LIST_POSITION)
            return;

        try {
            Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(pos);

            exp.setConfig(nlohmann::json::parse(getObject<Gtk::TextBuffer>("main_window_json_buf")->get_text()));

            getObject<Gtk::TextBuffer>("main_window_json_buf")->set_text(exp.getConfig().dump(4));
        }
        catch(const std::out_of_range& ex) {
            std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
        }
        catch(const std::exception& ex) {
            std::cerr << "Exception during canceling settings: " << ex.what() << std::endl;
        }
        catch(...) {
            std::cerr << "Unknown exception during canceling settings" << std::endl;
        }
    });

    getWidget<Gtk::ToggleButton>("main_window_start_recv")->signal_toggled().connect([](){
        bool off = !getWidget<Gtk::ToggleButton>("main_window_start_recv")->get_active();
        if(off) {
            HandlersList::getInstance().pauseAll();
        }
        else {
            size_t pos = main_window_selected_exp;

            if(pos == GTK_INVALID_LIST_POSITION)
                return;

            try {
                Experiment& exp = ExperimentsList::getInstance().getExperimentByIdx(pos);

                if(exp.getReceiverHandler())
                    static_cast<ReceiverHandler&>(*exp.getReceiverHandler()).set_pause(false);

            }
            catch(const std::out_of_range& ex) {
                std::cerr << "Something went wrong and selected experiment is out of range of available experiments" << std::endl;
            }
            catch(const std::exception& ex) {
                std::cerr << "Exception during canceling settings: " << ex.what() << std::endl;
            }
            catch(...) {
                std::cerr << "Unknown exception during canceling settings" << std::endl;
            }
        }
    });

    auto on_hw_update = []() {
        auto recv_dd = getWidget<Gtk::DropDown>("main_window_recv_filter_dd");
        auto trans_dd = getWidget<Gtk::DropDown>("main_window_trans_filter_dd");
        HW_List& hw_list = HW_List::get_instance();
        recv_dd->set_model(Gtk::StringList::create(hw_list.get_names()));
        trans_dd->set_model(Gtk::StringList::create(hw_list.get_names()));
    };
    on_hw_update();
    HW_List::get_instance().signal_update().connect([on_hw_update](){
        on_hw_update();
    });

    getWidget<Gtk::Button>("main_window_marker_apply")->signal_clicked().connect([](){
        auto marker = getWidget<Gtk::Entry>("main_window_marker_entry")->get_buffer()->get_text();
        MarkerManager::getInstance().setMarker(marker);
    });

    getWidget<Gtk::Button>("main_window_marker_cancel")->signal_clicked().connect([](){
        auto marker = MarkerManager::getInstance().getMarker();
        getWidget<Gtk::Entry>("main_window_marker_entry")->get_buffer()->set_text(marker);
    });

    getWidget<Gtk::SpinButton>("main_window_subcar_sb")->signal_value_changed().connect(&updatePlot);
    getWidget<Gtk::SpinButton>("main_window_recv_ant_sb")->signal_value_changed().connect(&updatePlot);
    getWidget<Gtk::SpinButton>("main_window_trans_ant_sb")->signal_value_changed().connect(&updatePlot);
    getWidget<Gtk::DropDown>("main_window_drawed_data_type")->property_selected().signal_changed().connect(&updatePlot);

    getWidget<Gtk::AspectFrame>("main_window_plot_ratio_frame")->set_child(*plot);

    conButtonWindow("exp_window_button", "experiment_window");
    conButtonWindow("hw_window_button", "hw_window");
    conButtonWindow("import_window_button", "import_window");
    conButtonWindow("main_window_extra_info_bn", "extra_info_window");
    conButtonWindow("export_window_button", "export_window");
}

void on_app_activate()
{
  // Load the GtkBuilder file and instantiate its widgets:
  auto refBuilder = Gtk::Builder::create();
  try
  {
    refBuilder->add_from_file("db_collector.ui");
  }
  catch(const Glib::FileError& ex)
  {
    std::cerr << "FileError: " << ex.what() << std::endl;
    return;
  }
  catch(const Glib::MarkupError& ex)
  {
    std::cerr << "MarkupError: " << ex.what() << std::endl;
    return;
  }
  catch(const Gtk::BuilderError& ex)
  {
    std::cerr << "BuilderError: " << ex.what() << std::endl;
    return;
  }
  pBuilder = refBuilder;

  // Get the GtkBuilder-instantiated dialog:
  pMainWindow = refBuilder->get_widget<Gtk::Window>("main_window");
  if (!pMainWindow)
  {
    std::cerr << "Could not get the dialog" << std::endl;
    return;
  }


  pMainWindow->signal_hide().connect([] () {
    delete pMainWindow;
    app->quit();
  });
  //glarea_box->remove(*glarea);
  //glarea_box->prepend(*Gtk::make_managed<Gtk::Label>("BEBRA"));



  app->add_window(*pMainWindow);
  pMainWindow->set_visible(true);

  plot = std::make_shared<ExtendablePlot>();
  dataToDraw = std::make_shared<DataSet>();
  plot->addDataSet(dataToDraw);

  main_window_process(refBuilder);
  hardware_window_process();
  import_window_process();
  experiment_window_process();
  export_window_process();

  Glib::signal_idle().connect(&pipelineWorker);

  std::filesystem::create_directory("images");
  for (int i = 0; i < 16 && !camera.open(i); i++) {}
  if(camera.isOpened())
    Glib::signal_timeout().connect(&camera_worker, 1000);
}
} // anonymous namespace

int main(int argc, char** argv)
{
    #ifdef __linux__
        // because since gtk 4.14 default behavior is
        // to prefer gles over gl
        // gitlab.gnome.org/GNOME/gtk/-/issues/6589
        std::string var("GDK_DEBUG=gl-prefer-gl");
        putenv(&var[0]);
    #endif

  app = Gtk::Application::create("org.gtkmm.example");

  app->signal_activate().connect([] () { on_app_activate(); });

  return app->run(argc, argv);
}
