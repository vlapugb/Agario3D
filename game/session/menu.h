#pragma once

#include <span>

#include "core/match.h"

namespace agar {
enum class Screen {
    Home,
    Create,
    Settings,
    Playing,
    Pause,
    Result
};
enum class Action {
    Quick,
    Create,
    Options,
    Exit,
    Name,
    Color,
    Minutes,
    Bots,
    Start,
    Back,
    Fullscreen,
    Vsync,
    Sensitivity,
    Invert,
    Apply,
    Cancel,
    Resume,
    Again,
    Home
};

struct Settings {
    bool fullscreen{}, vsync{true}, invertY{};
    int sensitivity{4}; // Четверти исходной чувствительности.
    bool operator==(const Settings&) const = default;
};

struct MenuLayout {
    double scale, x, y;
    MenuLayout(int width, int height);
};

struct Box {
    double x, y, w, h;
};
struct Menu {
    Screen screen{Screen::Home};
    Session draft;
    Settings settings, edit;
    int focus{};
    bool typing{}, replaceName{};
    std::u32string notice;
    void open(Screen next);
    std::span<const Action> rows() const;
    Box box(int index) const;
    int hit(double x, double y) const;
    bool adjust(Action action, int step);
    std::u32string value(Action action) const;
    bool operator==(const Menu&) const = default;
};

std::u32string_view label(Action action);
std::u32string number(int value);
std::string settingsPath();
Settings loadSettings(const std::string& path);
bool saveSettings(const Settings& settings, const std::string& path);
} // namespace agar
