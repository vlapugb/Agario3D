#include "match.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <tuple>

namespace agar {
namespace {
double flightLimit(int mass) {
    return BASE_SPEED * std::clamp(std::pow(double(START_MASS) / std::max(1, mass), .25), .45, 1.35);
}

double mergeDuration(int pairMass) {
    return std::clamp(BASE_MERGE_DURATION * std::sqrt(double(pairMass) / START_MASS), .30, 2.4);
}
} // namespace

double radius(int mass) {
    return 0.94 * std::cbrt(double(mass) / START_MASS);
}

double smooth(double fraction) {
    const double u = std::clamp(fraction, 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

double cohesion(double flightFraction) {
    return 1.0 - smooth(flightFraction);
}

double squared(Vec vector) {
    return dot(vector, vector);
}

Vec unit(Vec vector) {
    const auto size = length(vector);
    return size > 1e-12 ? vector * (1.0 / size) : Vec{{0, 0, 1}};
}

int Group::mass() const {
    int sum{};
    for (const auto& cell : cells) {
        sum += cell.mass;
    }
    return sum;
}

double Group::speedLimit() const {
    return cells.empty() ? 0.0 : flightLimit(mass());
}

double Group::speedFraction() const {
    return cells.empty() ? 0.0 : std::clamp(length(flight) / speedLimit(), 0.0, 1.0);
}

Vec Group::center() const {
    Vec sum{};
    int total{};
    for (const auto& cell : cells) {
        sum = sum + cell.position * cell.mass;
        total += cell.mass;
    }
    return total ? sum * (1.0 / total) : Vec{};
}

const Cell* Group::find(int id) const {
    const auto it = std::ranges::find(cells, id, &Cell::id);
    return it == cells.end() ? nullptr : &*it;
}

double Food::size() const {
    return mass == 5 ? 0.27 : (mass == 2 ? 0.195 : 0.14);
}

namespace {

constexpr Vec spawn{{0, 0, -42}};
constexpr double massRetainedPerMinute = .90;

void loseMass(Group& group, double fraction) {
    const int total = group.mass(), floor = static_cast<int>(group.cells.size());
    double pending{};
    for (auto& cell : group.cells) {
        cell.pendingLoss += (cell.mass - cell.pendingLoss) * fraction;
        pending += cell.pendingLoss;
    }
    const int units = std::clamp(static_cast<int>(std::floor(pending + 1e-10)), 0, total - floor);
    // Списываем целые единицы с частей с наибольшим остатком. Деление не даёт отсрочки за счёт округления.
    for (int i = 0; i < units; ++i) {
        auto& cell = *std::ranges::max_element(group.cells, {}, [](const Cell& c) {
            return c.mass > 1 ? c.pendingLoss : -std::numeric_limits<double>::infinity();
        });
        --cell.mass;
        cell.pendingLoss -= 1;
    }
    if (total - units == floor) {
        for (auto& cell : group.cells) {
            cell.pendingLoss = 0;
        }
    }
    // При естественной убыли сохраняем долю тяги, в том числе строй на полном ходу.
    if (units > 0) {
        group.flight = group.flight * (flightLimit(total - units) / flightLimit(total));
    }
}

Vec inside(Vec position, double size) {
    const double edge = ARENA_HALF - size;
    for (double& value : position.components) {
        value = std::clamp(value, -edge, edge);
    }
    return position;
}

bool canEat(int hunter, int prey) {
    return hunter * 4 >= prey * 5;
}

bool involved(const Fusion& pair, int id) {
    return pair.first == id || pair.second == id;
}

void cancelFusion(Group& group, int id) {
    std::erase_if(group.fusions, [id](const Fusion& pair) { return involved(pair, id); });
}

bool fusing(const Group& group, int id) {
    return std::ranges::any_of(group.fusions, [id](const Fusion& pair) { return involved(pair, id); });
}

std::vector<std::size_t> splitCandidates(const Group& group) {
    std::vector<std::size_t> eligible;
    for (std::size_t i = 0; i < group.cells.size(); ++i) {
        if (group.cells[i].mass >= 36) {
            eligible.push_back(i);
        }
    }
    std::ranges::stable_sort(eligible, std::greater<>{}, [&](auto index) { return group.cells[index].mass; });
    eligible.resize(std::min(eligible.size(), CELL_LIMIT - group.cells.size()));
    return eligible;
}

double fusionMargin(const Group& group, const Cell& cell) {
    for (const auto& pair : group.fusions) {
        if (involved(pair, cell.id)) {
            const auto* partner = group.find(pair.first == cell.id ? pair.second : pair.first);
            return (radius(cell.mass + partner->mass) - radius(std::max(cell.mass, partner->mass))) *
                   smooth(pair.progress());
        }
    }
    return 0;
}

void arm(Cell& cell) {
    cell.skin.patches = {};
    cell.cooldown = 12.0 + 0.15 * cell.mass;
    cell.position = inside(cell.position, radius(cell.mass));
    cell.stepOrigin = cell.position;
}

Vec steer(Vec flight, Command command, double dt, double topSpeed) {
    const double speed = length(flight);
    if (!command.thrust) {
        const double next = std::max(0.0, speed - dt * topSpeed / 2.0);
        return speed > 0.0 ? flight * (next / speed) : Vec{};
    }
    // При повороте сохраняется инерция. Прямолинейный разгон остаётся линейным.
    flight = flight + (command.direction * speed - flight) * -std::expm1(-4.5 * dt);
    const Vec target = command.direction * topSpeed;
    const Vec delta = target - flight;
    const double acceleration = topSpeed * dt / 5.0;
    const double distance = length(delta);
    return distance <= acceleration ? target : flight + delta * (acceleration / distance);
}

constexpr double recoilRange = 0.018;
int wallKey(int axis, double sign) {
    return -1 - 2 * axis - int(sign > 0);
}
bool heldContact(const Cell& cell, int key) {
    const auto patch = std::ranges::find(cell.skin.patches, key, &ContactPatch::key);
    return patch != cell.skin.patches.end() && patch->held;
}

/// @brief Проецирует только общее перемещение на допустимое смещение всей группы.
Vec fly(Group& group, Command command, double dt) {
    Vec lower{{-2 * ARENA_HALF, -2 * ARENA_HALF, -2 * ARENA_HALF}}, upper = lower * -1;
    std::array<bool, 3> blocked{};
    for (const auto& cell : group.cells) {
        const double size = radius(cell.mass);
        const double edge = ARENA_HALF - size - fusionMargin(group, cell);
        for (int axis = 0; axis < 3; ++axis) {
            const double sign = std::copysign(1.0, command.direction[axis]);
            const double gap = edge - sign * cell.position[axis];
            if (command.direction[axis] != 0 && gap <= recoilRange * size &&
                (gap <= 1e-8 || heldContact(cell, wallKey(axis, sign)))) {
                blocked[axis] = true;
            }
            lower[axis] = std::max(lower[axis], std::min(0.0, -edge - cell.position[axis]));
            upper[axis] = std::min(upper[axis], std::max(0.0, edge - cell.position[axis]));
        }
    }
    const double limit = group.speedLimit();
    const double speed = length(group.flight);
    if (speed > limit) {
        group.flight = group.flight * (limit / speed);
    }
    Vec requested = steer(group.flight, command, dt, limit);
    Vec glide = command.direction, tangent = group.flight;
    for (int axis = 0; axis < 3; ++axis) {
        if (blocked[axis]) {
            glide[axis] = 0;
            tangent[axis] = 0;
        }
    }
    // Тяга, направленная в стену, не должна каждый тик разворачивать касательную скорость обратно в стену.
    const double fraction = length(glide);
    if (command.thrust && glide != command.direction && fraction > 1e-9) {
        const Vec sliding = steer(tangent, {glide * (1 / fraction), true}, dt, limit * fraction);
        for (int axis = 0; axis < 3; ++axis) {
            if (!blocked[axis]) {
                requested[axis] = sliding[axis];
            }
        }
    }
    Vec travel = requested * dt;
    group.flight = requested;
    for (int axis = 0; axis < 3; ++axis) {
        const double allowed = std::clamp(travel[axis], lower[axis], upper[axis]);
        if (allowed != travel[axis]) {
            travel[axis] = allowed;
            group.flight[axis] = allowed / dt;
        }
    }
    for (auto& cell : group.cells) {
        cell.position = cell.position + travel;
    }
    return requested;
}

void confine(Cell& cell, Vec requested, Command command, double margin, double dt) {
    const double size = radius(cell.mass) + margin;
    const auto bounded = inside(cell.position, size);
    const auto velocity = requested + cell.impulse;
    for (int axis = 0; axis < 3; ++axis) {
        const double sign = std::copysign(1.0, cell.position[axis]);
        const double gap = ARENA_HALF - size - std::abs(cell.position[axis]);
        if (gap > recoilRange * size) {
            continue;
        }
        const bool touching = gap <= 1e-8;
        const double closing = sign * velocity[axis];
        const double drive = command.thrust ? sign * command.direction[axis] : 0;
        const int key = wallKey(axis, sign);
        // Малый отскок центра укладывается в толщину уже сжатой оболочки.
        const bool held = heldContact(cell, key);
        const bool supported = held && drive > 0;
        if ((!touching && !supported) || (closing <= 0 && drive <= 0)) {
            continue;
        }
        Vec normal{};
        normal[axis] = sign;
        cell.skin.contact(key, normal, closing, velocity - normal * closing, drive);
        if (sign * cell.impulse[axis] > 0) {
            cell.impulse[axis] = 0;
        }
        if (touching && !held && closing > BASE_SPEED * 0.2) {
            cell.impulse[axis] = -sign * std::min(0.12, 0.022 * closing);
        } else if (supported && sign * cell.impulse[axis] < 0) {
            cell.impulse[axis] = sign * std::min(0.0, sign * cell.impulse[axis] + dt);
        }
    }
    cell.position = bounded;
}

Vec crawlDirection(const Cell& a, const Cell& b, Vec normal, Vec tangent) {
    if (squared(tangent) > .04) {
        return unit(tangent);
    }
    const auto old = std::ranges::find(a.skin.patches, b.id, &ContactPatch::key);
    if (old != a.skin.patches.end() && old->contactScale > 0) {
        tangent = old->slip - normal * dot(old->slip, normal);
        if (squared(tangent) > .01) {
            return unit(tangent);
        }
    }
    // У строго лобового контакта нет касательной. Выбираем одну на пару и сохраняем её в пятне контакта.
    const Vec ref = std::abs(normal[1]) < .9 ? Vec{{0, 1, 0}} : Vec{{1, 0, 0}};
    return unit(ref - normal * dot(ref, normal)) * (a.id < b.id ? 1 : -1);
}

void cellContact(Cell& a, Cell& b, Vec relative, double dt, bool rivals) {
    const Vec offset = b.position - a.position;
    const double ra = radius(a.mass), rb = radius(b.mass), reach = ra + rb;
    const double distance = length(offset);
    if (distance > reach) {
        return;
    }
    const Vec n = distance > 1e-9 ? unit(offset) : unit(relative);
    const double closing = dot(relative, n);
    const Vec tangent = relative - n * closing;
    const double share = double(b.mass) / (a.mass + b.mass);
    const double depth = reach - distance;
    const double pressure = std::clamp(4 * depth / reach + std::max(0.0, closing) / BASE_SPEED, 0.0, 1.0);
    const Vec surfaceTravel = rivals ? crawlDirection(a, b, n, tangent) * BASE_SPEED : tangent;
    auto& faceA = a.skin.contact(b.id, n, 2 * share * closing, surfaceTravel, 2 * share * pressure);
    auto& faceB =
        b.skin.contact(a.id, n * -1, 2 * (1 - share) * closing, surfaceTravel * -1, 2 * (1 - share) * pressure);
    if (!rivals) {
        const Vec push = n * (depth * -std::expm1(-18 * dt));
        a.position = a.position - push * share;
        b.position = b.position + push * (1 - share);
        return;
    }

    const double small = std::min(ra, rb), layer = .52 * small;
    const double squeeze = std::min(1.0, depth / layer);
    faceA.wrapTarget = faceB.wrapTarget = smooth(squeeze);
    faceA.contactScale = small / ra;
    faceB.contactScale = small / rb;

    const bool edible = canEat(a.mass, b.mass) || canEat(b.mass, a.mass);
    faceA.seam = faceB.seam = (edible ? distance : std::max(distance, reach - layer)) / reach;
    if (edible) {
        return; // Оболочка обтекает добычу вплоть до входа её центра в охотника.
    }
    // После намокания края немного держатся на боку соседа и отпускают его по мере схода плёнки.
    const double wetDepth = .24 * small * std::min(faceA.wrap, faceB.wrap);
    const double damping = -std::expm1(-(1.8 + 12.0 * squeeze) * dt);
    double resistance = closing * damping + (depth - wetDepth) * (20 + 20 * squeeze) * dt;
    if (depth >= layer) {
        // Плотные ядра не проходят друг сквозь друга. У стены остаток поправки получает свободная клетка.
        const Vec push = n * (depth - layer), oldA = a.position, oldB = b.position;
        a.position = inside(oldA - push * share, ra);
        b.position = inside(oldB + push + (a.position - oldA), rb);
        a.position = inside(oldA + (b.position - oldB) - push, ra);
        resistance = std::max(resistance, closing);
    }
    const double diverted = .68 * std::max(0.0, std::min(resistance, closing)) * squeeze;
    const Vec slip = faceA.slip * diverted - tangent * -std::expm1(-3.0 * squeeze * dt);
    const Vec change = slip - n * resistance;
    a.impulse = a.impulse + change * share;
    b.impulse = b.impulse - change * (1 - share);
}

void drawTogether(Group& group, double flightFraction, double dt) {
    const double pull = 0.72 * cohesion(flightFraction) * dt;
    if (group.cells.size() < 2 || pull == 0) {
        return;
    }
    std::array<Vec, CELL_LIMIT> shifts{};
    const int total = group.mass();
    for (std::size_t i = 0; i < group.cells.size(); ++i) {
        const auto& a = group.cells[i];
        for (std::size_t j = i + 1; j < group.cells.size(); ++j) {
            const auto& b = group.cells[j];
            const double gain = -std::expm1(-pull * BASE_MERGE_DURATION / mergeDuration(a.mass + b.mass));
            const Vec delta = (b.position - a.position) * (gain / total);
            shifts[i] = shifts[i] + delta * b.mass;
            shifts[j] = shifts[j] - delta * a.mass;
        }
    }
    for (std::size_t i = 0; i < group.cells.size(); ++i) {
        group.cells[i].position = group.cells[i].position + shifts[i];
    }
}

void contacts(Group& group, double flightFraction, double dt) {
    const bool slow = flightFraction < 0.3;
    for (std::size_t i = 0; i < group.cells.size(); ++i) {
        auto& a = group.cells[i];
        for (std::size_t j = i + 1; j < group.cells.size(); ++j) {
            auto& b = group.cells[j];
            const Vec offset = b.position - a.position;
            const double reach = radius(a.mass) + radius(b.mass);
            if (squared(offset) > reach * reach) {
                continue;
            }
            if (std::ranges::any_of(group.fusions,
                                    [&](const Fusion& pair) { return involved(pair, a.id) && involved(pair, b.id); })) {
                continue;
            }
            if (slow && a.cooldown <= 0 && b.cooldown <= 0 && !fusing(group, a.id) && !fusing(group, b.id)) {
                group.fusions.push_back({a.id, b.id, offset, mergeDuration(a.mass + b.mass)});
                continue;
            }
            cellContact(a, b, a.impulse - b.impulse, dt, false);
        }
    }
}

/// @brief Сводит центры вокруг текущего барицентра, сохраняя движение пары при манёвре.
void advanceFusions(Group& group, double dt) {
    for (auto it = group.fusions.begin(); it != group.fusions.end();) {
        auto a = std::ranges::find(group.cells, it->first, &Cell::id);
        auto b = std::ranges::find(group.cells, it->second, &Cell::id);
        const int total = a->mass + b->mass;
        const double share = static_cast<double>(b->mass) / total;
        const Vec center = a->position + (b->position - a->position) * share;
        it->elapsed += dt;
        if (it->elapsed + 1e-10 >= it->duration) {
            a->position = center;
            a->stepOrigin = a->stepOrigin + (b->stepOrigin - a->stepOrigin) * share;
            a->impulse = a->impulse + (b->impulse - a->impulse) * share;
            a->skin = blend(a->skin, b->skin, share);
            a->mass = total;
            a->pendingLoss += b->pendingLoss;
            group.cells.erase(b);
            it = group.fusions.erase(it);
        } else {
            const Vec offset = it->initialOffset * (1.0 - smooth(it->progress()));
            const Vec firstOffset = offset * (-share), secondOffset = offset * (1.0 - share);
            // У стены заранее освобождаем место для выросшей оболочки общим сдвигом пары.
            const double margin = fusionMargin(group, *a);
            const double edgeA = ARENA_HALF - radius(a->mass) - margin, edgeB = ARENA_HALF - radius(b->mass) - margin;
            auto fitted = center;
            for (int axis = 0; axis < 3; ++axis) {
                fitted[axis] =
                    std::clamp(center[axis], std::max(-edgeA - firstOffset[axis], -edgeB - secondOffset[axis]),
                               std::min(edgeA - firstOffset[axis], edgeB - secondOffset[axis]));
            }
            a->position = fitted + firstOffset;
            b->position = fitted + secondOffset;
            ++it;
        }
    }
}

} // namespace

Vec Match::scatter(double extent) {
    std::uniform_real_distribution<double> draw{-extent, extent};
    return {{draw(m_random), draw(m_random), draw(m_random)}};
}

int Match::foodMass() {
    const int type = std::uniform_int_distribution<int>{0, 9}(m_random);
    return type < 7 ? 5 : (type < 9 ? 2 : 1);
}

Match::Match(Session options, unsigned seed)
    : session(std::move(options)), groups(std::clamp(session.bots, 0, 8) + 1), m_random{seed},
      m_commands(groups.size()) {
    remaining = 60.0 * session.minutes;
    const std::array<Vec, 9> starts{{{},
                                     {{-28, 4, 34}},
                                     {{35, -15, 36}},
                                     {{25, 11, -15}},
                                     {{-27, -12, -16}},
                                     {{8, 29, 48}},
                                     {{-29, 21, 9}},
                                     {{21, -26, 6}},
                                     {{-9, -30, 40}}}};
    for (std::size_t i = 0; i < groups.size(); ++i) {
        groups[i].cells.reserve(CELL_LIMIT);
        groups[i].cells.push_back({m_nextId++, START_MASS, spawn + starts[i]});
    }

    // Короткие цепочки дают видимый путь к следующей порции еды даже на краю арены.
    auto trail = [&](Vec start, Vec stride, int count) {
        for (int j = 0; j < count; ++j) {
            auto point = inside(start + stride * j + scatter(0.65), 1.0);
            if (point[2] >= spawn[2] - 1 && std::hypot(point[0], point[1]) < 1.8) {
                point[0] += std::copysign(2.4, point[0]);
            }
            food.push_back({point, foodMass()});
        }
    };
    trail(spawn + Vec{{2.4, -0.4, 3.5}}, {{0.46, 0.18, 2.35}}, 8);
    trail(spawn + Vec{{-3.5, 1.2, 7}}, {{-0.9, 0.48, 1.75}}, 9);
    for (const auto& group : groups) {
        for (int j = 0; j < 5; ++j) {
            const Vec stride = unit(scatter(1)) * 2.35;
            trail(group.center() + scatter(12), stride, 6);
        }
    }
    for (int j = 0; j < 145; ++j) {
        Vec start = j < 50 ? spawn + scatter(45) : scatter(126);
        trail(start, unit(scatter(1)) * 2.55, 5);
    }
    spikes = {{spawn + Vec{{-6, -1.4, 13}}}, {spawn + Vec{{8, 3.5, 25}}}, {spawn + Vec{{2, -7, -12}}}};
    for (int i = 0; i < 32; ++i) {
        auto point = i < 12 ? spawn + scatter(42) : scatter(125);
        if (point[2] > spawn[2] && std::hypot(point[0], point[1]) < 6) {
            point[0] += std::copysign(8.0, point[0]);
        }
        spikes.push_back({point});
    }
    for (int j = 0; j < 6; ++j) {
        const double angle = 0.8 + 2.3 * j;
        const Vec side{{std::cos(angle), std::sin(angle), 0}};
        const Vec start = spawn + side * (6.5 + j % 3) + Vec{{0, 0, 34.0 + 22 * j}};
        trail(start, {{-0.5 * side[1], 0.5 * side[0], 2.7}}, 5);
    }
}

void Match::split(Group& group, Vec direction) {
    for (const auto index : splitCandidates(group)) {
        auto& rear = group.cells[index];
        cancelFusion(group, rear.id);
        Cell front = rear;
        front.id = m_nextId++;
        rear.mass /= 2;
        front.mass -= rear.mass;
        const Vec offset = direction * (radius(front.mass) + radius(rear.mass));
        const double frontShare = static_cast<double>(front.mass) / (front.mass + rear.mass);
        front.pendingLoss *= frontShare;
        rear.pendingLoss -= front.pendingLoss;
        rear.position = rear.position - offset * frontShare;
        front.position = front.position + offset * (1.0 - frontShare);
        front.impulse = front.impulse + direction * 13.0;
        arm(rear);
        arm(front);
        group.cells.push_back(front);
    }
}

void Match::advance(Group& group, Command command, double dt) {
    if (group.cells.empty()) {
        group.flight = {};
        return;
    }
    command.direction = unit(command.direction);
    if (command.split) {
        split(group, command.direction);
    }
    for (auto& cell : group.cells) {
        cell.stepOrigin = cell.position;
    }
    const auto wanted = fly(group, command, dt);
    constexpr double drag = 2.6;
    const double decay = std::exp(-drag * dt);
    for (auto& cell : group.cells) {
        cell.cooldown = std::max(0.0, cell.cooldown - dt);
        cell.position = cell.position + cell.impulse * ((1.0 - decay) / drag);
        cell.impulse = squared(cell.impulse) * decay * decay < 1e-8 ? Vec{} : cell.impulse * decay;
    }
    const double flightFraction = group.speedFraction();
    drawTogether(group, flightFraction, dt);
    contacts(group, flightFraction, dt);
    advanceFusions(group, dt);
    for (auto& cell : group.cells) {
        confine(cell, wanted, command, fusionMargin(group, cell), dt);
    }
}

void Match::burst(Group& group, std::size_t index, const Spike& spike, double hitTime, double dt) {
    const Cell original = group.cells[index];
    cancelFusion(group, original.id);
    const int total = original.mass + Spike::mass;
    const int count = std::min(4, CELL_LIMIT + 1 - int(group.cells.size()));
    const Vec step = original.position - original.stepOrigin, velocity = step * (1 / dt);
    const Vec hit = original.stepOrigin + step * hitTime, toCore = spike.position - hit;
    const Vec normal = unit(squared(toCore) > 1e-12 ? toCore : velocity);
    const double closing = dot(velocity, normal);
    Vec tangent = velocity - normal * closing;
    if (squared(tangent) < 1e-8) {
        tangent = cross(normal, std::abs(normal[1]) < .85 ? Vec{{0, 1, 0}} : Vec{{1, 0, 0}});
    }
    tangent = unit(tangent);
    const Vec across = cross(normal, tangent);
    const double kick = 9 + 5 * std::clamp(closing / BASE_SPEED, 0.0, 1.0);
    const double share = double(original.mass) / total;
    const Vec center = spike.position + (hit - spike.position) * share;
    // Стягивание группы снова сработает на следующем шаге. В переносимый импульс входит свободная скорость клетки.
    const Vec drift = (group.flight + original.impulse) * share;

    std::array<Vec, 4> directions{};
    directions[0] = normal * -1;
    const double axial = 1.0 / (count - 1), lateral = std::sqrt(1 - axial * axial);
    Vec mean{};
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            const double angle = 2 * std::numbers::pi * (i - 1) / (count - 1);
            directions[i] = normal * axial + (tangent * std::cos(angle) + across * std::sin(angle)) * lateral;
        }
        const int mass = total / count + (i < total % count);
        mean = mean + directions[i] * (static_cast<double>(mass) / total);
    }
    for (int i = 0; i < count; ++i) {
        Cell part = original;
        part.mass = total / count + (i < total % count);
        part.pendingLoss = original.pendingLoss * (double(part.mass) / total);
        const Vec outward = directions[i] - mean;
        Vec launch = drift + outward * kick;
        part.position = center + outward * (.88 * radius(total)) + launch * ((1 - hitTime) * dt);
        const double edge = ARENA_HALF - radius(part.mass);
        for (int k = 0; k < 3; ++k) {
            if (std::abs(part.position[k]) >= edge && launch[k] * part.position[k] > 0) {
                launch[k] = 0;
            }
        }
        arm(part);
        part.id = m_nextId++;
        part.impulse = launch - group.flight;
        part.stepOrigin = part.position - launch * dt;
        if (i == 0) {
            group.cells[index] = part;
        } else {
            group.cells.push_back(part);
        }
    }
}

void Match::touchSpikes(double dt) {
    struct Hit {
        double time;
        int id;
        Group* group;
        std::size_t cell, spike;
    };
    std::vector<Hit> hits;
    for (std::size_t s = 0; s < spikes.size(); ++s) {
        auto& spike = spikes[s];
        if (!spike.active) {
            continue;
        }
        std::vector<int> passing;
        for (auto& group : groups) {
            for (std::size_t i = 0; i < group.cells.size(); ++i) {
                auto& cell = group.cells[i];
                const auto touch = spike.brush(cell, static_cast<int>(s), dt);
                if (!touch.needles && !touch.coreTime) {
                    continue;
                }
                if (cell.mass < Spike::mass || std::ranges::find(spike.passing, cell.id) != spike.passing.end()) {
                    passing.push_back(cell.id);
                } else if (touch.coreTime && group.cells.size() < CELL_LIMIT) {
                    hits.push_back({*touch.coreTime, cell.id, &group, i, s});
                }
            }
        }
        spike.passing = std::move(passing);
        spike.relax(dt);
    }
    // Если за один тик пересечены два ядра, порядок задаёт траектория.
    std::ranges::sort(hits, {}, [](const Hit& h) { return std::tuple{h.time, h.id, h.spike}; });
    for (const auto& hit : hits) {
        auto& spike = spikes[hit.spike];
        if (spike.active && hit.group->cells[hit.cell].id == hit.id && hit.group->cells.size() < CELL_LIMIT) {
            burst(*hit.group, hit.cell, spike, hit.time, dt);
            spike.active = false;
        }
    }
}

void Match::collectFood(Group& group) {
    for (auto& cell : group.cells) {
        double size = radius(cell.mass);
        for (auto& pellet : food) {
            const double reach = size + pellet.size();
            if (squared(cell.position - pellet.position) <= reach * reach) {
                cell.mass += pellet.mass;
                size = radius(cell.mass);
                const Vec offset = unit(scatter(1)) * 9.0;
                pellet = {inside(pellet.position + offset + scatter(4), 1.0), foodMass()};
            }
        }
        cell.position = inside(cell.position, size);
    }
}

void Match::hunt() {
    for (auto& hunters : groups) {
        for (auto& hunter : hunters.cells) {
            for (auto& prey : groups) {
                if (&prey == &hunters) {
                    continue;
                }
                std::erase_if(prey.cells, [&](const Cell& target) {
                    if (!canEat(hunter.mass, target.mass) ||
                        squared(hunter.position - target.position) > std::pow(radius(hunter.mass), 2)) {
                        return false;
                    }
                    cancelFusion(prey, target.id);
                    hunter.mass += target.mass;
                    hunter.pendingLoss += target.pendingLoss;
                    return true;
                });
            }
            hunter.position = inside(hunter.position, radius(hunter.mass));
        }
    }
}

Command Match::botCommand(int owner) const {
    const auto& group = groups[owner];
    if (group.cells.empty()) {
        return {};
    }
    Command escape{}, chase{}, forage{};
    double nearestThreat = 15.0 * 15.0;
    double nearestPrey = 21.0 * 21.0;
    double bestFood{};
    const auto candidates = splitCandidates(group);
    for (std::size_t i = 0; i < group.cells.size(); ++i) {
        const auto& cell = group.cells[i];
        for (const auto& rival : groups) {
            if (&rival == &group) {
                continue;
            }
            for (const auto& target : rival.cells) {
                const Vec offset = target.position - cell.position;
                const double distance2 = squared(offset);
                if (target.mass > cell.mass && distance2 < nearestThreat) {
                    nearestThreat = distance2;
                    escape = {unit(offset * -1.0), true};
                }
                if (canEat(cell.mass, target.mass) && distance2 < nearestPrey) {
                    nearestPrey = distance2;
                    const double distance = std::sqrt(distance2);
                    const auto lead = (rival.flight + target.impulse) * std::min(0.7, distance / 12.0);
                    const bool attack = std::ranges::find(candidates, i) != candidates.end() &&
                                        canEat((cell.mass + 1) / 2, target.mass) && distance > radius(cell.mass) &&
                                        distance < 5.5 + radius(cell.mass);
                    chase = {unit(offset + lead), true, attack};
                }
            }
        }
    }
    if (escape.thrust) {
        return escape;
    }
    if (chase.thrust) {
        return chase;
    }
    if (group.cells.size() > 1 && std::ranges::all_of(group.cells, [](const Cell& c) { return c.cooldown <= 0; })) {
        return {};
    }
    for (const auto& pellet : food) {
        for (const auto& cell : group.cells) {
            const Vec offset = pellet.position - cell.position;
            const double score = pellet.mass / (2.0 + squared(offset));
            if (score > bestFood) {
                bestFood = score;
                forage = {unit(offset), true};
            }
        }
    }
    return forage;
}

bool Match::resultVisible() const {
    return remaining == 0 || groups.front().cells.empty();
}

std::vector<int> Match::ranking() const {
    std::vector<int> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, std::greater<>{}, [&](int owner) { return groups[owner].mass(); });
    return order;
}

