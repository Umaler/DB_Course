#pragma once

#include <limits>
#include <vector>
#include <sigc++/sigc++.h>
#include <gdkmm/rgba.h>

class DataSet {
public:

    struct Extrems {
        double maxX = std::numeric_limits<double>::lowest();
        double minX = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max();
    };


    void addDataPoint(double y) {
        addDataPoint(extr.maxX + 1, y);
    }

    void addDataPoint(double x, double y);

    //have to add implementation into hpp
    //because otherwise linker doesn't
    //see specializations
    template <typename Iterator>    //iterators must point to pair<x, y>
    void addData(Iterator begin, Iterator end) {
        bool toSort = false;
        for(Iterator it = begin; it != end; ++it) {
            toSort = toSort || it->first < extr.maxX;
            addPoint(it->first, it->second);
        }
        if(toSort) {
            sort();
        }

        signalChanged.emit(*this);
    }

    template <typename Iterator>
    void addDataWithoutX(Iterator begin, Iterator end) {
        bool toSort = false;
        size_t x = 0;
        for(Iterator it = begin; it != end; ++it, x++) {
            addPoint(x, *it);
        }

        signalChanged.emit(*this);
    }

    void clear();

    sigc::signal<void(DataSet&)> signalOnChanged() const;

    size_t getNumberOfPoints() const;

    void show(bool toDraw = true);
    bool isShown();

    void setColor(Gdk::RGBA color);
    Gdk::RGBA getColor();

    const double* getFirstElementAddress() const;
    size_t getSizeOfBuffer() const;
    Extrems getExtremums() const;

private:
    void sort();

    bool toDraw = true;
    Gdk::RGBA color = Gdk::RGBA(1, 0, 0);

    void addPoint(double x, double y);
    Extrems extr;

    sigc::signal<void(DataSet&)> signalChanged;

    std::vector<double> points; //contains points as double x, double y, double x, double y...

};
