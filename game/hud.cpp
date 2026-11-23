#include "hud.h"

#include "huynity/image/image_buffer.h"
#include "huynity/objectlib/params/triangle_mesh_params.h"
#include "huynity/render_contracts/render_snapshot.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

namespace agar {
namespace {
namespace obj = huynity::objectlib;
namespace render = huynity::render_contracts;

enum Ink {
    Glass,
    Button,
    Selected,
    Line,
    Quiet,
    Bright,
    Mint,
    Coral,
    Violet
};
constexpr std::array<render::Color, 9> colors{{{0.034, 0.069, 0.083, 0.78},
                                               {0.12, 0.22, 0.25, 0.38},
                                               {0.14, 0.52, 0.46, 0.56},
                                               {0.17, 0.23, 0.26, 0.9},
                                               {0.64, 0.72, 0.74, 1},
                                               {0.94, 0.98, 0.97, 1},
                                               {0.36, 0.96, 0.84, 1},
                                               {1, 0.52, 0.46, 1},
                                               {0.78, 0.62, 1, 1}}};
constexpr int tileWidth = 48, tileHeight = 88, atlasWidth = 768, atlasHeight = 968;

std::shared_ptr<const huynity::image::ImageBuffer> loadAtlas() {
    const char* base = SDL_GetBasePath();
    if (!base) {
        throw std::runtime_error(SDL_GetError());
    }
    std::string path = std::string(base) + "hud.bmp";
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    Surface bitmap{SDL_LoadBMP(path.c_str()), SDL_DestroySurface};
    if (!bitmap) {
        path = std::string(base) + "../share/agario3d/hud.bmp";
        bitmap.reset(SDL_LoadBMP(path.c_str()));
    }
    if (!bitmap || bitmap->w != atlasWidth || bitmap->h != atlasHeight) {
        throw std::runtime_error("Cannot read HUD atlas: " + path);
    }
    Surface rgba{SDL_ConvertSurface(bitmap.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface};
    if (!rgba) {
        throw std::runtime_error(SDL_GetError());
    }
    auto image = huynity::image::ImageBuffer::create({atlasWidth, atlasHeight}).value();
    for (int y = 0; y < atlasHeight; ++y) {
        const auto* row = static_cast<const std::byte*>(rgba->pixels) + y * rgba->pitch;
        auto target = image.row(y);
        for (int x = 0; x < atlasWidth; ++x) {
            target[x * 4] = target[x * 4 + 1] = target[x * 4 + 2] = std::byte{255};
            target[x * 4 + 3] = row[x * 4];
        }
    }
    return std::make_shared<const huynity::image::ImageBuffer>(std::move(image));
}

int glyph(char32_t c) {
    if (c >= U' ' && c <= U'~') {
        return static_cast<int>(c - U' ');
    }
    if (c >= U'А' && c <= U'я') {
        return 95 + static_cast<int>(c - U'А');
    }
    const auto index = std::u32string_view(U"Ёё·—").find(c);
    return index == std::u32string_view::npos ? '?' - ' ' : 159 + static_cast<int>(index);
}

double textWidth(std::u32string_view text, double size) {
    return text.size() * size * 0.6;
}

struct Batch {
    obj::TriangleMesh2Params mesh;
    std::vector<huynity::math::Point2> uv;
    std::vector<float> radii;

    void quad(double x, double y, double w, double h, double u, double v, double du, double dv, float radius = 0) {
        const auto a = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.insert(mesh.positions.end(), {{{x, y}}, {{x + w, y}}, {{x + w, y + h}}, {{x, y + h}}});
        mesh.triangles.insert(mesh.triangles.end(), {{{a, a + 1, a + 2}}, {{a, a + 2, a + 3}}});
        radii.insert(radii.end(), 4, radius);
        uv.insert(uv.end(), {{{u, v}}, {{u + du, v}}, {{u + du, v + dv}}, {{u, v + dv}}});
    }
    void rect(double x, double y, double w, double h) {
        if (w > 0 && h > 0) {
            quad(x, y, w, h, 0.995, 0.995, 0, 0);
        }
    }
    void rounded(const Box& r, float radius) { quad(r.x, r.y, r.w, r.h, 0, 0, 1, 1, radius); }
    void text(double x, double y, double size, std::u32string_view label) {
        for (auto c : label) {
            if (c != U' ') {
                const auto id = glyph(c);
                quad(x - 4 * size / 64.0, y, size * tileWidth / 64.0, size * tileHeight / 64.0,
                     (id % 16) * tileWidth / double(atlasWidth), (id / 16) * tileHeight / double(atlasHeight),
                     tileWidth / double(atlasWidth), tileHeight / double(atlasHeight));
            }
            x += size * 0.6;
        }
    }
};

struct GlyphVertex {
    float x, y, u, v, r, g, b, a, radius;
};
constexpr const char* glyphVertex = R"(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in vec4 ink;
layout(location=3) in float roundness;
uniform vec2 screen;
out vec2 uv; out vec4 tint; flat out float radius;
void main(){uv=texCoord;tint=ink;radius=roundness;gl_Position=vec4(position/screen*vec2(2,-2)+vec2(-1,1),0,1);})";
constexpr const char* glyphFragment = R"(#version 330 core
in vec2 uv; in vec4 tint; flat in float radius;
uniform sampler2D atlas; out vec4 color;
void main(){
    vec2 pixel = max(fwidth(uv), vec2(1e-6));
    if(radius>0.){
        vec2 q = (abs(uv-.5)-.5)/pixel+radius;
        float edge=length(max(q,0.))+min(max(q.x,q.y),0.)-radius;
        float border=smoothstep(-1.6,-.45,edge);
        color=vec4(mix(tint.rgb,vec3(.43,.66,.68),.55*border),
                   max(tint.a,.42*border)*(1.-smoothstep(-1.,0.,edge)));
    } else {
        float coverage=texture(atlas,uv).a;
        color=vec4(tint.rgb,tint.a*smoothstep(.045,.955,coverage));
    }
})";
} // namespace

