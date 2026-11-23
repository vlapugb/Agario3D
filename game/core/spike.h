#pragma once

#include <optional>
#include <vector>

#include "deformation.h"

namespace agar {
struct Cell;

struct Spike {
    static constexpr int mass = 100, needleCount = 58;
    static constexpr double core = .71, base = .676, width = .134, maxBend = .205;
    struct Needle {
        Vec axis, tip;
    };
    static const std::array<Needle, needleCount>& crown();

    struct Bend {
        std::array<Spring, 3> spring{};
        Vec load{};
        Vec offset() const { return {{spring[0].value, spring[1].value, spring[2].value}}; }
    };
    struct Touch {
        bool needles{};
        std::optional<double> coreTime{};
    };
    Touch brush(Cell& cell, int spikeIndex, double dt);
    void relax(double dt);

    Vec position{};
    bool active{true}, flexing{};
    std::array<Bend, needleCount> bends{};
    std::vector<int> passing{}; // Клетки, вошедшие в текущий контакт с массой меньше 100.
};
} // namespace agar
