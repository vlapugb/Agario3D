#include "view.h"

#include "huynity/core/render_preparation.h"
#include "huynity/objectlib/operations/tessellate.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace agar {
namespace math = huynity::math;
namespace obj = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace {

auto material(render::Color color, bool unlit = false) {
    return std::make_shared<const render::Material>(render::Material{.color = color, .unlit = unlit});
}

obj::TriangleMesh3Params quill() {
    constexpr unsigned facets = 7;
    obj::TriangleMesh3Params mesh;
    mesh.positions.push_back({{0, 0, 1}});
    for (unsigned j = 0; j < facets; ++j) {
        const double angle = 2 * std::numbers::pi * j / facets;
        mesh.positions.push_back({{std::cos(angle), std::sin(angle), 0}});
        mesh.triangles.push_back({{1 + j, 1 + (j + 1) % facets, 0}});
    }
    return mesh;
}

math::Transform3 quillPose(const Spike::Needle& needle, Vec bend) {
    const Vec root = needle.axis * Spike::base, tip = needle.tip + bend - root;
    const Vec x = unit(cross(needle.axis, Vec{{0, 1, 0}})) * Spike::width, y = cross(needle.axis, x);
    return {{x[0], y[0], tip[0], root[0], x[1], y[1], tip[1], root[1], x[2], y[2], tip[2], root[2]}};
}

obj::TriangleMesh3Params thorn(obj::TriangleMesh3Params mesh, const obj::TriangleMesh3Params& quill) {
    for (const auto& needle : Spike::crown()) {
        const auto start = static_cast<std::uint32_t>(mesh.positions.size());
        auto pose = quillPose(needle, {});
        for (const auto& p : quill.positions) {
            mesh.positions.push_back(math::transformPoint(pose, p));
        }
        for (auto triangle : quill.triangles) {
            for (auto& index : triangle.indices) {
                index += start;
            }
            mesh.triangles.push_back(triangle);
        }
    }
    return mesh;
}

void dust(huynity::core::Scene& scene) {
    std::mt19937 rng{914};
    std::uniform_real_distribution<double> uniform{-1, 1};
    const std::array<Vec, 6> corners{{{{1, 0, 0}}, {{0, 0, 1}}, {{-1, 0, 0}}, {{0, 0, -1}}, {{0, 1, 0}}, {{0, -1, 0}}}};
    for (int layer = 0; layer < 2; ++layer) {
        obj::TriangleMesh3Params mesh;
        for (int i = 0; i < 490; ++i) {
            const Vec axis = unit({{uniform(rng), uniform(rng), uniform(rng)}});
            const double distance = 20 + (uniform(rng) + 1) * 95;
            const auto center = Vec{{0, 0, -42}} + axis * distance;
            const double size = (0.00075 + 0.00034 * layer) * distance + 0.018;
            const auto a = static_cast<std::uint32_t>(mesh.positions.size());
            for (auto c : corners) {
                mesh.positions.push_back({(center + c * size).components});
            }
            for (unsigned j = 0; j < 4; ++j) {
                const auto k = (j + 1) % 4;
                mesh.triangles.push_back({{a + j, a + 4, a + k}});
                mesh.triangles.push_back({{a + k, a + 5, a + j}});
            }
        }
        const auto id = scene.createObject<obj::ObjectType::TriangleMesh3>(mesh).value();
        scene
            .setMaterial(
                id, material(layer ? render::Color{0.45, 0.53, 0.60, 1} : render::Color{0.25, 0.32, 0.38, 1}, true))
            .value();
    }
}

math::Transform3 place(Vec position, Vec scale) {
    return math::compose(math::translation(position), math::scaling(scale));
}

Vec uniformSize(double size) {
    return {{size, size, size}};
}

} // namespace

View::View(huynity::renderer::GlRenderer::GlProcResolver resolver)
    : m_renderer(resolver), m_gl(resolver), m_cells(m_gl), m_hud(m_gl) {
    m_pellet = m_scene.createObject<obj::ObjectType::Sphere3>({0.27}).value();
    m_scene.setShading(m_pellet, render::ShadingModel::Gouraud).value();
    m_foodColors = {material({1, 0.74, 0.28, 1}), material({0.80, 0.62, 1, 1}), material({0.40, 0.78, 1, 1})};
    const auto sphere = obj::tessellate(obj::Sphere3::create({Spike::core}).value(), {.maxDeviation = .025}).value();
    obj::TriangleMesh3Params core{{sphere.positions().begin(), sphere.positions().end()},
                                  {sphere.triangles().begin(), sphere.triangles().end()}};
    auto needle = quill();
    m_spike = m_scene.createObject<obj::ObjectType::TriangleMesh3>(thorn(core, needle)).value();
    m_core = m_scene.createObject<obj::ObjectType::TriangleMesh3>(std::move(core)).value();
    m_quill = m_scene.createObject<obj::ObjectType::TriangleMesh3>(std::move(needle)).value();
    const auto lime = material({.66, 1, .19, 1});
    for (auto id : {m_spike, m_core, m_quill}) {
        m_scene.setMaterial(id, lime).value();
        m_scene.setVisibility(id, false).value();
    }
    m_scene.setVisibility(m_pellet, false).value();
    dust(m_scene);
}

