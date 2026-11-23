#pragma once

#include "huynity/math/transform.h"

#include "match.h"

namespace agar {

struct Camera {
    void turn(double dx, double dy);
    void follow(const Group& group, double aspect, double dt);
    Vec toView(Vec vector) const;
    huynity::math::Transform3 worldToView() const;
    huynity::math::Transform3 orientation() const;
    Vec eye() const;

    double yaw{}, pitch{};
    Vec right{{1, 0, 0}}, up{{0, 1, 0}}, forward{{0, 0, 1}};
    Vec focus{};
    double distance{}, lift{}, shoulder{};
};

} // namespace agar