Hud::Hud(const GlApi& api) : m_program(api, glyphVertex, glyphFragment), m_screen(m_program.uniform("screen")) {
    const auto atlas = loadAtlas();
    m_program.gl.GenTextures(1, &m_texture);
    m_program.gl.BindTexture(GL_TEXTURE_2D, m_texture);
    m_program.gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                            atlas->bytes().data());
    m_program.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    m_program.gl.GenerateMipmap(GL_TEXTURE_2D);
    m_program.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_program.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_program.gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_program.gl.GenBuffers(1, &m_buffer);
    m_program.gl.GenVertexArrays(1, &m_vao);
    m_program.gl.BindVertexArray(m_vao);
    m_program.gl.BindBuffer(GL_ARRAY_BUFFER, m_buffer);
    for (GLuint i = 0; i < 4; ++i) {
        m_program.gl.EnableVertexAttribArray(i);
    }
    m_program.gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), nullptr);
    m_program.gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex),
                                     reinterpret_cast<void*>(offsetof(GlyphVertex, u)));
    m_program.gl.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex),
                                     reinterpret_cast<void*>(offsetof(GlyphVertex, r)));
    m_program.gl.VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex),
                                     reinterpret_cast<void*>(offsetof(GlyphVertex, radius)));
}
Hud::~Hud() {
    m_program.gl.DeleteTextures(1, &m_texture);
    m_program.gl.DeleteBuffers(1, &m_buffer);
    m_program.gl.DeleteVertexArrays(1, &m_vao);
}

