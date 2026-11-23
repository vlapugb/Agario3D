#pragma once
#include "huynity/core/scene.h"
#include "huynity/renderer/gl_renderer.h"

#include "cell_renderer.h"
#include "hud.h"

namespace agar {
class View {
public:
    explicit View(huynity::renderer::GlRenderer::GlProcResolver resolver);
    void draw(const Match& match, const Camera& camera, const Menu& menu, int width, int height);

private:
    using Id = huynity::core::ObjectId;
    using Material = std::shared_ptr<const huynity::render_contracts::Material>;
    void drawArena(const Match& match);
    Id instance(std::vector<Id>& pool, std::size_t index, Id source);
    void hideTail(const std::vector<Id>& pool, std::size_t used);
    huynity::core::Scene m_scene;
    huynity::renderer::GlRenderer m_renderer;
    GlApi m_gl;
    CellRenderer m_cells;
    Hud m_hud;
    Id m_pellet, m_spike, m_core, m_quill;
    std::array<Material, 3> m_foodColors;
    std::vector<Id> m_food, m_spikes, m_cores, m_quills;
    bool m_showingArena{};
};
} // namespace agar
