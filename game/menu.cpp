#include "menu.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <memory>
#include <random>
#include <sstream>

namespace agar {
using enum Action;
namespace {
constexpr std::array home{Quick, Create, Action::Options, Exit};
constexpr std::array create{Name, Color, Minutes, Bots, Start, Back};
constexpr std::array options{Fullscreen, Vsync, Sensitivity, Invert, Apply, Cancel};
constexpr std::array paused{Resume, Again, Home};
constexpr std::array finished{Again, Home};
constexpr std::array<std::u32string_view, 5> hues{U"Бирюза", U"Коралл", U"Фиалка", U"Лазурь", U"Янтарь"};

} // namespace

std::string settingsPath() {
    std::unique_ptr<char, decltype(&SDL_free)> dir(SDL_GetPrefPath("Huynity", "Agario3D"), SDL_free);
    return dir ? std::string(dir.get()) + "settings.ini" : std::string{};
}

MenuLayout::MenuLayout(int w, int h)
    : scale(std::min(w / 960.0, h / 640.0)), x((w - 960 * scale) / 2), y((h - 640 * scale) / 2) {}

std::u32string number(int value) {
    auto text = std::to_string(value);
    return {text.begin(), text.end()};
}

std::u32string_view label(Action a) {
    static constexpr std::array<std::u32string_view, 19> labels{
        U"Быстрый матч", U"Создать сессию", U"Настройки", U"Выход",         U"Ник",
        U"Цвет клетки",  U"Длительность",   U"Боты",      U"Начать",        U"Назад",
        U"Режим окна",   U"VSync",          U"Мышь",      U"Инверсия Y",    U"Применить",
        U"Отмена",       U"Продолжить",     U"Ещё раз",   U"В главное меню"};
    return labels[static_cast<int>(a)];
}

void Menu::open(Screen next) {
    screen = next;
    focus = 0;
    typing = replaceName = false;
    notice.clear();
    if (next == Screen::Settings) {
        edit = settings;
    }
}
std::span<const Action> Menu::rows() const {
    switch (screen) {
    case Screen::Home:
        return home;
    case Screen::Create:
        return create;
    case Screen::Settings:
        return options;
    case Screen::Pause:
        return paused;
    case Screen::Result:
        return finished;
    default:
        return {};
    }
}
Box Menu::box(int i) const {
    if (screen == Screen::Result) {
        return {253.0 + i * 234, 535, 220, 44};
    }
    const bool form = screen == Screen::Settings || screen == Screen::Create;
    return {74,
            (form                      ? 165.0
             : screen == Screen::Pause ? 162.0
                                       : 236.0) +
                54 * i + (form && i >= 4 ? 14 : 0),
            form ? 425.0 : 325.0, 46};
}
int Menu::hit(double x, double y) const {
    for (int i = 0; i < int(rows().size()); ++i) {
        const auto r = box(i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}
bool Menu::adjust(Action a, int step) {
    switch (a) {
    case Color:
        draft.color = (draft.color + int(hues.size()) + step) % int(hues.size());
        break;
    case Minutes: {
        constexpr std::array times{3, 5, 10};
        const int current = int(std::ranges::find(times, draft.minutes) - times.begin());
        draft.minutes = times[(current + 3 + step) % 3];
        break;
    }
    case Bots:
        draft.bots = std::clamp(draft.bots + step, 0, 8);
        break;
    case Fullscreen:
        edit.fullscreen = !edit.fullscreen;
        break;
    case Vsync:
        edit.vsync = !edit.vsync;
        break;
    case Sensitivity:
        edit.sensitivity = std::clamp(edit.sensitivity + step, 1, 8);
        break;
    case Invert:
        edit.invertY = !edit.invertY;
        break;
    default:
        return false;
    }
    return true;
}
std::u32string Menu::value(Action a) const {
    switch (a) {
    case Name:
        return draft.name + (typing ? U"_" : U"");
    case Color:
        return std::u32string(hues[draft.color]);
    case Minutes:
        return number(draft.minutes) + U" мин";
    case Bots:
        return number(draft.bots);
    case Fullscreen:
        return edit.fullscreen ? U"Без рамки" : U"Оконный";
    case Vsync:
        return edit.vsync ? U"Вкл" : U"Выкл";
    case Sensitivity: {
        constexpr std::array values{U"0,25x", U"0,50x", U"0,75x", U"1,00x", U"1,25x", U"1,50x", U"1,75x", U"2,00x"};
        return values[edit.sensitivity - 1];
    }
    case Invert:
        return edit.invertY ? U"Вкл" : U"Выкл";
    default:
        return {};
    }
}

Settings loadSettings(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::unique_ptr<char, decltype(&SDL_free)> data(static_cast<char*>(SDL_LoadFile(path.c_str(), nullptr)), SDL_free);
    Settings s;
    if (!data) {
        return s;
    }
    std::istringstream input(data.get());
    if (!(input >> s.fullscreen >> s.vsync >> s.sensitivity >> s.invertY) || s.sensitivity < 1 || s.sensitivity > 8) {
        return {};
    }
    return s;
}
bool saveSettings(const Settings& s, const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::ostringstream out;
    out << s.fullscreen << ' ' << s.vsync << ' ' << s.sensitivity << ' ' << s.invertY << '\n';
    const auto bytes = out.str(), temp = path + ".tmp-" + std::to_string(std::random_device{}());
    if (SDL_SaveFile(temp.c_str(), bytes.data(), bytes.size()) && SDL_RenamePath(temp.c_str(), path.c_str())) {
        return true;
    }
    SDL_RemovePath(temp.c_str());
    return false;
}
} // namespace agar
