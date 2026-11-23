#include "huynity/adapters/window/window_factory.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "render/cell_renderer.h"

namespace {
using namespace agar;
constexpr int width = 480, height = 320;
using Pixel = std::array<GLubyte, 4>;

std::vector<Pixel> frame(CellRenderer& renderer, const Match& match, const Camera& camera, bool foreground = false) {
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (foreground) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(width / 2 - 4, height / 2 - 4, 8, 8);
        glClearColor(1, .5, 0, 1);
        glClearDepth(.2);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearDepth(1);
        glDisable(GL_SCISSOR_TEST);
    }
    renderer.draw(match, camera, width, height);
    std::vector<Pixel> pixels(width * height);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        throw std::runtime_error("OpenGL error drawing cells");
    }
    return pixels;
}

double silhouette(const std::vector<Pixel>& pixels) {
    int left = width, right = -1, top = height, bottom = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (pixels[y * width + x][1] > 32) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < left) {
        throw std::runtime_error("Empty player silhouette");
    }
    return double(right - left + 1) / (bottom - top + 1);
}

void rearView(CellRenderer& renderer) {
    Match match(Session{.bots = 0});
    match.food.clear();
    match.spikes.clear();
    Camera camera;
    camera.follow(match.groups[0], double(width) / height, 0);
    const double rest = silhouette(frame(renderer, match, camera));
    for (int i = 0; i < 300; ++i) {
        match.step({camera.forward, true}, FIXED_STEP);
        camera.follow(match.groups[0], double(width) / height, FIXED_STEP);
    }
    const double flying = silhouette(frame(renderer, match, camera));
    for (int i = 0; i < 180; ++i) {
        match.step({}, FIXED_STEP);
        camera.follow(match.groups[0], double(width) / height, FIXED_STEP);
    }
    const double settled = silhouette(frame(renderer, match, camera));
    std::cout << "Rear silhouette W/H: " << rest << " -> " << flying << " -> " << settled << '\n';
    if (std::abs(rest - 1) > .05 || flying < 1.35 || std::abs(settled - rest) > .05) {
        throw std::runtime_error("Flight/stop is not legible from the player's camera");
    }
}

void crossingSkins(CellRenderer& renderer) {
    Match match(Session{.bots = 1});
    match.food.clear();
    match.spikes.clear();
    match.groups[0].cells[0].position = {{-.69, 0, 6}};
    match.groups[1].cells[0].position = {{.69, 0, 6}};
    for (int i = 0; i < 6; ++i) {
        match.step({}, FIXED_STEP);
    }
    Camera camera;
    // Tiny yaw reverses center order while the actual overlap and its lighting barely move.
    camera.turn(-.01, 0);
    const auto before = frame(renderer, match, camera);
    camera.turn(.02, 0);
    const auto after = frame(renderer, match, camera);
    int jumps{};
    for (std::size_t i = 0; i < before.size(); ++i) {
        jumps += std::abs(before[i][0] - after[i][0]) > 45;
    }
    if (jumps > 20) {
        throw std::runtime_error("The entire contact patch changed layer at once");
    }
    const auto occluded = frame(renderer, match, camera, true);
    for (int y = height / 2 - 4; y < height / 2 + 4; ++y) {
        for (int x = width / 2 - 4; x < width / 2 + 4; ++x) {
            auto pixel = occluded[y * width + x];
            if (pixel[0] < 253 || std::abs(pixel[1] - 128) > 2 || pixel[2] > 2) {
                throw std::runtime_error("Cell film covered an opaque foreground object");
            }
        }
    }
}
} // namespace

int main() {
    auto window = huynity::adapters::window::createWindow({.size = {width, height}, .title = "Agario 3D render test"});
    if (!window->glProcResolver()) {
        return 77;
    }
    GlApi gl(window->glProcResolver());
    CellRenderer renderer(gl);
    rearView(renderer);
    crossingSkins(renderer);
}