View::Id View::instance(std::vector<Id>& pool, std::size_t index, Id source) {
    if (index == pool.size()) {
        pool.push_back(m_scene.createInstance(source).value());
    }
    m_scene.setVisibility(pool[index], true).value();
    return pool[index];
}

void View::hideTail(const std::vector<Id>& pool, std::size_t used) {
    for (; used < pool.size(); ++used) {
        m_scene.setVisibility(pool[used], false).value();
    }
}

void View::drawArena(const Match& match) {
    auto& scene = m_scene;
    for (std::size_t i = 0; i < match.food.size(); ++i) {
        const auto& f = match.food[i];
        const auto id = instance(m_food, i, m_pellet);
        scene.setLocalTransform(id, place(f.position, uniformSize(f.size() / 0.27))).value();
        scene.setMaterial(id, m_foodColors[f.mass == 5 ? 0 : (f.mass == 2 ? 1 : 2)]).value();
    }
    hideTail(m_food, match.food.size());
    std::size_t visible{}, cores{}, quills{};
    for (const auto& spike : match.spikes) {
        if (!spike.active) {
            continue;
        }
        auto pose = place(spike.position, uniformSize(radius(Spike::mass)));
        if (!spike.flexing) {
            scene.setLocalTransform(instance(m_spikes, visible++, m_spike), pose).value();
            continue;
        }
        scene.setLocalTransform(instance(m_cores, cores++, m_core), pose).value();
        for (int i = 0; i < Spike::needleCount; ++i) {
            const auto local = quillPose(Spike::crown()[i], spike.bends[i].offset());
            scene.setLocalTransform(instance(m_quills, quills++, m_quill), math::compose(pose, local)).value();
        }
    }
    hideTail(m_spikes, visible);
    hideTail(m_cores, cores);
    hideTail(m_quills, quills);
}

void View::draw(const Match& match, const Camera& camera, const Menu& menu, int width, int height) {
    const bool arena = menu.screen == Screen::Playing || menu.screen == Screen::Pause || menu.screen == Screen::Result;
    if (arena) {
        drawArena(match);
    } else if (m_showingArena) {
        hideTail(m_food, 0);
        hideTail(m_spikes, 0);
        hideTail(m_cores, 0);
        hideTail(m_quills, 0);
    }
    m_showingArena = arena;
    const Camera still;
    const auto& eye = arena ? camera : still;
    const huynity::core::PassesConfig passes{
        .camera3 = render::PerspectiveCamera3{.worldToView = eye.worldToView(),
                                              .verticalFieldOfView = std::numbers::pi / 3,
                                              .nearPlane = .06,
                                              .farPlane = 480},
        .camera2 = {},
        .hudEnabled = false,
        .lighting =
            {.ambient = {.color = {.96, .98, 1, 1}, .intensity = .53},
             .directional = {{.direction = unit({{.4, -.7, .55}}), .color = {1, .98, .95, 1}, .intensity = .68}},
             .fog = {.enabled = true, .color = {0, 0, 0, 1}, .start = 95, .end = 330}},
        .tessellation = {}};
    auto frame = huynity::core::RenderPreparation::prepareSnapshot(m_scene, passes, width, height).value();
    frame.clearColor = {0, 0, 0, 1};
    m_renderer.render(frame).value();
    const double aspect = double(width) / height;
    if (arena) {
        m_cells.draw(match, camera, width, height);
    } else {
        const MenuLayout layout(width, height);
        const double units = 16 * std::tan(std::numbers::pi / 6) / height;
        const double x = (layout.x + 729 * layout.scale - width * .5) * units;
        const double y = (height * .5 - layout.y - 314 * layout.scale) * units;
        m_cells.preview(cellColors[menu.screen == Screen::Create ? menu.draft.color : 0], x, y,
                        98 * layout.scale * units, aspect);
    }
    m_hud.draw(match, menu, width, height);
}
} // namespace agar
