#include "deformation.h"

#include <algorithm>
#include <cmath>

#include "match.h"

namespace agar {
void Spring::advance(double target, double omega, double damping, double dt) {
    const double offset = value - target;
    const double decay = omega * damping, frequency = omega * std::sqrt(1 - damping * damping);
    const double attenuation = std::exp(-decay * dt), c = std::cos(frequency * dt),
                 s = std::sin(frequency * dt) / frequency;
    value = target + attenuation * (offset * c + (velocity + decay * offset) * s);
    velocity = attenuation * (velocity * c - (decay * velocity + omega * omega * offset) * s);
}

namespace {
double activity(const ContactPatch& p) {
    return std::abs(p.dent.value) + std::abs(p.side.value) + p.wrap +
           (std::abs(p.dent.velocity) + std::abs(p.side.velocity)) / 16;
}

ContactPatch& slot(Deformation& skin, int key) {
    for (auto& p : skin.patches) {
        if (p.key == key) {
            return p;
        }
    }
    auto& p = *std::ranges::min_element(skin.patches, {}, [](const ContactPatch& c) {
        const bool wall = c.key < 0 && !needleContact(c.key);
        return (wall && (c.held || c.touching) ? 8.0 : 0.0) + (c.touching ? 2.0 : 0.0) + activity(c);
    });
    p = {.key = key};
    return p;
}
} // namespace

void Deformation::move(Vec velocity, double dt) {
    const Vec acceleration = (velocity - travel) * (1 / dt);
    travel = velocity;
    flow = flow + (velocity - flow) * -std::expm1(-12 * dt);
    if (squared(flow) > 1e-8) {
        const Vec axis = unit(flow);
        // Переносим широкую ось капли вместе с потоком, в том числе при вертикальном полёте.
        Vec across = broadside - axis * dot(broadside, axis);
        if (squared(across) < 1e-6) {
            across = cross(std::abs(axis[1]) < .85 ? Vec{{0, 1, 0}} : Vec{{0, 0, 1}}, axis);
        }
        broadside = unit(across);
    }
    const double force = length(acceleration);
    const Vec target = acceleration * (-0.039 / std::max(1.0, force / 6.0));
    for (int axis = 0; axis < 3; ++axis) {
        inertia[axis].advance(target[axis], 14, 0.29, dt);
        lag[axis] = inertia[axis].value;
    }
}

ContactPatch& Deformation::contact(int key, Vec normal, double closingSpeed, Vec tangent, double pressure) {
    auto& p = slot(*this, key);
    p.normal = normal;
    if (!p.held && !p.touching) {
        p.dent.velocity +=
            (7.9 - 2.5 * std::clamp(pressure, 0.0, 1.0)) * std::clamp(closingSpeed / BASE_SPEED, 0.0, 1.0);
    }
    p.touching = true;
    p.pressure = std::max(p.pressure, std::clamp(pressure, 0.0, 1.0));
    const auto speed = length(tangent);
    p.slip = tangent * (1.0 / std::max(speed, BASE_SPEED));
    return p;
}

void Deformation::advance(double dt) {
    for (auto& p : patches) {
        if (!p.key) {
            continue;
        }
        const double oldDent = p.dent.value;
        p.dent.advance(0.155 * p.pressure, 16.0, 0.38, dt);
        if (p.dent.value <= 0 || p.dent.value >= 0.33) {
            p.dent = {std::clamp(p.dent.value, 0.0, 0.33), 0};
        }
        // Оболочка освобождает пятно контакта раньше, чем проходит поперечная волна.
        if (p.held && !p.touching) {
            p.side.velocity -= 2.3 * p.side.value;
        }
        p.side.advance(0.5 * (oldDent + p.dent.value), 19.0, p.touching ? 0.64 : 0.44, dt);
        p.wrap += (p.wrapTarget - p.wrap) * -std::expm1(-(p.wrapTarget > p.wrap ? 22.0 : 9.0) * dt);
        p.wrapTarget = 0;
        p.held = p.touching;
        p.touching = false;
        p.pressure = 0;
        if (!p.held && activity(p) < 0.0005) {
            p = {};
        }
    }
}

double Deformation::extent() const {
    double bound = 1.10 + .36 * std::min(1.2, length(flow) / BASE_SPEED) + 1.8 * length(lag);
    for (const auto& p : patches) {
        bound += .80 * std::abs(p.side.value) + .34 * p.wrap;
    }
    return bound;
}

Deformation blend(const Deformation& a, const Deformation& b, double share) {
    Deformation result;
    result.flow = a.flow * (1 - share) + b.flow * share;
    result.travel = a.travel * (1 - share) + b.travel * share;
    result.lag = a.lag * (1 - share) + b.lag * share;
    // Знак поперечной оси не меняет форму её сечения.
    result.broadside =
        unit(a.broadside * (1 - share) + b.broadside * std::copysign(share, dot(a.broadside, b.broadside)));
    for (int axis = 0; axis < 3; ++axis) {
        result.inertia[axis] = {result.lag[axis], std::lerp(a.inertia[axis].velocity, b.inertia[axis].velocity, share)};
    }
    auto add = [&](const Deformation& source, double weight) {
        for (const auto& p : source.patches) {
            if (!p.key) {
                continue;
            }
            auto& dst = slot(result, p.key);
            dst.normal = dst.normal + p.normal * weight;
            dst.slip = dst.slip + p.slip * weight;
            dst.dent.value += p.dent.value * weight;
            dst.dent.velocity += p.dent.velocity * weight;
            dst.side.value += p.side.value * weight;
            dst.side.velocity += p.side.velocity * weight;
            const double film = p.wrap * weight;
            dst.wrap += film;
            dst.seam += p.seam * film;
            dst.contactScale += p.contactScale * film;
            dst.held |= p.held;
        }
    };
    add(a, 1 - share);
    add(b, share);
    for (auto& p : result.patches) {
        if (p.key) {
            p.normal = unit(p.normal);
            if (p.wrap > 0) {
                p.seam /= p.wrap;
                p.contactScale /= p.wrap;
            }
        }
    }
    return result;
}
} // namespace agar
