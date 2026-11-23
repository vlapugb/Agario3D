#include "gl.h"

#include <stdexcept>
#include <string>

namespace agar {
namespace {
GLuint shader(const GlApi& gl, GLenum type, const char* text) {
    const auto id = gl.CreateShader(type);
    gl.ShaderSource(id, 1, &text, nullptr);
    gl.CompileShader(id);
    GLint ok{};
    gl.GetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]{};
        gl.GetShaderInfoLog(id, sizeof(log), nullptr, log);
        gl.DeleteShader(id);
        throw std::runtime_error(std::string("GLSL: ") + log);
    }
    return id;
}
} // namespace
Program::Program(const GlApi& api, const char* vertex, const char* fragment) : gl(api) {
    id = gl.CreateProgram();
    try {
        for (auto stage : {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER}) {
            auto part = shader(gl, stage, stage == GL_VERTEX_SHADER ? vertex : fragment);
            gl.AttachShader(id, part);
            gl.DeleteShader(part);
        }
        gl.LinkProgram(id);
        GLint ok{};
        gl.GetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            gl.GetProgramInfoLog(id, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("GLSL link: ") + log);
        }
    } catch (...) {
        gl.DeleteProgram(id);
        throw;
    }
}
Program::~Program() {
    gl.DeleteProgram(id);
}
} // namespace agar
