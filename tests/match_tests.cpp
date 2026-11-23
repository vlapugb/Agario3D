#include "huynity/runtime/fixed_step_update.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>

#include "core/camera.h"
#include "core/match.h"
#include "core/shell.h"

namespace {
using namespace agar;
constexpr double dt = FIXED_STEP;
constexpr Command thrust{{{0, 0, 1}}, true};
constexpr Command launch{{{0, 0, 1}}, true, true};

void check(bool condition, std::source_location where = std::source_location::current()) {
    if (!condition) {
        throw std::runtime_error(std::string(where.function_name()) + " line " + std::to_string(where.line()));
    }
}

void near(double a, double b, double tolerance = 1e-8, std::source_location where = std::source_location::current()) {
    if (!(std::abs(a - b) <= tolerance)) {
        std::cerr << a << " expected " << b << '\n';
        check(false, where);
    }
}

Match emptyArena(int mass = 40) {
    Match m;
    for (auto& g : m.groups) {
        g.cells.clear();
    }
    m.food.clear();
    m.spikes.clear();
    m.groups[0].cells.push_back({.id = 10000, .mass = mass});
    return m;
}

void ticks(Match& m, Command command, int count) {
    for (int i = 0; i < count; ++i) {
        m.step(command, dt);
    }
}

double gap(const Group& g) {
    check(g.cells.size() == 2);
    return length(g.cells[1].position - g.cells[0].position);
}

void flight() {
    Match m;
    auto& g = m.groups[0];
    ticks(m, thrust, 299);
    near(length(g.flight), BASE_SPEED * 299 / 300);
    m.step(thrust, dt);
    near(length(g.flight), BASE_SPEED);
    near(cohesion(1), 0);
    near(cohesion(.5), 0.5);
    near(cohesion(.9), 0.028);
    m.step(launch, dt);
    check(g.cells[1].impulse[2] > 12 && squared(g.cells[0].impulse) == 0);
    ticks(m, thrust, 299);
    check(squared(g.cells[1].impulse) == 0);
    const double cruising = gap(g);
    std::cout << std::fixed << std::setprecision(6) << "Split +5s   z = " << g.cells[0].position[2] << " / "
              << g.cells[1].position[2] << "   gap = " << cruising << '\n';
    for (int i = 0; i < 780; ++i) {
        m.step(thrust, dt);
        near(gap(g), cruising);
        near(g.speedFraction(), 1);
    }
    std::cout << "Split +18s  z = " << g.cells[0].position[2] << " / " << g.cells[1].position[2]
              << "   gap = " << gap(g) << '\n';
    check(g.cells[0].cooldown == 0 && g.cells[1].cooldown == 0 && g.fusions.empty());
    ticks(m, {}, 60);
    near(length(g.flight), g.speedLimit() / 2);
    const double braking = gap(g);
    check(braking < cruising);
    ticks(m, {}, 60);
    near(length(g.flight), 0);
    check(gap(g) < braking);
    std::cout << "Brake +2s   z = " << g.cells[0].position[2] << " / " << g.cells[1].position[2]
              << "   gap = " << gap(g) << '\n';
    const Vec stopped = g.center();
    const int joiningMass = g.mass();
    check(joiningMass == 39);
    int fusionTicks{};
    for (int i = 0; i < 300 && g.cells.size() > 1; ++i) {
        if (!g.fusions.empty()) {
            ++fusionTicks;
        }
        m.step({}, dt);
        near(length(g.center() - stopped), 0);
        check(g.mass() == joiningMass);
    }
    check(g.cells.size() == 1 && g.fusions.empty() && g.cells.front().mass == joiningMass);
    const double duration = BASE_MERGE_DURATION * std::sqrt(joiningMass / 40.0);
    near((fusionTicks + 1) * dt, std::ceil(duration / dt) * dt);
    std::cout << "Fusion " << (fusionTicks + 1) * dt << " s   z = " << g.center()[2] << "   mass = " << g.mass()
              << '\n';
    ticks(m, thrust, 100);
    ticks(m, {}, 12);
    near(g.speedFraction(), 100.0 / 300 - 12.0 / 120);
    m.step(thrust, dt);
    near(g.speedFraction(), 101.0 / 300 - 12.0 / 120);
}

void motionSkin() {
    auto m = emptyArena();
    auto& group = m.groups[0];
    ticks(m, {}, 60);
    check(group.cells[0].skin.flow == Vec{} && group.cells[0].skin.lag == Vec{});
    ticks(m, thrust, 300);
    near(length(group.cells[0].skin.travel), BASE_SPEED);
    check(group.cells[0].skin.flow[2] > .98 * BASE_SPEED);
    ticks(m, {{{1, 0, 0}}, true}, 12);
    const auto& skin = group.cells[0].skin;
    check(skin.travel[0] > skin.flow[0] && skin.lag[0] < 0);
    ticks(m, {}, 35);
    check(dot(skin.lag, skin.travel) > 0);
    for (int i = 0; i < 120 && length(skin.travel) > 1e-8; ++i) {
        m.step({}, dt);
    }
    check(length(skin.travel) < 1e-8);
    const Vec recoil = skin.lag;
    check(length(recoil) > .003);
    bool crossed = false;
    for (int i = 0; i < 59; ++i) {
        m.step({}, dt);
        crossed |= dot(recoil, skin.lag) < 0;
    }
    check(crossed && length(skin.lag) < .002 && length(skin.flow) < .001);

    auto split = emptyArena();
    split.step(launch, dt);
    check(split.groups[0].cells[1].skin.flow[2] > 1);
    check(length(split.groups[0].cells[0].skin.flow) < .25 * split.groups[0].cells[1].skin.flow[2]);
}

void massAndFlight() {
    near(emptyArena(1).groups[0].speedLimit(), 7.83);
    near(emptyArena(2000).groups[0].speedLimit(), 2.61);
    double previousSpeed = 10;
    for (int mass : {20, 40, 80, 160, 640}) {
        auto m = emptyArena(mass);
        auto& g = m.groups.front();
        const double initial = g.speedLimit();
        near(initial, 5.8 * std::pow(40.0 / mass, .25));
        check(initial < previousSpeed);
        previousSpeed = initial;
        for (int i = 1; i <= 300; ++i) {
            m.step(thrust, dt);
            near(g.speedFraction(), i / 300.0);
        }
        near(g.cells.front().skin.travel[2], g.speedLimit());
        for (int i = 1; i <= 120; ++i) {
            m.step({}, dt);
            near(g.speedFraction(), 1 - i / 120.0);
        }
    }
    auto paired = emptyArena(160);
    auto& bot = paired.groups[1];
    bot.cells.push_back({20001, 160, {{80, 0, 0}}});
    paired.food.push_back({{{80, 0, 115}}, 5});
    for (int i = 0; i < 300; ++i) {
        paired.step(thrust, dt);
        near(length(paired.groups[0].flight - bot.flight), 0);
        near(paired.groups[0].center()[2], bot.center()[2]);
    }
    near(bot.speedFraction(), 1);

    auto m = emptyArena(160);
    auto& g = m.groups.front();
    ticks(m, thrust, 300);
    const auto speed = g.speedLimit();
    const int splitMass = g.mass();
    m.step(launch, dt);
    near(g.speedLimit(), speed);
    ticks(m, thrust, 300);
    const double separation = gap(g);
    const int mass = g.mass();
    for (int i = 0; i < 780; ++i) {
        m.step(thrust, dt);
        near(g.speedFraction(), 1);
        near(gap(g), separation);
    }
    check(g.mass() < mass);
    ticks(m, {}, 120);
    check(gap(g) < separation);
    std::cout << "At the split, mass " << splitMass << " gives " << speed << " units/s and a cruise gap of "
              << separation << '\n';
}

void massAndFusion() {
    auto approach = [](int left, int right) {
        auto m = emptyArena(left);
        auto& g = m.groups[0];
        g.cells.push_back({10001, right, {{12, 0, 0}}});
        const Vec center = g.center();
        ticks(m, {}, 30);
        near(length(g.center() - center), 0);
        return gap(g) / 12;
    };
    const auto light = approach(20, 20), mixed = approach(20, 80), heavy = approach(80, 80);
    check(light < mixed && mixed < heavy && heavy < 1);
    auto merge = [](int mass, double seconds) {
        auto m = emptyArena(mass);
        auto& g = m.groups[0];
        g.cells.push_back({10001, mass, {{1.95 * radius(mass), 0, 0}}});
        // Тот же порог готовности при разных абсолютных скоростях.
        g.flight = thrust.direction * (.31 * g.speedLimit());
        m.step(thrust, dt);
        check(g.fusions.empty());
        g.flight = {};
        g.cells[1].position = g.cells[0].position + Vec{{1.95 * radius(mass), 0, 0}};
        const int steps = int(std::ceil(seconds / dt - 1e-10));
        for (int i = 1; i <= steps; ++i) {
            if (i == 5) {
                m.food.push_back({g.cells.front().position, 5});
            }
            m.step({}, dt);
            if (i < steps) {
                check(g.fusions.size() == 1);
                near(g.fusions[0].duration, seconds);
                near(g.fusions[0].progress(), i * dt / seconds);
            }
        }
        check(g.fusions.empty() && g.cells.size() == 1 && g.mass() == 2 * mass + 5);
    };
    merge(20, .60);
    merge(80, 1.20);
    std::cout << "Fusion takes 0.60 s for 20 + 20 and 1.20 s for 80 + 80. Gap fractions after 0.5 s are " << light
              << " / " << heavy << '\n';
}

void passiveLoss() {
    auto trial = [](int fps) {
        auto m = emptyArena(150);
        auto& player = m.groups[0];
        player.cells[0].cooldown = 180;
        player.cells.push_back({10001, 50, {{12, 0, 0}}, {}, 180});
        auto& bot = m.groups[1];
        bot.cells.push_back({20001, 200, {{90, 0, 0}}});
        huynity::runtime::FixedStepUpdate clock(dt, 8);
        std::vector<int> trace;
        for (int frame = 1; frame <= fps * 120; ++frame) {
            clock.advance(1. / fps, [&](double step) {
                m.step({}, step);
                check(player.mass() == bot.mass());
                trace.push_back(player.mass());
            });
            if (frame == fps * 60) {
                check(player.cells[0].mass == 135 && player.cells[1].mass == 45 && bot.mass() == 180);
            }
        }
        check(player.mass() == 162 && bot.mass() == 162);
        return trace;
    };
    const auto trace = trial(60);
    check(trace.size() == 7200 && trace == trial(30) && trace == trial(120));

    auto exact = [](const Group& g) {
        double mass{};
        for (const auto& c : g.cells) {
            mass += c.mass - c.pendingLoss;
        }
        return mass;
    };
    auto m = emptyArena(120);
    auto& g = m.groups[0];
    ticks(m, {}, 220);
    const double perStep = std::pow(.90, dt / 60);
    double expected = 120 * std::pow(.90, 220 * dt / 60);
    near(exact(g), expected);
    m.step({.split = true}, dt);
    expected *= perStep;
    check(g.cells.size() == 2);
    near(exact(g), expected);
    for (auto& c : g.cells) {
        c.cooldown = 0;
        c.impulse = {};
    }
    g.cells[1].position = g.cells[0].position;
    ticks(m, {}, 90);
    expected *= std::pow(perStep, 90);
    check(g.cells.size() == 1);
    near(exact(g), expected);
    m.spikes.push_back({g.center()});
    m.step({}, dt);
    expected = expected * perStep + 100;
    check(g.cells.size() == 4 && !m.spikes[0].active);
    near(exact(g), expected);

    auto eating = emptyArena(80);
    auto& hunter = eating.groups[0];
    eating.groups[1].cells = {{20001, 40, {{80, 0, 0}}}};
    ticks(eating, {}, 240);
    eating.groups[1].cells[0].position = hunter.center();
    eating.step({}, dt);
    check(eating.groups[1].cells.empty());
    near(exact(hunter), 120 * std::pow(perStep, 241));

    auto tiny = emptyArena(2);
    tiny.remaining = 600;
    ticks(tiny, {}, 36000);
    check(tiny.groups[0].mass() == 1 && tiny.groups[0].cells[0].pendingLoss == 0);
    std::cout << "At 30/60/120 FPS, 150 + 50 and 200 both reach 180 at 60 s and 162 at 120 s\n";
}

void splitting() {
    for (int mass : {35, 36, 37, 72}) {
        auto m = emptyArena(mass);
        auto& g = m.groups[0];
        m.step({.split = true}, dt);
        check(g.cells.size() == (mass >= 36 ? 2 : 1) && g.mass() == mass);
        near(length(g.flight), 0);
        if (mass >= 36) {
            check(g.cells[0].mass == mass / 2 && g.cells[1].mass == (mass + 1) / 2);
            near(g.cells[1].cooldown, 12 + 0.15 * g.cells[1].mass - dt);
            m.food.push_back({g.cells[0].position, 5});
            const auto old = g.cells[0].cooldown;
            m.step({}, dt);
            near(g.cells[0].cooldown, old - dt);
            check(g.cells[0].mass == mass / 2 + 5 && g.cells[1].mass == (mass + 1) / 2);
        }
        if (mass == 72) {
            m.step({.split = true}, dt);
            check(g.cells.size() == 4);
        }
    }
    auto m = emptyArena(38);
    auto& g = m.groups[0];
    for (int i = 1; i < 15; ++i) {
        g.cells.push_back({10000 + i, i == 8 ? 99 : 36, {{5.0 * i, 0, 0}}});
    }
    const auto total = g.mass();
    m.step({.split = true}, dt);
    check(g.cells.size() == 16 && g.mass() == total && g.cells[0].mass == 38 && g.cells[8].mass == 49);
    m.step({.split = true}, dt);
    check(g.cells.size() == 16 && g.mass() == total);
}

void spikes() {
    for (int mass : {99, 100, 103}) {
        for (int used : {1, 14, 15, 16}) {
            auto m = emptyArena(mass);
            auto& g = m.groups[0];
            for (int i = 1; i < used; ++i) {
                g.cells.push_back({10000 + i, 18, {{4.0 * i, 20, 0}}, {}, 20});
            }
            m.spikes.push_back({{}});
            const int before = g.mass();
            m.step({}, dt);
            const bool eaten = mass >= 100 && used < 16;
            check(m.spikes[0].active != eaten && g.mass() == before + (eaten ? 100 : 0));
            check(g.cells.size() == static_cast<std::size_t>(eaten ? std::min(16, used + 3) : used));
            if (eaten) {
                const int n = std::min(4, 17 - used);
                for (int i = 0; i < n; ++i) {
                    const auto& part = g.cells[i == 0 ? 0 : used + i - 1];
                    check(part.mass == (100 + mass) / n + (i < (100 + mass) % n));
                    near(part.cooldown, 12 + 0.15 * part.mass);
                    check(length(part.impulse) > 7);
                }
            } else {
                check(m.spikes[0].position == Vec{});
            }
        }
    }
}

void spikePhysics() {
    const double scale = radius(Spike::mass);
    const auto& needle = Spike::crown()[12];
    const Vec along = unit(cross(needle.axis, {{0, 1, 0}}));
    auto graze = [&](int fps) {
        auto m = emptyArena(100);
        auto& g = m.groups[0];
        auto& cell = g.cells[0];
        auto& spike = m.spikes.emplace_back();
        const Vec middle = needle.axis * (scale * (1 + length(needle.tip) - .13));
        cell.position = middle - along * 2.5;
        const double limit = g.speedLimit();
        g.flight = along * limit;
        huynity::runtime::FixedStepUpdate clock(dt, 8);
        std::vector<std::array<double, 5>> trace;
        for (int frame = 0; frame < 2 * fps; ++frame) {
            clock.advance(1. / fps, [&](double step) {
                m.step({along, true}, step);
                check(spike.active && cell.mass == 100 && g.cells.size() == 1);
                near(length(g.flight - along * limit), 0);
                check(dot(cell.skin.travel, along) > .8 * limit);
                const auto bend = spike.bends[12].offset();
                double dent{}, remote{};
                for (const auto& p : cell.skin.patches) {
                    if (needleContact(p.key)) {
                        dent = std::max(dent, p.dent.value);
                    }
                }
                for (int i = 0; i < Spike::needleCount; ++i) {
                    const double amount = length(spike.bends[i].offset());
                    check(amount <= Spike::maxBend + 1e-10);
                    if (dot(Spike::crown()[i].axis, needle.axis) < 0) {
                        remote = std::max(remote, amount);
                    }
                }
                near(remote, 0);
                trace.push_back({dot(bend, along), length(bend), dent, spike.flexing ? 1. : 0.,
                                 limit - dot(cell.skin.travel, along)});
            });
        }
        return trace;
    };
    const auto trace = graze(60);
    check(trace.size() == 120 && graze(30) == trace && graze(120) == trace);
    const auto peak = *std::ranges::max_element(trace, {}, [](auto f) { return f[1]; });
    check(peak[1] > .05 && trace.back()[3] == 0);
    check(std::ranges::any_of(trace, [](auto f) { return f[0] > .035 && f[2] > .025; }));
    check(std::ranges::any_of(trace, [](auto f) { return f[0] < -.003; }));
    check(std::ranges::any_of(trace, [](auto f) { return f[4] > .06; }) && std::abs(trace.back()[4]) < .025);

    auto divided = emptyArena(70);
    auto& siblings = divided.groups[0];
    siblings.cells[0].position[2] = -3;
    siblings.cells.push_back({11000, 70, {{8, 0, -3}}});
    const double dividedLimit = siblings.speedLimit();
    siblings.flight = thrust.direction * dividedLimit;
    divided.spikes.emplace_back();
    double resistance{};
    for (int i = 1; i <= 90; ++i) {
        divided.step(thrust, dt);
        resistance = std::max(resistance, length(siblings.cells[0].impulse));
        near(length(siblings.cells[1].impulse), 0);
        near(siblings.cells[1].position[2], -3 + i * dt * dividedLimit);
    }
    check(resistance > .02 && divided.spikes[0].active && siblings.mass() == 140 && siblings.cells.size() == 2);

    // Малая клетка проходит сквозь ядро. Еда внутри контакта не вызывает разрыв задним числом.
    auto pass = emptyArena(99);
    auto& g = pass.groups[0];
    auto& spike = pass.spikes.emplace_back();
    g.cells[0].position = {{0, 0, -3}};
    const double passLimit = g.speedLimit();
    g.flight = thrust.direction * passLimit;
    ticks(pass, thrust, 30);
    check(spike.active && g.mass() == 99 && !spike.passing.empty());
    pass.food.push_back({g.center(), 1});
    ticks(pass, thrust, 1);
    pass.food.clear();
    ticks(pass, thrust, 45);
    check(spike.active && g.mass() == 100 && spike.passing.empty());
    near(g.center()[2], -3 + 76 * dt * passLimit, .25);
    g.flight = thrust.direction * -g.speedLimit();
    for (int i = 0; i < 100 && spike.active; ++i) {
        pass.step({thrust.direction * -1, true}, dt);
    }
    check(!spike.active && g.cells.size() == 4 && g.mass() == 200);

    // Направление отдачи задаёт нормаль в точке входа, дрейф — импульс клетки плюс покоящаяся колючка.
    auto burst = [&](Vec heading, Vec miss, double speed) {
        auto m = emptyArena(103);
        auto& group = m.groups[0];
        auto& thorn = m.spikes.emplace_back();
        group.cells[0].position = miss - heading * 3.0;
        group.flight = heading * speed;
        const double reach = radius(103) + scale * Spike::core;
        for (int i = 0; i < 250 && thorn.active; ++i) {
            const Vec prev = group.center();
            const Vec impulse = group.cells[0].impulse;
            m.step({heading, true}, dt);
            // Отключаем дальнейший разгон между шагами для повторяемого медленного удара.
            if (thorn.active) {
                group.flight = heading * speed;
                continue;
            }
            const Vec incoming = group.flight + impulse * std::exp(-2.6 * dt);
            const Vec drift = incoming * (103. / 203.);
            const Vec step = group.flight * dt + impulse * (-std::expm1(-2.6 * dt) / 2.6);
            const double projection = dot(prev, step);
            const double hit =
                (-projection - std::sqrt(projection * projection - squared(step) * (squared(prev) - reach * reach))) /
                squared(step);
            const Vec impact = prev + step * hit;
            near(length(group.center() - impact * (103. / 203.) - drift * ((1 - hit) * dt)), 0, 1e-6);
            Vec momentum{};
            for (auto& part : group.cells) {
                momentum = momentum + (group.flight + part.impulse) * part.mass;
                near(length(part.skin.travel - (group.flight + part.impulse)), 0, 1e-7);
            }
            near(length(momentum - incoming * 103), 0);
            check(group.mass() == 203 && group.cells.size() == 4);
            const Vec recoil = group.flight + group.cells[0].impulse - drift;
            check(dot(unit(recoil), unit(impact)) > .999);
            const Vec a = group.cells[1].impulse - group.cells[0].impulse;
            const Vec b = group.cells[2].impulse - group.cells[0].impulse;
            const Vec c = group.cells[3].impulse - group.cells[0].impulse;
            check(std::abs(dot(a, cross(b, c))) > 100);
            return length(recoil);
        }
        throw std::runtime_error("Missed spike core");
    };
    const double headOn = burst({{0, 1, 0}}, {}, BASE_SPEED);
    const double oblique = burst(along, needle.axis * 1.8, BASE_SPEED);
    const double slow = burst({{0, 0, -1}}, {}, 1.0);
    check(headOn > oblique + 1 && oblique > slow + .5);

    auto contest = emptyArena(100);
    contest.spikes.emplace_back();
    const double reach = scale * (1 + Spike::core);
    contest.groups[0].cells[0].position[2] = -reach - .13;
    contest.groups[0].cells[0].impulse[2] = 14;
    contest.groups[1].cells.push_back({25000, 100, {{0, 0, reach + .02}}, {{0, 0, -12}}});
    contest.step({}, dt);
    check(!contest.spikes[0].active && contest.groups[0].mass() == 100 && contest.groups[1].mass() == 200);

    auto swept = emptyArena(100);
    swept.spikes = {{{{0, 0, 2.5}}}, {{{0, 0, 0}}}};
    swept.groups[0].cells[0].position[2] = -4;
    swept.groups[0].cells[0].impulse[2] = 540;
    swept.step({}, dt);
    check(swept.spikes[0].active && !swept.spikes[1].active && swept.groups[0].mass() == 200);

    auto corner = emptyArena(103);
    const Vec point{{ARENA_HALF - scale, ARENA_HALF - scale, ARENA_HALF - scale}};
    corner.spikes.push_back({point});
    corner.groups[0].cells[0].position = point - Vec{{0, 0, 3}};
    corner.groups[0].flight = thrust.direction * BASE_SPEED;
    ticks(corner, thrust, 50);
    check(corner.groups[0].cells.size() == 4 && corner.groups[0].mass() == 203);
    for (auto& part : corner.groups[0].cells) {
        for (auto coordinate : part.position.components) {
            check(std::abs(coordinate) + radius(part.mass) <= ARENA_HALF + 1e-8);
        }
    }
    std::cout << "Spike graze: tip flex " << peak[1] << " radii, identical at 30/60/120 FPS. Recoil: head-on " << headOn
              << ", oblique " << oblique << ", slow " << slow << '\n';
}

void walls() {
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign : {-1, 1}) {
            auto m = emptyArena();
            auto& g = m.groups[0];
            auto& cell = g.cells[0];
            cell.position[axis] = sign * (ARENA_HALF - radius(40) - 0.01);
            Vec dir{};
            dir[axis] = sign;
            g.flight = dir * BASE_SPEED;
            ticks(m, {dir, true}, 120);
            near(cell.position[axis], sign * (ARENA_HALF - radius(40)));
            near(length(g.flight), 0);
            near(cell.skin.patches[0].dent.value, 0.155, 0.0001);
            m.step({dir, true}, dt);
            near(cell.skin.patches[0].dent.value, 0.155, 0.0001);
            ticks(m, {dir * (-1), true}, 9);
            near(cell.skin.patches[0].dent.value, 0, 0.001);
            check(std::abs(cell.skin.patches[0].side.value) > 0.008);
            check(std::abs(cell.position[axis]) < ARENA_HALF - radius(40));
        }
    }
    auto m = emptyArena();
    auto& g = m.groups[0];
    auto& cell = g.cells[0];
    cell.position[0] = ARENA_HALF - radius(40);
    const Vec diagonal = unit({{1, 0, 1}});
    g.flight = diagonal * BASE_SPEED;
    m.step({diagonal, true}, dt);
    near(g.flight[0], 0);
    near(g.flight[2], BASE_SPEED / std::sqrt(2.0));
    check(cell.skin.patches[0].slip[2] > .65);
    const auto startSlide = cell.position;
    ticks(m, {diagonal, true}, 180);
    near(g.flight[2], BASE_SPEED / std::sqrt(2.0));
    near(cell.position[2] - startSlide[2], 3 * BASE_SPEED / std::sqrt(2.0));
    near(cell.position[0], startSlide[0]);
    // Две части бок о бок у одной стенки постепенно освобождают место для общей сферы.
    auto merged = emptyArena(20);
    auto& siblings = merged.groups[0];
    siblings.cells[0].position = {{ARENA_HALF - radius(20), 0, 0}};
    siblings.cells.push_back({10001, 20, siblings.cells[0].position + Vec{{0, 1.45, 0}}});
    Vec previous = siblings.center();
    for (int i = 0; i < 36; ++i) {
        merged.step({{{1, 0, 0}}, true}, dt);
        check(siblings.mass() == 40 && length(siblings.center() - previous) < 0.012);
        previous = siblings.center();
    }
    check(siblings.cells.size() == 1);
    near(siblings.center()[0], ARENA_HALF - radius(40));
}

