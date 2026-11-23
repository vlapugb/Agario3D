#pragma once

#include "huynity/objectlib/params/triangle_mesh_params.h"

#include "match.h"

namespace agar {

struct ShellMesh {
    huynity::objectlib::TriangleMesh3Params mesh;
    std::vector<Vec> normals;
};

/// @brief Поверхность двух сближающихся капель в координатах камеры вокруг барицентра.
/// При нулевом расстоянии это сфера суммарного объёма.
ShellMesh shell(double firstRadius, double secondRadius, Vec separation, double progress);

} // namespace agar
