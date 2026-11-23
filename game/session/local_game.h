#pragma once

#include <SDL3/SDL.h>

#include "core/camera.h"
#include "menu.h"

namespace agar {
class LocalGame {
public:
    explicit LocalGame(SDL_Window* window, std::string preferences = settingsPath());
    void update(double dt);
    void event(const SDL_Event& event);

    Menu menu;
    Match match;
    Camera camera;
    int width{}, height{};
    bool running{true};

private:
    void resize();
    double aspect() const;
    void textEntry(bool on);
    void open(Screen screen);
    void start(Session options);
    void activate(Action action, int direction = 1);
    void key(const SDL_KeyboardEvent& event);
    void point(float x, float y, bool click);

    SDL_Window* window;
    std::string preferences;
    int splits{};
    bool thrust{}, wHeld{}, waitForWRelease{}, ignoreMotion{};
    Uint64 captureTime{};
};
} // namespace agar
