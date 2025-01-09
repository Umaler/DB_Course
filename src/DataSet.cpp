#include "DataSet.hpp"

void DataSet::addDataPoint(double x, double y) {
    bool toSort = x < extr.maxX;
    addPoint(x, y);
    if(toSort) {
        sort();
    }

    signalChanged.emit(*this);
}

void DataSet::clear() {
    extr = Extrems();
    points.clear();
    signalChanged.emit(*this);
}

void DataSet::show(bool toDraw) {
    this->toDraw = toDraw;
    signalChanged.emit(*this);
}

bool DataSet::isShown() {
    return toDraw;
}

void DataSet::setColor(Gdk::RGBA color) {
    this->color = color;
    signalChanged.emit(*this);
}

Gdk::RGBA DataSet::getColor() {
    return color;
}

sigc::signal<void(DataSet&)> DataSet::signalOnChanged() const {
    return signalChanged;
}

size_t DataSet::getNumberOfPoints() const {
    return points.size() / 2;
}

const double* DataSet::getFirstElementAddress() const {
    if(points.empty())
        return nullptr;
    return &(points[0]);
}

size_t DataSet::getSizeOfBuffer() const {
    return sizeof(double) * points.size();
}

DataSet::Extrems DataSet::getExtremums() const {
    return extr;
}

void DataSet::sort() {
    size_t numOfPoints = getNumberOfPoints();
    for(size_t i = 0; i < numOfPoints; i++) {
        for(size_t j = i; j < numOfPoints; j++) {
            if(points[j * 2] < points[i * 2]) {
                std::swap(points[j * 2], points[i * 2]);
                std::swap(points[j * 2 + 1], points[i * 2 + 1]);
            }
        }
    }
}

void DataSet::addPoint(double x, double y) {
    points.push_back(x);
    points.push_back(y);

    if(x > extr.maxX) extr.maxX = x;
    if(x < extr.minX) extr.minX = x;
    if(y > extr.maxY) extr.maxY = y;
    if(y < extr.minY) extr.minY = y;
}