void eatingAndBots() {
    auto m = emptyArena(50);
    auto& p = m.groups[0];
    auto& b = m.groups[1];
    b.cells = {{23000, 40, {{0, 0, 1.3}}}};
    m.step({}, dt);
    check(p.mass() == 50 && b.mass() == 40);
    b.cells[0].position = {};
    p.cells[0].mass = 49;
    m.step({}, dt);
    check(p.mass() == 49 && b.mass() == 40);
    p.cells[0].mass = 50;
    b.cells[0].position = p.center();
    m.step({}, dt);
    check(p.mass() == 90 && b.cells.empty());
    auto hunt = emptyArena(40);
    auto& hunter = hunt.groups[1];
    auto& prey = hunt.groups[0];
    hunter.cells = {{23000, 99, {{0, 0, -4}}}};
    check(hunt.botCommand(1).split); // 50 против 40 на точном пороге.
    hunter.cells[0].mass = 98;
    check(!hunt.botCommand(1).split);
    prey.flight = {{5, 0, 0}};
    check(hunt.botCommand(1).direction[0] > 0.2);
    hunt.groups[2].cells = {{23001, 200, {{-1, 0, -4}}}};
    check(hunt.botCommand(1).direction[0] > 0.99 && !hunt.botCommand(1).split);
    hunt.groups[2].cells.clear();
    prey.cells.clear();
    hunt.food = {{{{1, 0, -4}}, 1}, {{{-1.5, 0, -4}}, 5}};
    check(hunt.botCommand(1).direction[0] < -0.99);
    prey.cells = {{10000, 40, {{0, 0, -4}}}};
    hunt.step({}, dt);
    check(hunt.resultVisible() && prey.cells.empty() && hunter.mass() >= 138);
}

