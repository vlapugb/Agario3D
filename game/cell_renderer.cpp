#include "cell_renderer.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "menu.h"

namespace agar {
namespace {
constexpr const char* vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
uniform vec3 center;
uniform vec4 projection;
uniform float radius, toUnit;
uniform int count, layerPass;
uniform vec3 flow, lag, broadside;
uniform vec2 clockPhase;
uniform vec4 contact[6], wave[6]; // xyz: normal / tangential drag, w: dent / side wave
uniform vec4 drape[6]; // amount, interface offset and smaller cell radius, in local radii
out vec3 surfaceNormal, restNormal;
out float distanceZ, film;

vec3 flightShape(vec3 p, vec3 d) {
    float speed = min(length(flow), 1.2);
    vec3 axis = flow / max(length(flow), 1e-6);
    vec3 wide = broadside - axis*dot(broadside,axis);
    wide /= max(length(wide),1e-6);
    vec3 thin = cross(axis,wide);
    float h=dot(d,axis), w=dot(d,wide), v=dot(d,thin);
    float braking=dot(lag,axis);
    float axial=1.+.19*speed-.80*braking, span=1.+.19*speed+.52*braking;
    // Three reciprocal scales conserve volume; the fan is visible looking straight down the wake.
    vec3 result=p + axis*((axial-1.)*dot(p,axis)) + wide*((span-1.)*dot(p,wide))
                  + thin*((1./(axial*span)-1.)*dot(p,thin));
    result += speed*(.16*h*(p-axis*dot(p,axis)) - axis*(.18*(h*h-.2)));
    result += lag*(.18+.98*(1.-h)*(1.-h)*.25);
    float ripple=sin(3.6*w+1.4*v-2.4*h-clockPhase.x+clockPhase.y)
               + .55*sin(-2.1*w+4.3*v+1.6*h+.68*clockPhase.x-clockPhase.y);
    return result + d*(.032*speed*ripple);
}

vec3 warp(vec3 p, out float coating) {
    vec3 d = normalize(p + vec3(0.,0.,1e-8)), result=flightShape(p,d), wall=vec3(0.);
    coating=0.;
    for (int i = 0; i < count; ++i) {
        vec3 n = contact[i].xyz;
        float c = dot(d, n);
        float cap = mix(pow(max(c, 0.), 4.), smoothstep(-.20, .80, c), .50);
        vec3 side = d - c * n;
        float crest = .03 + .65 * (contact[i].w - wave[i].w);
        float belt = exp(-9. * (c-crest) * (c-crest));
        vec3 squeeze = side*(.64*wave[i].w*belt) - n*(contact[i].w*cap);
        if (drape[i].w > .5) {
            float point = pow(max(c, 0.), 18.);
            float lip = exp(-100.*(c-.87)*(c-.87));
            wall += -n*(contact[i].w*point) + side*(.33*wave[i].w*lip)
                    + wave[i].xyz*(.23*contact[i].w*point);
        } else if (drape[i].z > 0.) {
            vec3 slide=wave[i].xyz;
            float wrap=drape[i].x, size=drape[i].z;
            float lip=exp(-5.*(c-.59)*(c-.59))*smoothstep(-.25,.66,dot(d,slide));
            result += .48*squeeze + wrap*size*(slide*(.23*cap+.19*lip) + n*(.48*lip) + .12*side*cap);
            // Both cells share a curved seam. Each leading lip runs up the other's shoulder.
            float u=clamp(dot(result,slide)/size,-.94,.94);
            float arc=.22*u + sign(u)*(1.-sqrt(1.-u*u));
            float over=dot(result,n)-(drape[i].y+.80*size*arc);
            result -= n*(wrap*.5*(over+sqrt(over*over+.0025*size*size)));
            coating=max(coating,wrap*lip);
        } else {
            wall += squeeze - n*(.035*(contact[i].w-wave[i].w)*(1.-cap))
                    + wave[i].xyz*(.30*contact[i].w*cap + .16*wave[i].w*belt);
        }
    }
    return result + wall + d*max(0.,-dot(wall,d)-.33);
}
vec3 warp(vec3 p) {
    float unused;
    return warp(p,unused);
}
void main() {
    vec3 p = position * toUnit;
    surfaceNormal = normal;
    film=0.;
    if (count > 0 || dot(flow,flow)+dot(lag,lag) > 1e-9) {
        if (layerPass != 1) {
            vec3 t = normalize(cross(normal, abs(normal.y)<.85 ? vec3(0,1,0) : vec3(1,0,0)));
            vec3 b = cross(normal,t);
            const float e = .002;
            surfaceNormal = normalize(cross(warp(p + e*t)-warp(p - e*t),warp(p + e*b)-warp(p - e*b)));
        }
        p = warp(p,film);
    }
    vec3 v = center + radius * p;
    restNormal = normal;
    distanceZ = v.z;
    gl_Position = vec4(v.xy*projection.xy,v.z*projection.z+projection.w,v.z);
})";

constexpr const char* fragment = R"(#version 330 core
in vec3 surfaceNormal, restNormal;
in float distanceZ, film;
uniform vec3 tint;
uniform sampler2D sceneDepth;
uniform int layerPass;
layout(location=0,index=0) out vec4 color;
layout(location=0,index=1) out vec4 transmission;
float bubble(vec2 p, vec2 center, float size) {
    vec2 q = (p-center)/size;
    float r = length(q);
    return .14*exp(-42.*pow(r-.79,2.)) - .055*exp(-3.*r*r)
         + .28*exp(-18.*dot(q-vec2(-.27,.34),q-vec2(-.27,.34)));
}
void main() {
    if (layerPass == 1) {color=transmission=vec4(0.); return;}
    if (layerPass == 2 && gl_FragCoord.z >= texelFetch(sceneDepth,ivec2(gl_FragCoord.xy),0).r) discard;
    vec3 n=normalize(surfaceNormal)*vec3(1.,1.,-1.);
    float nz=clamp(n.z,0.,1.);
    float key=max(0.,dot(n,normalize(vec3(-.46,.62,.66))));
    float rim=pow(1.-nz,2.6);
    float wet=max(0.,dot(normalize(vec3(n.x*.82,n.y*1.23,n.z)),normalize(vec3(-.43,.56,.71))));
    float glint=.30*pow(wet,44.)+.09*pow(wet,10.);
    float edge=pow(1.-nz,3.2);
    vec3 body=tint*(.26+.38*key+.11*nz+.30*rim)
             + glint*mix(vec3(1.),tint,.48) + edge*mix(vec3(.09),tint,.52);
    vec2 p=normalize(restNormal).xy;
    float air=bubble(p,vec2(-.38,-.12),.075)+bubble(p,vec2(.24,.38),.055)
             +bubble(p,vec2(.47,-.27),.105)+bubble(p,vec2(-.58,.22),.041);
    body += (tint*.22+vec3(.16))*air;
    // The chord through a sphere is proportional to the facing cosine.
    float thickness=clamp(-normalize(restNormal).z,0.,1.)*(1.-.65*film);
    body*=1.-.22*film;
    vec3 absorption=mix(vec3(2.5),vec3(.95),tint);
    transmission=vec4(.78*exp(-absorption*(.09+thickness*1.18)),1.);
    color=vec4(body*(1.-smoothstep(95.,330.,distanceZ)),1.);

})";
struct Vertex {
    float pos[3], n[3];
};
void vectorUniform(const GlApi& gl, GLint id, Vec v) {
    gl.Uniform3f(id, v[0], v[1], v[2]);
}
} // namespace

