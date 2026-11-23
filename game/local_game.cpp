#include "local_game.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace agar {
namespace {
bool setDisplay(SDL_Window* window, const Settings& s) {
    bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (full != s.fullscreen && (!SDL_SetWindowFullscreenMode(window, nullptr) ||
                                 !SDL_SetWindowFullscreen(window, s.fullscreen) || !SDL_SyncWindow(window))) {
        return false;
    }
    int interval{};
    if (!SDL_GL_GetSwapInterval(&interval)) {
        interval = 0;
    }
    return (interval != 0) == s.vsync || SDL_GL_SetSwapInterval(s.vsync ? 1 : 0);
}

} // namespace

LocalGame::LocalGame(SDL_Window* w, std::string path) : window(w), preferences(std::move(path)) {
    menu.settings = loadSettings(preferences);
    if (!setDisplay(window, menu.settings)) {
        int interval{};
        SDL_GL_GetSwapInterval(&interval);
        menu.settings.vsync = interval != 0;
        menu.settings.fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    }
    width = 1280;
    height = 720;
    resize();
}
void LocalGame::resize() {
    SDL_GetWindowSizeInPixels(window, &width, &height);
}
void LocalGame::update(double dt) {
    if (menu.screen != Screen::Playing && menu.screen != Screen::Result) {
        return;
    }
    const bool split = splits > 0;
    splits -= split;
    match.step({camera.forward, thrust, split}, dt);
    camera.follow(match.groups.front(), aspect(), dt);
    if (menu.screen == Screen::Playing && match.resultVisible()) {
        open(Screen::Result);
    }
}
void LocalGame::event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        running = false;
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        resize();
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        wHeld = false;
        if (menu.screen == Screen::Playing) {
            open(Screen::Pause);
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (menu.typing && e.text.text) {
            const char* cursor = e.text.text;
            while (*cursor) {
                const auto c = static_cast<char32_t>(SDL_StepUTF8(&cursor, nullptr));
                if (c < U' ' || c == 127) {
                    continue;
                }
                if (menu.replaceName) {
                    menu.draft.name.clear();
                    menu.replaceName = false;
                }
                if (menu.draft.name.size() < 16) {
                    menu.draft.name.push_back(c);
                }
            }
        }
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        key(e.key);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (menu.screen == Screen::Playing) {
            if (ignoreMotion || e.motion.timestamp < captureTime) {
                ignoreMotion = false;
                break;
            }
            const double gain = menu.settings.sensitivity / 4.0;
            camera.turn(e.motion.xrel * gain, e.motion.yrel * gain * (menu.settings.invertY ? -1 : 1));
        } else {
            point(e.motion.x, e.motion.y, false);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT && menu.screen != Screen::Playing) {
            point(e.button.x, e.button.y, true);
        }
        break;
    }
}

