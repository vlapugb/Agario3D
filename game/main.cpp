#include "huynity/adapters/window/window_factory.h"
#include "huynity/runtime/fixed_step_update.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>

#include "render/view.h"
#include "session/local_game.h"

int main() {
    try {
        auto surface = huynity::adapters::window::createWindow({.size = {1280, 720}, .title = "Agario 3D · Huynity"});
        auto* window = SDL_GL_GetCurrentWindow();
        if (!window) {
            throw std::runtime_error("Для игры требуется OpenGL 3.3 Core.");
        }
        agar::LocalGame game(window);
        agar::View view(surface->glProcResolver());
        huynity::runtime::FixedStepUpdate clock(agar::FIXED_STEP, 8);
        auto previous = SDL_GetTicksNS();
        while (game.running) {
            const auto now = SDL_GetTicksNS();
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                game.event(e);
            }
            if (!game.running) {
                break;
            }
            clock.advance(std::min(.12, (now - previous) * 1e-9), [&](double dt) { game.update(dt); });
            previous = now;
            if (game.width > 0 && game.height > 0) {
                view.draw(game.match, game.camera, game.menu, game.width, game.height);
                surface->swapBuffers();
            }
            SDL_Delay(1);
        }
    } catch (const std::exception& error) {
        std::cerr << "Agario 3D: " << error.what() << '\n';
        return 1;
    }
}
