#pragma once

#include "core/camera.h"
#include "core/shell.h"
#include "gl.h"

namespace agar {
inline constexpr std::array<Vec, 5> cellColors{
    {{{0.055, 0.85, 0.73}}, {{1.0, 0.36, 0.31}}, {{0.65, 0.40, 0.95}}, {{0.24, 0.65, 1}}, {{1, 0.72, 0.22}}}};

class CellRenderer {
public:
    explicit CellRenderer(const GlApi& gl);
    ~CellRenderer();
    void draw(const Match& match, const Camera& camera, int width, int height);
    void preview(Vec tint, double viewX, double viewY, double radius, double aspect);

private:
    struct Mesh {
        GLuint vao{}, vertices{}, indices{};
        GLsizei count{};
    };
    void upload(Mesh& target, const ShellMesh& shell, GLenum usage);
    void begin(double aspect);
    void captureSceneDepth(int width, int height);
    void cell(const Mesh& mesh, Vec center, Vec tint, double radius, double toUnit, const Deformation& skin,
              const Camera& camera, double time, int id);
    Program m_program;
    GLint m_projection, m_center, m_tint, m_radius, m_toUnit, m_count, m_contact, m_wave, m_drape, m_flow, m_lag,
        m_broadside, m_clockPhase, m_layerPass;
    GLuint m_sceneDepth{};
    int m_depthWidth{}, m_depthHeight{};
    Mesh m_sphere, m_fusion;
};
} // namespace agar