void Hud::draw(const Match& match, const Menu& menu, int width, int height) {
    const auto& player = match.groups[0];
    const bool playing = menu.screen == Screen::Playing, result = menu.screen == Screen::Result;
    State state{.width = width, .height = height, .parts = {}, .scores = {}, .name = match.session.name, .menu = menu};
    if (playing || result) {
        state.seconds = static_cast<int>(std::ceil(match.remaining - 1e-9));
        state.speed = static_cast<int>(std::lround(100 * player.speedFraction()));
        state.speedTenths = static_cast<int>(std::lround(10 * length(player.flight)));
        for (const auto& cell : player.cells) {
            state.parts.push_back(cell.mass);
        }
        if (result) {
            for (const auto& g : match.groups) {
                state.scores.push_back(g.mass());
            }
        }
    }
    if (m_previous != state) {
        m_previous = state;
        std::array<Batch, colors.size()> layers;
        const MenuLayout layout(width, height);
        const double scale = playing ? std::clamp(std::min(width / 860.0, height / 620.0), .35, 1.6) : layout.scale;
        const double w = width / scale, h = height / scale;
        constexpr double margin = 24;
        const int sec = state.seconds % 60;
        const auto timer = number(state.seconds / 60) + U":" + (sec < 10 ? U"0" : U"") + number(sec);
        if (playing) {
            layers[Quiet].text(margin, 16, 13, U"МАССА");
            layers[Mint].text(margin, 31, 34, number(player.mass()));
            layers[Line].rect(121, 23, 1, 46);
            layers[Quiet].text(141, 16, 13, U"СКОРОСТЬ");
            layers[Bright].text(141, 32, 28, number(state.speed) + U"%");
            layers[Quiet].text(227, 44, 15,
                               number(state.speedTenths / 10) + U"," + number(state.speedTenths % 10) + U" ед/с");
            layers[Line].rect(141, 69, 83, 2);
            layers[Mint].rect(141, 69, 0.83 * state.speed, 2);
            if (state.parts.size() > 1) {
                const auto count = state.parts.size();
                std::u32string line = number(static_cast<int>(count)) + (count <= 4 ? U" клетки · " : U" клеток · ");
                double y = 84;
                for (std::size_t i = 0; i < count; ++i) {
                    auto part = (i ? U" + " : U"") + number(state.parts[i]);
                    if (textWidth(line + part, 14) > w - 2 * margin) {
                        layers[Quiet].text(margin, y, 14, line);
                        line.clear();
                        y += 19;
                    }
                    line += part;
                }
                layers[Quiet].text(margin, y, 14, line);
            }
            layers[Quiet].text(w - margin - textWidth(U"ДО КОНЦА", 13), 16, 13, U"ДО КОНЦА");
            layers[Bright].text(w - margin - textWidth(timer, 28), 32, 28, timer);
            const double cx = w / 2, cy = h / 2;
            layers[Bright].rect(cx - 6, cy - 0.6, 3.3, 1.2);
            layers[Bright].rect(cx + 2.7, cy - 0.6, 3.3, 1.2);
            layers[Bright].rect(cx - 0.6, cy - 6, 1.2, 3.3);
            layers[Bright].rect(cx - 0.6, cy + 2.7, 1.2, 3.3);
            layers[Quiet].text(margin, h - 34, 13, U"Мышь  обзор     W  тяга     Space  деление     Esc  пауза");
        } else {
            const Box last = menu.box(int(menu.rows().size()) - 1);
            if (result) {
                layers[Glass].rounded({225, 53, 510, 548}, 20);
            } else {
                const double bottom = last.y + last.h + (menu.notice.empty() ? 26 : 57);
                layers[Glass].rounded({48, 58, last.w + 52, bottom - 58}, 20);
            }
            if (result) {
                const auto order = match.ranking();
                const bool tie = order.size() > 1 && state.scores[order[1]] == state.scores[order[0]];
                const auto title =
                    player.cells.empty()
                        ? U"Вы поглощены"
                        : (order.size() == 1 ? U"Тренировка завершена"
                                             : (player.mass() == state.scores[order[0]] ? (tie ? U"Ничья" : U"Победа")
                                                                                        : U"Время вышло"));
                layers[Mint].rect(253, 83, 46, 3);
                layers[Bright].text(253, 93, 31, title);
                layers[Quiet].text(253, 147, 14, state.seconds ? U"Матч продолжается  " + timer : U"Итоги матча");
                int rank{};
                for (int row = 0; row < int(order.size()); ++row) {
                    const int owner = order[row], score = state.scores[owner];
                    if (row == 0 || score != state.scores[order[row - 1]]) {
                        rank = row + 1;
                    }
                    const double y = 192 + 34 * row;
                    layers[Quiet].text(258, y, 16, number(rank));
                    layers[owner ? Bright : Mint].text(310, y, 18,
                                                       owner ? U"Бот " + number(owner) : match.session.name);
                    auto value = number(score);
                    layers[Bright].text(696 - textWidth(value, 18), y, 18, value);
                }
            } else {
                const auto title = menu.screen == Screen::Home     ? U"Agario 3D"
                                   : menu.screen == Screen::Create ? U"Создать сессию"
                                   : menu.screen == Screen::Pause  ? U"Пауза"
                                                                   : U"Настройки";
                layers[Mint].rect(74, 80, 48, 3);
                layers[Bright].text(72, 99, menu.screen == Screen::Home ? 52 : 37, title);
                if (menu.screen == Screen::Home) {
                    layers[Quiet].text(76, 181, 16, U"Быстрый матч · 3 минуты · 5 ботов");
                }
                if (menu.screen == Screen::Create) {
                    const auto caption = menu.draft.name.empty() ? U"Игрок" : menu.draft.name;
                    layers[Bright].text(729 - textWidth(caption, 18) / 2, 453, 18, caption);
                    if (menu.draft.bots == 0) {
                        layers[Mint].text(76, 505, 15, U"Тренировка · еда и колючки");
                    }
                }
            }
            for (int i = 0; i < int(menu.rows().size()); ++i) {
                const auto a = menu.rows()[i];
                const Box r = menu.box(i);
                const bool active = i == menu.focus;
                layers[active ? Selected : Button].rounded(r, 12);
                layers[active ? Mint : Bright].text(r.x + 16, r.y + 9, 19, label(a));
                const auto value = menu.value(a);
                if (a == Action::Name) {
                    layers[Bright].text(r.x + r.w - 17 - textWidth(value, 17), r.y + 10, 17, value);
                } else if (!value.empty()) {
                    layers[Quiet].text(r.x + r.w - 171, r.y + 9, 18, U"<");
                    layers[Bright].text(r.x + r.w - 92 - textWidth(value, 16) / 2, r.y + 11, 16, value);
                    layers[Quiet].text(r.x + r.w - 22, r.y + 9, 18, U">");
                }
            }
            if (!menu.notice.empty()) {
                layers[Coral].text(75, last.y + last.h + 9, 14, menu.notice);
            }
            if (!result && menu.screen != Screen::Pause) {
                layers[Quiet].text(75, std::max(572.0, last.y + 66), 13,
                                   menu.typing ? U"До 16 символов · Enter  готово"
                                               : U"Стрелки  выбор     Enter  подтвердить");
            }
        }
        std::vector<GlyphVertex> vertices;
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const auto& batch = layers[i];
            const auto c = colors[i];
            for (const auto& tri : batch.mesh.triangles) {
                for (const auto index : tri.indices) {
                    const auto pos = batch.mesh.positions[index];
                    const auto uv = batch.uv[index];
                    vertices.push_back({float(pos[0] * scale + (playing ? 0 : layout.x)),
                                        float(pos[1] * scale + (playing ? 0 : layout.y)), float(uv[0]), float(uv[1]),
                                        float(c.red), float(c.green), float(c.blue), float(c.alpha),
                                        float(batch.radii[index] * scale)});
                }
            }
        }
        m_program.gl.BindBuffer(GL_ARRAY_BUFFER, m_buffer);
        m_program.gl.BufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GlyphVertex), vertices.data(),
                                GL_DYNAMIC_DRAW);
        m_count = GLsizei(vertices.size());
    }
    m_program.gl.Disable(GL_DEPTH_TEST);
    m_program.gl.Disable(GL_CULL_FACE);
    m_program.gl.Enable(GL_BLEND);
    m_program.gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_program.gl.UseProgram(m_program.id);
    m_program.gl.Uniform2f(m_screen, width, height);
    m_program.gl.ActiveTexture(GL_TEXTURE0);
    m_program.gl.BindTexture(GL_TEXTURE_2D, m_texture);
    m_program.gl.BindVertexArray(m_vao);
    m_program.gl.DrawArrays(GL_TRIANGLES, 0, m_count);
}
} // namespace agar