CellRenderer::CellRenderer(const GlApi& api)
    : m_program(api, vertex, fragment), m_projection(m_program.uniform("projection")),
      m_center(m_program.uniform("center")), m_tint(m_program.uniform("tint")), m_radius(m_program.uniform("radius")),
      m_toUnit(m_program.uniform("toUnit")), m_count(m_program.uniform("count")),
      m_contact(m_program.uniform("contact")), m_wave(m_program.uniform("wave")), m_drape(m_program.uniform("drape")),
      m_flow(m_program.uniform("flow")), m_lag(m_program.uniform("lag")), m_broadside(m_program.uniform("broadside")),
      m_clockPhase(m_program.uniform("clockPhase")), m_layerPass(m_program.uniform("layerPass")) {
    for (auto mesh : {&m_sphere, &m_fusion}) {
        m_program.gl.GenVertexArrays(1, &mesh->vao);
        m_program.gl.GenBuffers(1, &mesh->vertices);
        m_program.gl.GenBuffers(1, &mesh->indices);
        m_program.gl.BindVertexArray(mesh->vao);
        m_program.gl.BindBuffer(GL_ARRAY_BUFFER, mesh->vertices);
        m_program.gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->indices);
        m_program.gl.EnableVertexAttribArray(0);
        m_program.gl.EnableVertexAttribArray(1);
        m_program.gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        m_program.gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                         reinterpret_cast<void*>(offsetof(Vertex, n)));
    }
    upload(m_sphere, shell(1, 0, {}, 1), GL_STATIC_DRAW);
}
CellRenderer::~CellRenderer() {
    m_program.gl.DeleteTextures(1, &m_sceneDepth);
    for (auto mesh : {&m_sphere, &m_fusion}) {
        m_program.gl.DeleteBuffers(1, &mesh->vertices);
        m_program.gl.DeleteBuffers(1, &mesh->indices);
        m_program.gl.DeleteVertexArrays(1, &mesh->vao);
    }
}
void CellRenderer::upload(Mesh& target, const ShellMesh& s, GLenum usage) {
    std::vector<Vertex> v(s.normals.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            v[i].pos[k] = float(s.mesh.positions[i][k]);
            v[i].n[k] = float(s.normals[i][k]);
        }
    }
    const auto& triangles = s.mesh.triangles;
    static_assert(sizeof(triangles[0]) == 3 * sizeof(GLuint));
    m_program.gl.BindVertexArray(target.vao);
    m_program.gl.BindBuffer(GL_ARRAY_BUFFER, target.vertices);
    m_program.gl.BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), usage);
    m_program.gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, triangles.size() * sizeof(triangles[0]), triangles.data(), usage);
    target.count = GLsizei(triangles.size() * 3);
}
void CellRenderer::begin(double aspect) {
    m_program.gl.UseProgram(m_program.id);
    m_program.gl.Uniform1i(m_layerPass, 0);
    constexpr double nearPlane = .06, farPlane = 480, y = 1.7320508075688772;
    m_program.gl.Uniform4f(m_projection, y / aspect, y, (farPlane + nearPlane) / (farPlane - nearPlane),
                           -2 * farPlane * nearPlane / (farPlane - nearPlane));
    m_program.gl.Enable(GL_DEPTH_TEST);
    m_program.gl.DepthMask(GL_FALSE);
    m_program.gl.DepthFunc(GL_LESS);
    m_program.gl.Enable(GL_BLEND);
    // Add the shell light and transmit the existing framebuffer per color channel.
    m_program.gl.BlendFunc(GL_ONE, GL_SRC1_COLOR);
    m_program.gl.Enable(GL_CULL_FACE);
    m_program.gl.CullFace(GL_BACK);
    m_program.gl.FrontFace(GL_CW);
}