void fusionRules() {
    auto m = emptyArena(20);
    auto& g = m.groups[0];
    g.cells.push_back({10001, 20, {{1.3, 0, 0}}, {}, 0.5});
    const auto before = gap(g);
    m.step({}, dt);
    check(g.fusions.empty() && gap(g) > before);
    g.cells[1].cooldown = 0;
    g.flight = {{0, 0, BASE_SPEED * 0.3 + BASE_SPEED * dt / 2}};
    m.step({}, dt);
    near(length(g.flight), 0.3 * BASE_SPEED);
    check(g.fusions.empty());
    m.step({}, dt);
    check(g.fusions.size() == 1);
    // Добыча может быть съедена во время анимации. Оставшаяся клетка продолжает жить отдельно.
    m.groups[1].cells = {{20000, 25, g.cells[1].position}};
    m.step({}, dt);
    check(g.cells.size() == 1 && g.fusions.empty() && g.mass() == 20);
    check(m.groups[1].mass() == 45);
}

void cameraFraming() {
    auto m = emptyArena();
    auto& g = m.groups[0];
    Camera camera;
    camera.follow(g, 16.0 / 9, 0);
    const double tanY = std::tan(std::acos(-1.0) / 6);
    const double diameterFraction = radius(40) / (tanY * camera.distance);
    near(diameterFraction, 0.20, 0.003);
    check(camera.toView(g.center() - camera.eye())[1] < 0);
    g.cells[0].position = {{0, 0, -ARENA_HALF + radius(40)}};
    camera = {};
    camera.follow(g, 16.0 / 9, 0);
    check(camera.eye()[2] < -ARENA_HALF);
    for (int i = 1; i < 16; ++i) {
        g.cells.push_back(
            {10000 + i, 18, g.center() + Vec{{std::sin(i * 2.) * 9., std::cos(i * 3.) * 6., double(i) - 8.}}});
    }
    for (double aspect : {0.55, 1.0, 16.0 / 9, 3.0}) {
        for (double pitch : {10000., -10000., 680.}) {
            camera.turn(741, pitch);
            near(camera.right[1], 0);
            near(length(camera.forward), 1);
            near(dot(camera.up, camera.forward), 0);
            camera.follow(g, aspect, dt);
            for (const auto& cell : g.cells) {
                const Vec p = camera.toView(cell.position - camera.eye());
                const double r = radius(cell.mass);
                check(p[2] > r);
                check(std::abs(p[1]) + r * std::hypot(1.0, tanY) <= p[2] * tanY);
                const double tanX = tanY * aspect;
                check(std::abs(p[0]) + r * std::hypot(1.0, tanX) <= p[2] * tanX);
            }
        }
    }
    camera.turn(0, -10000);
    auto climb = emptyArena();
    ticks(climb, {camera.forward, true}, 300);
    near(climb.groups[0].flight[1], BASE_SPEED);
    near(climb.groups[0].flight[0], 0);
    near(climb.groups[0].flight[2], 0);
}

