#pragma once

#include "huynity/math/vector.h"

#include <array>

namespace agar {

using Vec = huynity::math::Vector3;

struct Spring {
    double value{}, velocity{};
    void advance(double target, double omega, double damping, double dt);
};

constexpr bool needleContact(int key) {
    return key < -6;
}

struct ContactPatch {
    int key{}; // Клетки > 0, грани куба от -1 до -6, дальше — отдельные иглы.
    Vec normal{}, slip{};
    Spring dent{}, side{};
    double pressure{};
    double wrap{}, wrapTarget{}, seam{}, contactScale{};
    bool touching{}, held{};
};

struct Deformation {
    std::array<ContactPatch, 6> patches{};
    Vec flow{}, lag{}, travel{};
    Vec broadside{{1, 0, 0}};
    std::array<Spring, 3> inertia{};
    void move(Vec velocity, double dt);
    ContactPatch& contact(int key, Vec normal, double closingSpeed, Vec tangent, double pressure);
    void advance(double dt);
    double extent() const;
};

Deformation blend(const Deformation& a, const Deformation& b, double share);

} // namespace agar
