#pragma once

#include <gtkmm/glarea.h>
#include "Shader.hpp"
#include "DataSet.hpp"

#include <epoxy/gl.h>
#include <memory>
#include <vector>

class ExtendablePlot : public Gtk::GLArea {
public:
    ExtendablePlot();

    void addDataSet(std::shared_ptr<DataSet> ds);

    virtual ~ExtendablePlot() = default;

protected:

    const size_t left_reserve = 250;
    const size_t right_reserve = 20;
    const size_t up_reserve = 20;
    const size_t down_reserve = 100;

    double maxX = std::numeric_limits<double>::lowest();
    double minX = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    std::vector<std::shared_ptr<DataSet>> datasets;

    Shader shader;
    Shader textureShader;

    void onUpdates(const DataSet& updatedDS);

    void initShaders();

    void on_realize();

    struct EdgePositions {
        double up;
        double right;
        double down;
        double left;
    };

    struct OpenglDSBuffers {    //RAII wrapper for OpenGL buffers for datasets
        unsigned int VBO;
        unsigned int VAO;

        OpenglDSBuffers(const DataSet& dataSet);

        void enable();
        void disable();

        ~OpenglDSBuffers();

    };

    struct OpenglCairoBuffer {      //RAII wrapper for OpenGL buffers for Cairo drawing
        unsigned int VBO;
        unsigned int VAO;
        unsigned int texture;

        OpenglCairoBuffer(const Cairo::ImageSurface& surface);

        void enable();
        void disable();

        ~OpenglCairoBuffer();
    };

    void drawDataSet(const DataSet& data, Gdk::RGBA color, EdgePositions edgePos);

    void drawLegend(size_t windowWidth, size_t windowHeight, EdgePositions pos);

    bool on_render(const Glib::RefPtr< Gdk::GLContext >& context) override;
};