void impacts() {
    // Проходим через наружную оболочку добычи обычным движением до входа центра.
    auto hunt = emptyArena(50);
    const double huntSpeed = hunt.groups[0].speedLimit();
    hunt.groups[0].flight = {{0, 0, huntSpeed}};
    hunt.groups[1].cells = {{24000, 40, {{0, 0, 2.75}}}};
    bool crossedShell = false, coatedPrey = false;
    for (int i = 0; i < 45 && !hunt.groups[1].cells.empty(); ++i) {
        hunt.step(thrust, dt);
        if (!hunt.groups[1].cells.empty()) {
            const double d = length(hunt.groups[0].center() - hunt.groups[1].center());
            crossedShell |= d < radius(50) + radius(40) && d > radius(50);
            const auto& hunter = hunt.groups[0].cells[0];
            near(hunter.position[2], (i + 1) * dt * huntSpeed);
            near(length(hunter.impulse), 0);
            coatedPrey |= hunter.skin.patches[0].wrap > .1;
        }
    }
    check(crossedShell && coatedPrey && hunt.groups[0].mass() == 90 && hunt.groups[1].cells.empty());
    auto rivals = emptyArena(40);
    rivals.groups[0].flight = {{0, 0, BASE_SPEED}};
    rivals.groups[1].cells = {{24001, 48, {{0, 0, 1.96}}}};
    for (int i = 0; i < 10; ++i) {
        rivals.step(thrust, dt);
    }
    auto& a = rivals.groups[0].cells[0];
    auto& b = rivals.groups[1].cells[0];
    check(a.position[2] < b.position[2] && a.mass + b.mass == 88);
    check(a.skin.patches[0].dent.value > b.skin.patches[0].dent.value && b.skin.patches[0].dent.value > .05);
    check(dot(a.skin.patches[0].normal, b.skin.patches[0].normal) < -.99);

    auto corner = emptyArena();
    auto& g = corner.groups[0];
    g.cells[0].position = {{ARENA_HALF - radius(40) - .01, ARENA_HALF - radius(40) - .01, 0}};
    const auto diagonal = unit({{1, 1, 0}});
    g.flight = diagonal * BASE_SPEED;
    ticks(corner, {diagonal, true}, 10);
    const auto& p = g.cells[0].skin.patches;
    check(p[0].dent.value > .13 && p[1].dent.value > .13 && dot(p[0].normal, p[1].normal) == 0);

    auto trace = [](int fps) {
        auto m = emptyArena();
        auto& g = m.groups[0];
        g.cells[0].position[2] = ARENA_HALF - radius(40) - .01;
        g.flight = thrust.direction * BASE_SPEED;
        huynity::runtime::FixedStepUpdate clock(dt, 8);
        std::vector<std::array<double, 4>> history;
        for (int frame = 0; frame < fps * 3; ++frame) {
            clock.advance(1. / fps, [&](double step) {
                m.step(history.size() < 120 ? thrust : Command{{{0, 0, -1}}, true}, step);
                const auto& patch = g.cells[0].skin.patches[0];
                history.push_back({patch.dent.value, patch.side.value, g.center()[2], length(g.flight)});
            });
        }
        return history;
    };
    const auto trace60 = trace(60);
    check(trace60.size() == 180 && trace(30) == trace60 && trace(120) == trace60);
    const auto rebound =
        std::ranges::min_element(trace60.begin(), trace60.begin() + 15, {}, [](auto point) { return point[2]; });
    const double bounce = ARENA_HALF - radius(40) - (*rebound)[2];
    check(bounce > .0005 && bounce < .018 * radius(40));
    auto deepest = std::ranges::max_element(trace60, {}, [](auto v) { return v[0]; });
    auto widest = std::ranges::max_element(trace60, {}, [](auto v) { return v[1]; });
    check((*deepest)[0] >= .28 && (*deepest)[0] <= .33 && widest > deepest);
    near(trace60[119][0], .155, .0001);
    near(trace60[130][0], 0, .001);
    check(trace60[129][1] > .03 && trace60[140][1] < 0 && std::abs(trace60[155][1]) < .004);
    Deformation tap;
    tap.contact(-1, {{1, 0, 0}}, .35, {}, 0);
    double peak = 0;
    for (int i = 0; i < 60; ++i) {
        tap.advance(dt);
        peak = std::max(peak, tap.patches[0].dent.value);
    }
    check(peak > .003 && peak < .035);
    std::cout << "Wall dent " << (*deepest)[0] << " at " << (deepest - trace60.begin() + 1) * dt << " s; side wave "
              << (*widest)[1] << " at " << (widest - trace60.begin() + 1) * dt << " s; sustained " << trace60[119][0]
              << "; identical at 30/60/120 FPS\n";
}