void CellRenderer::captureSceneDepth(int width, int height) {
    const auto& gl = m_program.gl;
    gl.ActiveTexture(GL_TEXTURE0);
    if (!m_sceneDepth) {
        gl.GenTextures(1, &m_sceneDepth);
    }
    gl.BindTexture(GL_TEXTURE_2D, m_sceneDepth);
    if (width != m_depthWidth || height != m_depthHeight) {
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.CopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 0, 0, width, height, 0);
        m_depthWidth = width;
        m_depthHeight = height;
    } else {
        gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    }
}
void CellRenderer::cell(const Mesh& mesh, Vec center, Vec tint, double size, double toUnit, const Deformation& skin,
                        const Camera& camera, double time, int id) {
    vectorUniform(m_program.gl, m_flow, camera.toView(skin.flow) * (1 / BASE_SPEED));
    vectorUniform(m_program.gl, m_lag, camera.toView(skin.lag));
    vectorUniform(m_program.gl, m_broadside, camera.toView(skin.broadside));
    m_program.gl.Uniform2f(m_clockPhase, 5 * time, 2.39996 * id);
    std::array<float, 24> contacts{}, waves{}, drapes{};
    int count{};
    for (const auto& p : skin.patches) {
        if (!p.key) {
            continue;
        }
        auto n = camera.toView(p.normal), t = camera.toView(p.slip);
        for (int k = 0; k < 3; ++k) {
            contacts[4 * count + k] = float(n[k]);
            waves[4 * count + k] = float(t[k]);
        }
        contacts[4 * count + 3] = float(p.dent.value);
        waves[4 * count + 3] = float(p.side.value);
        drapes[4 * count] = float(p.wrap);
        drapes[4 * count + 1] = float(p.seam);
        drapes[4 * count + 2] = float(p.contactScale);
        drapes[4 * count + 3] = needleContact(p.key) ? 1.0f : 0.0f;
        ++count;
    }
    m_program.gl.Uniform1i(m_count, count);
    if (count) {
        m_program.gl.Uniform4fv(m_contact, count, contacts.data());
        m_program.gl.Uniform4fv(m_wave, count, waves.data());
        m_program.gl.Uniform4fv(m_drape, count, drapes.data());
    }
    vectorUniform(m_program.gl, m_center, center);
    vectorUniform(m_program.gl, m_tint, tint);
    m_program.gl.Uniform1f(m_radius, size);
    m_program.gl.Uniform1f(m_toUnit, toUnit);
    m_program.gl.BindVertexArray(mesh.vao);
    m_program.gl.DrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
}
void CellRenderer::preview(Vec tint, double x, double y, double size, double aspect) {
    begin(aspect);
    cell(m_sphere, {{x, y, 8}}, tint, size, 1, {}, Camera{}, 0, 0);
    m_program.gl.DepthMask(GL_TRUE);
    m_program.gl.Disable(GL_CULL_FACE);
}
void CellRenderer::draw(const Match& match, const Camera& camera, int width, int height) {
    struct Draw {
        Vec center, tint;
        const Cell* a;
        const Cell* b;
        double bound;
        std::optional<ShellMesh> fusion;
    };
    const double aspect = double(width) / height;
    std::vector<Draw> visible;
    bool overlapping = false;
    auto queue = [&](Vec p, Vec tint, const Cell& a, const Cell* b, double progress) {
        auto v = camera.toView(p - camera.eye());
        const double bound = radius(a.mass) * a.skin.extent() + (b ? radius(b->mass) * b->skin.extent() : 0);
        if (v[2] + bound > .06 && std::abs(v[0]) < v[2] * .578 * aspect + bound * (1 + aspect) &&
            std::abs(v[1]) < v[2] * .578 + 2 * bound) {
            overlapping = overlapping || std::ranges::any_of(visible, [&](const Draw& other) {
                              const double reach = bound + other.bound;
                              return squared(v - other.center) < reach * reach;
                          });
            auto& draw = visible.emplace_back(Draw{v, tint, &a, b, bound, {}});
            if (b) {
                draw.fusion = shell(radius(a.mass), radius(b->mass), camera.toView(b->position - a.position), progress);
            }
        }
    };
    for (std::size_t i = 0; i < match.groups.size(); ++i) {
        const auto& g = match.groups[i];
        const auto tint = cellColors[i == 0 ? match.session.color : (i % 2 ? 1 : 2)];
        for (const auto& a : g.cells) {
            if (std::ranges::none_of(g.fusions, [&](const Fusion& f) { return f.first == a.id || f.second == a.id; })) {
                queue(a.position, tint, a, nullptr, 0);
            }
        }
        for (const auto& f : g.fusions) {
            const auto& a = *g.find(f.first);
            const auto& b = *g.find(f.second);
            queue(a.position + (b.position - a.position) * (double(b.mass) / (a.mass + b.mass)), tint, a, &b,
                  f.progress());
        }
    }
    std::ranges::sort(visible, {}, [](const Draw& d) { return -d.center[2]; });
    begin(aspect);
    const auto drawCells = [&] {
        for (const auto& d : visible) {
            if (!d.b) {
                cell(m_sphere, d.center, d.tint, radius(d.a->mass), 1, d.a->skin, camera, -match.remaining, d.a->id);
                continue;
            }
            const auto& a = *d.a;
            const auto& b = *d.b;
            const double size = radius(a.mass + b.mass);
            upload(m_fusion, *d.fusion, GL_STREAM_DRAW);
            cell(m_fusion, d.center, d.tint, size, 1 / size, blend(a.skin, b.skin, double(b.mass) / (a.mass + b.mass)),
                 camera, -match.remaining, a.id);
        }
    };
    const auto& gl = m_program.gl;
    if (overlapping) {
        // A lip can pass in front of the other shell while its center is still behind it.
        // Keep opaque occluders, find the nearest skin per pixel, then composite that skin last.
        captureSceneDepth(width, height);
        gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl.DepthMask(GL_TRUE);
        gl.Uniform1i(m_layerPass, 1);
        drawCells();
        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl.DepthMask(GL_FALSE);
        gl.Uniform1i(m_layerPass, 2);
        gl.DepthFunc(GL_GREATER);
        drawCells();
        gl.DepthFunc(GL_EQUAL);
    }
    drawCells();
    gl.DepthFunc(GL_LESS);
    m_program.gl.DepthMask(GL_TRUE);
    m_program.gl.Disable(GL_CULL_FACE);
}
} // namespace agar
