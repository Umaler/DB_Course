#pragma once
#include <nlohmann/json.hpp>
#include <vector>
#include <sigc++/sigc++.h>

namespace utils {

template<typename T, typename U>
T getDefault(nlohmann::json j, U key, T default_val) {
    try {
        return j[key].template get<T>();
    }
    catch(...) {
        return default_val;
    }
}

}

enum Position {
    Receiver = 0b1,
    Preprocessor = 0b10,
    Draw_Processor = 0b100,
    DB_Processor = 0b1000
};

class Handler {
public:
    typedef std::pair<std::vector<std::vector<std::vector<double>>>,
                      std::vector<std::vector<std::vector<double>>>> dataType;

    virtual void set_settings(nlohmann::json config) = 0;
    virtual void process_data(dataType data) = 0;
    virtual void worker(std::stop_token stoken) = 0;
    virtual int allowed_positions() = 0;

    sigc::signal<void(dataType)> signal_processed_data() {
        return _processed_data;
    }

protected:
    sigc::signal<void(dataType)> _processed_data;

};

class DefaultHandler : public Handler {
public:
    void set_settings(nlohmann::json config) override {}

    void process_data(dataType data) override {
        _processed_data.emit(data);
    }

    void worker(std::stop_token stoken) override {}

    int allowed_positions() override {
        return Position::DB_Processor | Position::Draw_Processor | Position::Preprocessor;
    }

private:


};