void rivalCrawl() {
    const double r = radius(40), reach = 2 * r;
    auto encounter = [&](int fps) {
        auto match = emptyArena();
        auto& a = match.groups[0].cells.front();
        auto& b = match.groups[1].cells.emplace_back(Cell{24001, 40, {{0, 0, 2.3}}});
        match.groups[0].flight = thrust.direction * BASE_SPEED;
        huynity::runtime::FixedStepUpdate clock(dt, 8);
        std::vector<std::array<double, 4>> trace;
        for (int frame = 0; frame < 4 * fps; ++frame) {
            clock.advance(1. / fps, [&](double step) {
                match.step(trace.size() < 146 ? thrust : Command{}, step);
                const auto offset = b.position - a.position;
                const auto& skin = a.skin.patches.front();
                check(a.mass == 40 && b.mass == 40 && length(offset) >= reach - .52 * r - 1e-8);
                near(a.position[1] + b.position[1], 0);
                if (skin.held) {
                    const auto& other = b.skin.patches.front();
                    check(skin.key == b.id && other.key == a.id);
                    near(dot(skin.slip, skin.normal), 0);
                    near(dot(skin.slip, other.slip), -1);
                    near(skin.wrap, other.wrap);
                }
                trace.push_back({length(offset), skin.wrap, offset[1], offset[2]});
            });
        }
        return trace;
    };
    const auto trace = encounter(60);
    check(trace.size() == 240 && encounter(30) == trace && encounter(120) == trace);
    const auto deepest = std::ranges::min_element(trace, {}, [](auto tick) { return tick[0]; });
    check((*deepest)[0] < reach - .35 * r);
    check(std::ranges::any_of(trace, [](auto tick) { return tick[1] > .7; }));
    // На лобовом курсе клетка проходит по боку соперника и обгоняет его.
    check(
        std::ranges::any_of(trace, [&](auto tick) { return tick[0] < reach && std::abs(tick[2]) > r && tick[3] < 0; }));
    check(trace.back()[0] > reach && trace.back()[1] == 0);

    auto wall = emptyArena();
    auto& a = wall.groups[0].cells.front();
    auto& b = wall.groups[1].cells.emplace_back(Cell{24001, 40, {{0, 0, ARENA_HALF - r}}});
    a.position = b.position - Vec{{.33, .2, 2.1}};
    wall.groups[0].flight = thrust.direction * BASE_SPEED;
    bool sliding = false;
    for (int i = 0; i < 80; ++i) {
        wall.step(thrust, dt);
        check(b.position[2] <= ARENA_HALF - r && length(a.position - b.position) >= reach - .52 * r - 1e-8);
        const auto& patch = a.skin.patches.front();
        sliding |= patch.wrap > .4 && patch.slip[0] < -.35;
    }
    check(sliding && a.mass + b.mass == 80);

    // Вертикальный импульс, как у выброшенной части: на контакт тратится относительная скорость пары.
    auto vertical = emptyArena();
    auto& top = vertical.groups[1].cells.emplace_back(Cell{24001, 40, {{0, 2.3, 0}}});
    auto& lower = vertical.groups[0].cells[0];
    lower.impulse = {{0, 13, 0}};
    double transverse{};
    for (int i = 1; i <= 80; ++i) {
        vertical.step({}, dt);
        const Vec offset = top.position - lower.position;
        check(length(offset) >= reach - .52 * r - 1e-8);
        transverse = std::max(transverse, std::hypot(offset[0], offset[2]));
        const Vec center = (lower.position + top.position) * .5;
        near(center[1], 1.15 + 6.5 * -std::expm1(-2.6 * i * dt) / 2.6);
        near(std::hypot(center[0], center[2]), 0);
    }
    check(transverse > .2 * r);

    // Пересекаем порог 1,25 прямо во время наползания, подобрав единицу еды.
    auto growth = emptyArena(49);
    auto& hunter = growth.groups[0].cells[0];
    auto& prey = growth.groups[1].cells;
    prey.push_back({24001, 40, {{0, 0, ARENA_HALF - r}}});
    hunter.position = prey.front().position - Vec{{0, 0, 2.3}};
    growth.groups[0].flight = thrust.direction * BASE_SPEED;
    for (int i = 0; i < 45 && hunter.skin.patches[0].wrap < .55; ++i) {
        growth.step(thrust, dt);
    }
    check(hunter.skin.patches[0].wrap >= .55 && prey.size() == 1);
    growth.food.push_back({hunter.position, 1});
    growth.step(thrust, dt);
    check(hunter.mass == 50 && prey.size() == 1);
    for (int i = 0; i < 120 && !prey.empty(); ++i) {
        growth.step({unit(prey.front().position - hunter.position), true}, dt);
    }
    check(hunter.mass == 90 && prey.empty());
    std::cout << "Rival contact: maximum overlap " << (reach - (*deepest)[0]) / r << " of radius at 30/60/120 FPS\n";
}

