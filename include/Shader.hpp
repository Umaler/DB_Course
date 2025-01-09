#pragma once

#include <memory>

class Shader {
public:

    Shader() = default;

    Shader(const char* vertShaderProg, const char* fragShaderProg);

    Shader(const Shader& other) = default;

    Shader& operator=(const Shader& other) = default;

    operator bool();

    operator unsigned int();

    ~Shader() = default;

private:
    std::shared_ptr<unsigned int> id;
    bool isOk = false;

};
