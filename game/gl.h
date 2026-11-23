#pragma once

#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#include <stdexcept>
#include <string>

namespace agar {
class GlApi {
    using Resolver = void* (*)(const char*);
    Resolver m_resolver;
    template <class F>
    F load(const char* name) const {
        auto fn = reinterpret_cast<F>(m_resolver(name));
        if (!fn) {
            throw std::runtime_error(std::string("OpenGL 3.3: ") + name);
        }
        return fn;
    }

public:
    explicit GlApi(Resolver resolver) : m_resolver(resolver) {}
    // В том числе на Windows берём функции у того же контекста, что и рендерер Huynity.
#define AGAR_GL(Name) decltype(&::gl##Name) Name = load<decltype(&::gl##Name)>("gl" #Name)
    AGAR_GL(ActiveTexture);
    AGAR_GL(AttachShader);
    AGAR_GL(BindBuffer);
    AGAR_GL(BindTexture);
    AGAR_GL(BindVertexArray);
    AGAR_GL(BlendFunc);
    AGAR_GL(BufferData);
    AGAR_GL(CompileShader);
    AGAR_GL(ColorMask);
    AGAR_GL(CopyTexImage2D);
    AGAR_GL(CopyTexSubImage2D);
    AGAR_GL(CreateProgram);
    AGAR_GL(CreateShader);
    AGAR_GL(CullFace);
    AGAR_GL(DeleteBuffers);
    AGAR_GL(DeleteProgram);
    AGAR_GL(DeleteShader);
    AGAR_GL(DeleteTextures);
    AGAR_GL(DeleteVertexArrays);
    AGAR_GL(DepthFunc);
    AGAR_GL(DepthMask);
    AGAR_GL(Disable);
    AGAR_GL(DrawArrays);
    AGAR_GL(DrawElements);
    AGAR_GL(Enable);
    AGAR_GL(EnableVertexAttribArray);
    AGAR_GL(FrontFace);
    AGAR_GL(GenerateMipmap);
    AGAR_GL(GenBuffers);
    AGAR_GL(GenTextures);
    AGAR_GL(GenVertexArrays);
    AGAR_GL(GetProgramInfoLog);
    AGAR_GL(GetProgramiv);
    AGAR_GL(GetShaderInfoLog);
    AGAR_GL(GetShaderiv);
    AGAR_GL(GetUniformLocation);
    AGAR_GL(LinkProgram);
    AGAR_GL(ShaderSource);
    AGAR_GL(TexImage2D);
    AGAR_GL(TexParameteri);
    AGAR_GL(Uniform1f);
    AGAR_GL(Uniform1i);
    AGAR_GL(Uniform2f);
    AGAR_GL(Uniform3f);
    AGAR_GL(Uniform4f);
    AGAR_GL(Uniform4fv);
    AGAR_GL(UseProgram);
    AGAR_GL(VertexAttribPointer);
#undef AGAR_GL
};

class Program {
public:
    Program(const GlApi& gl, const char* vertex, const char* fragment);
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    GLint uniform(const char* name) const { return gl.GetUniformLocation(id, name); }
    const GlApi& gl;
    GLuint id{};
};
} // namespace agar
