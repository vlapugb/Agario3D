#pragma once

#include <array>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "spike.h"

namespace agar {

constexpr double ARENA_HALF = 136.0;
constexpr int START_MASS = 40;
constexpr double BASE_SPEED = 5.8;
constexpr double MATCH_DURATION = 180.0;
constexpr double FIXED_STEP = 1.0 / 60.0;
constexpr double BASE_MERGE_DURATION = 0.6;
constexpr int CELL_LIMIT = 16;
constexpr unsigned OPENING_SEED = 12481;

double radius(int mass);
double cohesion(double flightFraction);
double smooth(double fraction);
Vec unit(Vec vector);
double squared(Vec vector);

struct Session {
    std::u32string name{U"Игрок"};
    int color{}, minutes{3}, bots{5};
    bool operator==(const Session&) const = default;
};

struct Command {
    Vec direction{{0, 0, 1}};
    bool thrust{};
    bool split{};
};

struct Cell {
    int id{};
    int mass{START_MASS};
    Vec position{};
    Vec impulse{};
    double cooldown{};
    Deformation skin{};
    Vec stepOrigin{};
    double pendingLoss{}; // Отрицательный остаток означает, что часть потери уже округлили вверх.
};

struct Fusion {
    int first{}, second{};
    Vec initialOffset{};
    double duration{BASE_MERGE_DURATION}, elapsed{};
    double progress() const { return elapsed / duration; }
};

struct Group {
    std::vector<Cell> cells;
    std::vector<Fusion> fusions;
    Vec flight{};
    int mass() const;
    double speedLimit() const;
    double speedFraction() const;
    Vec center() const;
    const Cell* find(int id) const;
};

struct Food {
    Vec position{};
    int mass{};
    double size() const;
};

struct Match {
    explicit Match(Session session = {}, unsigned seed = OPENING_SEED);
    explicit Match(unsigned seed) : Match(Session{}, seed) {}
    void step(Command player, double dt);
    bool resultVisible() const;
    std::vector<int> ranking() const;
    Command botCommand(int owner) const;

    Session session;
    std::vector<Group> groups;
    std::vector<Food> food;
    std::vector<Spike> spikes;
    double remaining{MATCH_DURATION};

private:
    Vec scatter(double extent);
    int foodMass();
    void advance(Group& group, Command command, double dt);
    void split(Group& group, Vec direction);
    void collectFood(Group& group);
    void touchSpikes(double dt);
    void burst(Group& group, std::size_t index, const Spike& spike, double hitTime, double dt);
    void hunt();
    std::mt19937 m_random;
    int m_nextId{1};
    double m_brainDelay{};
    std::vector<Command> m_commands;
};

} // namespace agar
