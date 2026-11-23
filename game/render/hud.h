#pragma once
#include <optional>

#include "gl.h"
#include "session/menu.h"

namespace agar {
class Hud {
public:
    explicit Hud(const GlApi& gl);
    ~Hud();
    void draw(const Match& match, const Menu& menu, int width, int height);

private:
    struct State {
        int width{}, height{}, seconds{}, speed{}, speedTenths{};
        std::vector<int> parts, scores;
        std::u32string name;
        Menu menu;
        bool operator==(const State&) const = default;
    };
    std::optional<State> m_previous;
    Program m_program;
    GLint m_screen;
    GLuint m_texture{}, m_vao{}, m_buffer{};
    GLsizei m_count{};
};
} // namespace agar
