#pragma once

#include <string>
#include <sigc++/sigc++.h>

class MarkerManager {
public:

    static MarkerManager& getInstance() {
        static MarkerManager mm;
        return mm;
    }

    void setMarker(std::string newValue) {
        value = newValue;
        _updateSignal.emit();
    }

    const std::string& getMarker() const {
        return value;
    }

    sigc::signal<void()> updateSignal() const {
        return _updateSignal;
    }

private:
    sigc::signal<void()> _updateSignal;
    std::string value;
    MarkerManager() = default;

};