void sessions() {
    for (int bots : {0, 5, 8}) {
        for (int minutes : {3, 5, 10}) {
            Session settings{U"Орбита", 3, minutes, bots};
            Match m(settings);
            check(m.groups.size() == std::size_t(1 + bots) && m.ranking().size() == m.groups.size());
            check(!m.spikes.empty() && !m.food.empty() && m.remaining == 60 * minutes);
            m.groups[0].cells[0].skin.contact(-1, {{1, 0, 0}}, BASE_SPEED, {}, 1);
            m.step(launch, dt);
            auto restart = Match(m.session);
            check(restart.session == settings && restart.remaining == 60 * minutes);
            check(restart.groups[0].mass() == 40 && restart.groups[0].cells.size() == 1);
            for (auto& patch : restart.groups[0].cells[0].skin.patches) {
                check(!patch.key);
            }
        }
    }
}

void shellVolume() {
    auto measureMesh = [](const ShellMesh& shell) {
        double volume{};
        Vec moment{};
        for (const auto& face : shell.mesh.triangles) {
            const auto& p = shell.mesh.positions;
            const Vec a{p[face.indices[0]].coordinates};
            const Vec b{p[face.indices[1]].coordinates};
            const Vec c{p[face.indices[2]].coordinates};
            const double v = dot(a, cross(b, c)) / 6.0;
            volume += v;
            moment = moment + (a + b + c) * (v / 4.0);
        }
        check(volume > 0);
        near(length(moment), 0, 1e-8);
        return volume;
    };
    const double unitVolume = measureMesh(shell(1, 0, {}, 1));
    for (int m : {20, 78}) {
        const double a = radius(m), b = radius(20);
        for (int i = 0; i <= 36; ++i) {
            const double u = i / 36.0;
            const Vec offset = unit({{3, 1, 2}}) * ((a + b) * (1 - smooth(u)));
            auto s = shell(a, b, offset, u);
            near(measureMesh(s), unitVolume * (a * a * a + b * b * b), 1e-8);
            if (i == 36) {
                for (const auto& p : s.mesh.positions) {
                    near(length(Vec{p.coordinates}), radius(m + 20), 1e-8);
                }
            }
        }
    }
}

void matchClock() {
    Match m;
    ticks(m, {}, 10799);
    near(m.remaining, dt);
    m.step({}, dt);
    check(m.remaining == 0 && m.resultVisible());
    const auto ranking = m.ranking();
    for (int i = 1; i < 6; ++i) {
        check(m.groups[ranking[i - 1]].mass() >= m.groups[ranking[i]].mass());
    }
    const auto mass = m.groups[ranking.front()].mass();
    ticks(m, launch, 20);
    check(m.groups[ranking.front()].mass() == mass);
    m = Match{};
    check(!m.resultVisible() && m.remaining == 180 && !m.food.empty());
    for (const auto& g : m.groups) {
        check(g.mass() == 40 && g.cells.size() == 1 && g.fusions.empty() && squared(g.flight) == 0);
    }
}

} // namespace

int main() {
    flight();
    motionSkin();
    massAndFlight();
    massAndFusion();
    passiveLoss();
    splitting();
    spikes();
    spikePhysics();
    walls();
    impacts();
    rivalCrawl();
    sessions();
    eatingAndBots();
    fusionRules();
    cameraFraming();
    shellVolume();
    matchClock();
    std::cout << "Match checks passed.\n";
}