void Match::step(Command player, double dt) {
    if (remaining == 0 || dt <= 0.0) {
        return;
    }
    dt = std::min(dt, remaining);
    const double lossFraction = -std::expm1(std::log(massRetainedPerMinute) * (dt / 60.0));
    for (auto& group : groups) {
        loseMass(group, lossFraction);
    }
    m_brainDelay -= dt;
    if (m_brainDelay <= 0) {
        for (int i = 1; i < static_cast<int>(groups.size()); ++i) {
            m_commands[i] = botCommand(i);
        }
        m_brainDelay = 0.18;
    }
    m_commands[0] = player;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        advance(groups[i], m_commands[i], dt);
        m_commands[i].split = false;
    }
    touchSpikes(dt);
    for (auto& group : groups) {
        collectFood(group);
    }
    hunt();
    for (std::size_t i = 0; i < groups.size(); ++i) {
        auto& group = groups[i];
        for (auto& a : group.cells) {
            for (std::size_t j = i + 1; j < groups.size(); ++j) {
                for (auto& b : groups[j].cells) {
                    cellContact(a, b, group.flight + a.impulse - groups[j].flight - b.impulse, dt, true);
                }
            }
            a.position = inside(a.position, radius(a.mass));
            a.skin.move((a.position - a.stepOrigin) * (1 / dt), dt);
            a.skin.advance(dt);
        }
    }
    remaining -= dt;
    if (remaining < 1e-9) {
        remaining = 0;
    }
}

} // namespace agar
