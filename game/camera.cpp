#include "camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace agar {

void Camera::turn(double dx, double dy) {
    yaw = std::remainder(yaw + dx * 0.0023, 2 * std::numbers::pi);
    pitch = std::clamp(pitch - dy * 0.0023, -std::numbers::pi / 2, std::numbers::pi / 2);
    forward = {{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)}};
    right = {{std::cos(yaw), 0, -std::sin(yaw)}};
    up = cross(forward, right);
}

Vec Camera::toView(Vec v) const {
    return {{dot(v, right), dot(v, up), dot(v, forward)}};
}

Vec Camera::eye() const {
    return focus - forward * distance + up * lift + right * shoulder;
}

huynity::math::Transform3 Camera::worldToView() const {
    const auto offset = toView(eye());
    return {{right[0], right[1], right[2], -offset[0], up[0], up[1], up[2], -offset[1], forward[0], forward[1],
             forward[2], -offset[2]}};
}

huynity::math::Transform3 Camera::orientation() const {
    return {{right[0], up[0], forward[0], 0, right[1], up[1], forward[1], 0, right[2], up[2], forward[2], 0}};
}

void Camera::follow(const Group& group, double aspect, double dt) {
    if (group.cells.empty()) {
        return;
    }
    const Vec center = group.center();
    double spread{};
    for (const auto& cell : group.cells) {
        spread = std::max(spread, length(cell.position - center));
    }
    const bool first = distance == 0;
    const double track = first ? 1.0 : -std::expm1(-10 * dt);
    const double zoom = first ? 1.0 : -std::expm1(-6.5 * dt);
    const double base = 8.72 * radius(group.mass());
    focus = focus + (center - focus) * track;
    lift = std::lerp(lift, base * 0.13 + std::min(base * 0.26, 0.48 * spread), zoom);
    shoulder = std::lerp(shoulder, std::min(base * 0.12, 0.2 * spread), zoom);
    const double tanY = std::tan(std::numbers::pi / 6) * 0.90, tanX = tanY * aspect;
    double required{};
    for (const auto& cell : group.cells) {
        const auto p = toView(cell.position - focus);
        const double r = radius(cell.mass) * cell.skin.extent();
        const auto horizontal = (std::abs(p[0] - shoulder) + r * std::hypot(1.0, tanX)) / tanX;
        const auto vertical = (std::abs(p[1] - lift) + r * std::hypot(1.0, tanY)) / tanY;
        required = std::max(required, std::max(horizontal, vertical) - p[2]);
    }
    const double target = std::max(base + 0.46 * spread, required * 1.06);
    distance = std::max(required, std::lerp(distance, target, zoom));
}

} // namespace agar