double LocalGame::aspect() const {
    return double(std::max(1, width)) / std::max(1, height);
}
void LocalGame::textEntry(bool on) {
    if (menu.typing == on) {
        return;
    }
    if (on) {
        if (!SDL_StartTextInput(window)) {
            throw std::runtime_error(SDL_GetError());
        }
    } else {
        SDL_StopTextInput(window);
    }
    menu.typing = menu.replaceName = on;
}
void LocalGame::open(Screen s) {
    textEntry(false);
    menu.open(s);
    thrust = false;
    splits = 0;
    const bool capture = s == Screen::Playing;
    if (!SDL_SetWindowRelativeMouseMode(window, capture)) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
    captureTime = SDL_GetTicksNS();
    ignoreMotion = capture;
    waitForWRelease = capture && (wHeld || SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_W]);
}
void LocalGame::start(Session options) {
    if (options.name.empty()) {
        options.name = U"Игрок";
    }
    match = Match(std::move(options));
    camera = {};
    camera.follow(match.groups.front(), aspect(), 0);
    open(Screen::Playing);
}
void LocalGame::activate(Action action, int direction) {
    if (action == Action::Name) {
        textEntry(!menu.typing);
        return;
    }
    textEntry(false);
    if (menu.adjust(action, direction)) {
        return;
    }
    switch (action) {
    case Action::Quick:
        start({});
        break;
    case Action::Start:
        start(menu.draft);
        break;
    case Action::Again:
        start(match.session);
        break;
    case Action::Create:
        open(Screen::Create);
        break;
    case Action::Options:
        open(Screen::Settings);
        break;
    case Action::Apply:
        if (setDisplay(window, menu.edit) && saveSettings(menu.edit, preferences)) {
            menu.settings = menu.edit;
            open(Screen::Home);
        } else {
            setDisplay(window, menu.settings);
            menu.notice = U"Не удалось применить настройки";
            SDL_Log("Settings: %s", SDL_GetError());
        }
        break;
    case Action::Resume:
        open(Screen::Playing);
        break;
    case Action::Exit:
        running = false;
        break;
    default:
        open(Screen::Home);
        break;
    }
}
void LocalGame::key(const SDL_KeyboardEvent& e) {
    const auto code = e.scancode;
    if (e.type == SDL_EVENT_KEY_UP) {
        if (code == SDL_SCANCODE_W) {
            thrust = false;
            wHeld = false;
            waitForWRelease = false;
        }
        return;
    }
    if (code == SDL_SCANCODE_W) {
        wHeld = true;
    }
    if (e.repeat && !(menu.typing && code == SDL_SCANCODE_BACKSPACE)) {
        return;
    }
    if (menu.screen == Screen::Playing) {
        if (code == SDL_SCANCODE_W && !waitForWRelease) {
            thrust = true;
        }
        if (code == SDL_SCANCODE_SPACE) {
            ++splits;
        }
        if (code == SDL_SCANCODE_ESCAPE) {
            open(Screen::Pause);
        }
        return;
    }
    if (menu.typing) {
        if (code == SDL_SCANCODE_BACKSPACE && !menu.draft.name.empty()) {
            if (menu.replaceName) {
                menu.draft.name.clear();
            } else {
                menu.draft.name.pop_back();
            }
            menu.replaceName = false;
        }
        if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_ESCAPE) {
            textEntry(false);
            return;
        }
        if (code != SDL_SCANCODE_TAB) {
            return;
        }
        textEntry(false);
    }
    if (code == SDL_SCANCODE_ESCAPE) {
        if (menu.screen == Screen::Pause) {
            open(Screen::Playing);
        } else if (menu.screen != Screen::Home) {
            open(Screen::Home);
        }
        return;
    }
    if (code == SDL_SCANCODE_R && menu.screen == Screen::Result) {
        start(match.session);
        return;
    }
    auto rows = menu.rows();
    if (rows.empty()) {
        return;
    }
    const int n = int(rows.size());
    if (code == SDL_SCANCODE_UP || (code == SDL_SCANCODE_TAB && (e.mod & SDL_KMOD_SHIFT))) {
        menu.focus = (menu.focus + n - 1) % n;
    } else if (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_TAB) {
        menu.focus = (menu.focus + 1) % n;
    } else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT) {
        const int step = code == SDL_SCANCODE_RIGHT ? 1 : -1;
        if (menu.screen == Screen::Result) {
            menu.focus = (menu.focus + n + step) % n;
        } else {
            menu.adjust(rows[menu.focus], step);
        }
    } else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER || code == SDL_SCANCODE_SPACE) {
        activate(rows[menu.focus]);
    }
}
void LocalGame::point(float x, float y, bool click) {
    int logicalW{}, logicalH{};
    if (!SDL_GetWindowSize(window, &logicalW, &logicalH) || logicalW <= 0 || logicalH <= 0 || width <= 0 ||
        height <= 0) {
        return;
    }
    const MenuLayout layout(width, height);
    const double px = (x * width / logicalW - layout.x) / layout.scale,
                 py = (y * height / logicalH - layout.y) / layout.scale;
    const int i = menu.hit(px, py);
    if (i < 0) {
        if (click) {
            textEntry(false);
        }
        return;
    }
    if (!click && menu.typing) {
        return;
    }
    menu.focus = i;
    if (click) {
        const auto r = menu.box(i);
        activate(menu.rows()[i], px >= r.x + r.w - 190 && px < r.x + r.w - 112 ? -1 : 1);
    }
}
} // namespace agar
