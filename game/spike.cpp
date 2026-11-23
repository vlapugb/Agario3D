#include "spike.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "match.h"

namespace agar {
const std::array<Spike::Needle, Spike::needleCount>& Spike::crown() {
    static const auto needles = [] {
        std::array<Needle, needleCount> result;
        const double golden = std::numbers::pi * (3 - std::sqrt(5.0));
        for (int i = 0; i < needleCount; ++i) {
            const double y = 1 - 2.0 * (i + .5) / needleCount, r = std::sqrt(1 - y * y);
            const Vec axis{{r * std::cos(i * golden), y, r * std::sin(i * golden)}};
            result[i] = {axis, axis * (.95 + .05 * std::sin(7.8 * i))};
        }
        return result;
    }();
    return needles;
}

Spike::Touch Spike::brush(Cell& cell, int spikeIndex, double dt) {
    const double scale = radius(mass), r = radius(cell.mass) / scale;
    const Vec start = (cell.stepOrigin - position) * (1 / scale);
    const Vec step = (cell.position - cell.stepOrigin) * (1 / scale);
    const double step2 = squared(step);
    auto timeAt = [&](Vec point) { return step2 > 0 ? std::clamp(dot(point - start, step) / step2, 0.0, 1.0) : 0.0; };
    const double closest2 = squared(start + step * timeAt({}));
    if (closest2 > (r + 1 + maxBend) * (r + 1 + maxBend)) {
        return {};
    }

    Touch touch;
    const double reach = r + core;
    if (closest2 <= reach * reach) {
        const double outside = squared(start) - reach * reach;
        const double approach = -dot(start, step);
        // Устойчивая форма малого корня уравнения пересечения отрезка со сферой.
        touch.coreTime =
            outside <= 0
                ? 0.0
                : std::clamp(outside / (approach + std::sqrt(std::max(0.0, approach * approach - step2 * outside))),
                             0.0, 1.0);
    }

    const Vec velocity = step * (scale / dt);
    double bestPressure{};
    int bestNeedle{};
    Vec bestNormal{}, load{};
    for (int i = 0; i < needleCount; ++i) {
        const auto& [axis, restTip] = crown()[i];
        const Vec tip = restTip + bends[i].offset(), root = axis * base;
        const Vec center = start + step * timeAt(tip);
        const Vec radial = center - axis * dot(center, axis);
        Vec rim = root;
        if (squared(radial) > 1e-12) {
            rim = root + unit(radial) * width;
        }
        const Vec edge = tip - rim;
        const double lever = std::clamp(dot(center - rim, edge) / squared(edge), 0.0, 1.0);
        const Vec surface = rim + edge * lever;
        const Vec offset = surface - (start + step * timeAt(surface));
        const double distance2 = squared(offset);
        if (distance2 >= r * r) {
            continue;
        }
        touch.needles = true;
        const double distance = std::sqrt(distance2), depth = r - distance;
        const Vec normal = distance > 1e-8 ? offset * (1 / distance) : axis * -1;
        const Vec tangent = velocity - normal * dot(velocity, normal);
        const double pressure = smooth(depth / .18);
        const Vec force =
            (normal * std::min(.14, depth) + tangent * (.17 * pressure / std::max(BASE_SPEED, length(tangent)))) *
            lever;
        bends[i].load = bends[i].load + force;
        load = load + force;
        flexing = true;
        // Погружённое в тело остриё уже прошло сквозь оболочку. След остаётся на пружине с его собственным ключом.
        const double onSkin = pressure * (1 - smooth((depth / r - .12) / .38)) * (.35 + .65 * lever);
        if (onSkin > bestPressure) {
            bestPressure = onSkin;
            bestNormal = normal;
            bestNeedle = i;
        }
    }
    if (bestPressure > 0) {
        const double closing = dot(velocity, bestNormal);
        cell.skin.contact(-7 - spikeIndex * needleCount - bestNeedle, bestNormal, closing,
                          velocity - bestNormal * closing, bestPressure);
    }
    if (!touch.coreTime) {
        // Упругий венец сопротивляется скольжению только задетой клетки. Ядро проходимо для мелких частей.
        const Vec resistance = load * (20.0 * mass / (mass + cell.mass));
        cell.impulse = cell.impulse - resistance * (dt / std::max(1.0, length(resistance) / 3.5));
    }
    return touch;
}

void Spike::relax(double dt) {
    if (!flexing) {
        return;
    }
    flexing = false;
    for (auto& bend : bends) {
        const Vec target = bend.load * (1 / std::max(1.0, length(bend.load) / .175));
        bend.load = {};
        for (int i = 0; i < 3; ++i) {
            auto& spring = bend.spring[i];
            if (spring.value == 0 && spring.velocity == 0 && target[i] == 0) {
                continue;
            }
            spring.advance(target[i], 23, .48, dt);
            if (target[i] == 0 && std::abs(spring.value) + std::abs(spring.velocity) / 23 < .0002) {
                spring = {};
            } else {
                flexing = true;
            }
        }
        const Vec offset = bend.offset();
        if (squared(offset) > maxBend * maxBend) {
            const Vec n = unit(offset);
            Vec v{{bend.spring[0].velocity, bend.spring[1].velocity, bend.spring[2].velocity}};
            v = v - n * std::max(0.0, dot(v, n));
            for (int i = 0; i < 3; ++i) {
                bend.spring[i] = {n[i] * maxBend, v[i]};
            }
        }
    }
}
} // namespace agar
