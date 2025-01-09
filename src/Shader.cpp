#include "Shader.hpp"

#include <epoxy/gl.h>
#include <iostream>

Shader::Shader(const char* vertShaderProg, const char* fragShaderProg) :
    id(nullptr,
       [](unsigned int* _id){
            if(!_id)
                return;
            glDeleteProgram(*_id);
            delete _id;
       }),
    isOk(true)
{
    auto makeShader = [](const char* prog, GLenum type) -> unsigned int {
        unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &prog, NULL);
        glCompileShader(shader);
        int success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if(!success) {
            GLint logSize;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
            std::string infoLog(logSize, ' ');
            glGetShaderInfoLog(shader, logSize, NULL, &(infoLog[0]));
            throw std::runtime_error(infoLog);
        }
        return shader;
    };

    unsigned int vertShader, fragShader;
    try {
        vertShader = makeShader(vertShaderProg, GL_VERTEX_SHADER);
        fragShader = makeShader(fragShaderProg, GL_FRAGMENT_SHADER);
    }
    catch(const std::exception& ex) {
        std::cerr << "Error during shaders initialization: " << ex.what();
        isOk = false;
    }
    catch(...) {
        isOk = false;
    }

    unsigned int completeShader = glCreateProgram();
    glAttachShader(completeShader, vertShader);
    glAttachShader(completeShader, fragShader);
    glLinkProgram(completeShader);

    int success;
    glGetProgramiv(completeShader, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteShader(fragShader);
        glDeleteShader(vertShader);

        GLint logSize;
        glGetProgramiv(completeShader, GL_INFO_LOG_LENGTH, &logSize);
        std::string infoLog(logSize, ' ');
        glGetProgramInfoLog(completeShader, logSize, NULL, &(infoLog[0]));

        std::cerr << "Error during shaders linking: " << infoLog;
        isOk = false;

        return;
    }

    glDetachShader(completeShader, fragShader);
    glDetachShader(completeShader, vertShader);
    glDeleteShader(fragShader);
    glDeleteShader(vertShader);

    id = std::make_shared<unsigned int>(completeShader);

    return;
}

Shader::operator bool() {
    return isOk;
}

Shader::operator unsigned int() {
    return *id;
}
