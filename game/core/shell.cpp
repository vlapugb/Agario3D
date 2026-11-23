#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace agar {
namespace {

constexpr int slices = 48;
constexpr int sides = 56;

struct Ring {
    double x, radius, axialNormal;
};

struct Measures {
    double volume{}, moment{};
};

Measures measure(const std::vector<Ring>& rings) {
    // У всех сечений одинаковое количество сторон: общий множитель площади можно сократить.
    Measures m;
    for (std::size_t i = 1; i < rings.size(); ++i) {
        const auto& [x, a, ignored] = rings[i - 1];
        const double b = rings[i].radius;
        const double dx = rings[i].x - x;
        const double volume = dx * (a * a + a * b + b * b) / 3.0;
        m.volume += volume;
        m.moment += x * volume + dx * dx * (a * a + 2 * a * b + 3 * b * b) / 12.0;
    }
    return m;
}

double sphereVolume() {
    std::vector<Ring> rings;
    for (int i = 0; i <= slices; ++i) {
        const double t = std::numbers::pi * i / slices;
        rings.push_back({-std::cos(t), std::sin(t), 0});
    }
    return measure(rings).volume;
}

} // namespace

ShellMesh shell(double ra, double rb, Vec separation, double progress) {
    const double distance = length(separation);
    const Vec axis = distance > 1e-9 ? unit(separation) : Vec{{0, 1, 0}};
    const Vec side = unit(cross(axis, std::abs(axis[1]) < 0.9 ? Vec{{0, 1, 0}} : Vec{{1, 0, 0}}));
    const Vec around = cross(axis, side);
    const double left = std::min(-ra, distance - rb);
    const double right = std::max(ra, distance + rb);
    // Внутри узкой полосы около стыка сглаживаем максимум квадратов радиусов сечения.
    // За пределами полосы профиль и нормаль совпадают с исходными сферами.
    auto difference = [&](double x) { return ra * ra - rb * rb + distance * (distance - 2 * x); };
    const double neck =
        std::min({ra * rb, distance * std::min(ra, rb), std::abs(difference(left)), std::abs(difference(right))}) *
        0.85 * smooth(5.0 * progress);
    auto sample = [&](double x) {
        const double a = ra * ra - x * x;
        const double b = rb * rb - (x - distance) * (x - distance);
        const double mix = neck > 1e-12 ? std::clamp(0.5 + (a - b) / (2 * neck), 0.0, 1.0) : double(a >= b);
        const double square = std::lerp(b, a, mix) + neck * mix * (1 - mix);
        return Ring{x, std::sqrt(std::max(0.0, square)), x - distance * (1 - mix)};
    };
    std::vector<Ring> rings;
    rings.reserve(slices + 2);
    for (int i = 0; i <= slices; ++i) {
        const double f = 0.5 - 0.5 * std::cos(std::numbers::pi * i / slices);
        rings.push_back(sample(std::lerp(left, right, f)));
    }
    rings.front().radius = rings.back().radius = 0;
    if (distance > 1e-7 && rb > 0) {
        const double seam = (ra * ra - rb * rb + distance * distance) / (2 * distance);
        auto after = std::ranges::lower_bound(rings, seam, {}, &Ring::x);
        if (after != rings.end() && after != rings.begin() && after->x - seam > 1e-6 && seam - (after - 1)->x > 1e-6) {
            rings.insert(after, sample(seam));
        }
    }
    const auto [volume, moment] = measure(rings);
    static const double unitVolume = sphereVolume();
    const double scale = std::cbrt(unitVolume * (ra * ra * ra + rb * rb * rb) / volume);
    const double center = moment / volume;
    std::array<Vec, sides> radial{};
    for (int j = 0; j < sides; ++j) {
        const double angle = 2 * std::numbers::pi * j / sides;
        radial[j] = side * std::cos(angle) + around * std::sin(angle);
    }
    ShellMesh result;
    result.mesh.positions.reserve(rings.size() * sides);
    result.normals.reserve(rings.size() * sides);
    result.mesh.triangles.reserve((rings.size() - 1) * sides * 2);
    for (std::size_t i = 0; i < rings.size(); ++i) {
        const auto& r = rings[i];
        for (int j = 0; j < sides; ++j) {
            const Vec pos = (axis * (r.x - center) + radial[j] * r.radius) * scale;
            const auto normal = unit(axis * r.axialNormal + radial[j] * r.radius);
            result.mesh.positions.push_back({pos.components});
            result.normals.push_back(normal);
            if (i > 0) {
                const auto a = static_cast<std::uint32_t>((i - 1) * sides + j);
                const auto b = static_cast<std::uint32_t>((i - 1) * sides + (j + 1) % sides);
                if (i != 1) {
                    result.mesh.triangles.push_back({{a, b, a + sides}});
                }
                if (i + 1 != rings.size()) {
                    result.mesh.triangles.push_back({{b, b + sides, a + sides}});
                }
            }
        }
    }
    return result;
}

} // namespace agar
