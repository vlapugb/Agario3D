#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <stdexcept>

#include "session/local_game.h"

namespace {
using namespace agar;
void check(bool ok, std::source_location where = std::source_location::current()) {
    if (!ok) {
        throw std::runtime_error(std::string(where.function_name()) + " line " + std::to_string(where.line()));
    }
}
void key(LocalGame& game, SDL_Scancode code, bool down = true, bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = code;
    e.key.repeat = repeat;
    game.event(e);
}
void click(LocalGame& game, Action action, bool decrease = false) {
    const auto rows = game.menu.rows();
    const auto it = std::ranges::find(rows, action);
    check(it != rows.end());
    const auto rect = game.menu.box(int(it - rows.begin()));
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = rect.x + rect.w - (decrease ? 162 : 12);
    e.button.y = rect.y + rect.h / 2;
    game.event(e);
}
void move(LocalGame& game, int x, int y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.timestamp = SDL_GetTicksNS();
    e.motion.xrel = x;
    e.motion.yrel = y;
    game.event(e);
}
void run(SDL_Window* window, const std::string& path) {
    LocalGame game(window, path);
    check(game.menu.screen == Screen::Home);
    click(game, Action::Options);
    click(game, Action::Sensitivity);
    click(game, Action::Invert);
    click(game, Action::Cancel);
    check(game.menu.settings.sensitivity == 4 && !game.menu.settings.invertY);
    click(game, Action::Options);
    for (int i = 0; i < 15; ++i) {
        click(game, Action::Sensitivity, true);
    }
    check(game.menu.edit.sensitivity == 1);
    for (int i = 0; i < 15; ++i) {
        click(game, Action::Sensitivity);
    }
    click(game, Action::Invert);
    click(game, Action::Apply);
    check(game.menu.screen == Screen::Home && game.menu.settings.sensitivity == 8);
    check(LocalGame(window, path).menu.settings == game.menu.settings);

    click(game, Action::Create);
    click(game, Action::Name);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "ПилотБирюзовойОрбиты";
    game.event(text);
    check(game.menu.draft.name == U"ПилотБирюзовойОр");
    key(game, SDL_SCANCODE_BACKSPACE);
    text.text.text = "Ё";
    game.event(text);
    key(game, SDL_SCANCODE_RETURN);
    check(game.menu.draft.name.size() == 16 && game.menu.draft.name.back() == U'Ё');
    click(game, Action::Color);
    click(game, Action::Minutes);
    for (int i = 0; i < 7; ++i) {
        click(game, Action::Bots, true);
    }
    click(game, Action::Start);
    const auto options = game.match.session;
    check(game.menu.screen == Screen::Playing && options.color == 1 && game.match.groups.size() == 1 &&
          game.match.remaining == 300);
    check(SDL_GetWindowRelativeMouseMode(window));

    // Большая дельта при захвате курсора отбрасывается, следующая применяет 2x и инверсию Y.
    move(game, 900, 800);
    check(game.camera.yaw == 0 && game.camera.pitch == 0);
    move(game, 20, 10);
    check(std::abs(game.camera.yaw - .092) < 1e-7 && std::abs(game.camera.pitch - .046) < 1e-7);
    key(game, SDL_SCANCODE_W);
    for (int i = 0; i < 300; ++i) {
        game.update(1. / 60);
    }
    check(std::abs(length(game.match.groups[0].flight) - BASE_SPEED) < 1e-8);
    key(game, SDL_SCANCODE_ESCAPE);
    check(!SDL_GetWindowRelativeMouseMode(window));
    auto position = game.match.groups[0].center();
    const auto time = game.match.remaining;
    const auto mass = game.match.groups[0].mass();
    const double loss = game.match.groups[0].cells[0].pendingLoss;
    for (int i = 0; i < 3600; ++i) {
        game.update(1. / 60);
    }
    check(game.match.remaining == time && game.match.groups[0].center() == position);
    check(loss > 0 && game.match.groups[0].mass() == mass && game.match.groups[0].cells[0].pendingLoss == loss);
    auto yaw = game.camera.yaw;
    move(game, -710, 640);
    key(game, SDL_SCANCODE_ESCAPE);
    move(game, 500, 400);
    check(game.camera.yaw == yaw && SDL_GetWindowRelativeMouseMode(window));
    key(game, SDL_SCANCODE_W, true, true);
    for (int i = 0; i < 121; ++i) {
        game.update(1. / 60);
    }
    check(length(game.match.groups[0].flight) < 1e-8);
    key(game, SDL_SCANCODE_W, false);
    key(game, SDL_SCANCODE_W);
    game.update(1. / 60);
    check(length(game.match.groups[0].flight) > 0);
    auto& cell = game.match.groups[0].cells[0];
    cell.mass += 17;
    cell.skin.contact(-1, {{1, 0, 0}}, BASE_SPEED, {}, 1);
    cell.skin.advance(1. / 60);
    game.match.remaining = 1. / 60;
    game.update(1. / 60);
    check(game.menu.screen == Screen::Result);
    click(game, Action::Again);
    check(game.match.session == options && game.match.remaining == 300 && game.match.groups[0].mass() == 40);
    check(game.match.groups[0].cells[0].pendingLoss == 0 && squared(game.match.groups[0].flight) == 0);
    check(!game.match.groups[0].cells[0].skin.patches[0].key && game.camera.yaw == 0);
    key(game, SDL_SCANCODE_ESCAPE);
    click(game, Action::Home);
    click(game, Action::Quick);
    check(game.match.groups.size() == 6 && game.match.remaining == 180 && game.match.session == Session{});
    key(game, SDL_SCANCODE_ESCAPE);
    click(game, Action::Home);
    click(game, Action::Exit);
    check(!game.running);
}
} // namespace
int main() {
    const auto path =
        std::filesystem::temp_directory_path() / ("agar-session-" + std::to_string(std::random_device{}()) + ".ini");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }
    auto* window = SDL_CreateWindow("Session test", 960, 640, SDL_WINDOW_HIDDEN);
    int result = 0;
    try {
        check(window != nullptr);
        run(window, path.string());
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::filesystem::remove(path);
    return result;
}
