#pragma once
#include <string>
#include <vector>

enum class Neighborhood {
    Move10,
    Move11,
    Move20,
    Move21,
    Move22,
    TwoOpt,
    EjectionChain
};

inline std::string neighborhood_to_str(Neighborhood n) {
    switch(n) {
        case Neighborhood::Move10:        return "Move (1, 0)";
        case Neighborhood::Move11:        return "Move (1, 1)";
        case Neighborhood::Move20:        return "Move (2, 0)";
        case Neighborhood::Move21:        return "Move (2, 1)";
        case Neighborhood::Move22:        return "Move (2, 2)";
        case Neighborhood::TwoOpt:        return "2-opt";
        case Neighborhood::EjectionChain: return "Ejection-chain";
    }
    return "";
}

// Forward declare Solution to break circular dependency
struct Solution;

namespace neighborhoods {
    // Returns {best_solution, tabu_entry}
    std::pair<Solution, std::vector<size_t>> inter_route(
        Neighborhood n,
        const Solution& solution,
        const std::vector<std::vector<size_t>>& tabu_list,
        double aspiration_cost);

    std::pair<Solution, std::vector<size_t>> intra_route(
        Neighborhood n,
        const Solution& solution,
        const std::vector<std::vector<size_t>>& tabu_list,
        double aspiration_cost);

    // Returns nullopt if no improvement found (both neighborhoods empty)
    // Otherwise returns the best neighbor and updates tabu_list
    bool search(
        Neighborhood n,
        const Solution& solution,
        std::vector<std::vector<size_t>>& tabu_list,
        size_t tabu_size,
        double aspiration_cost,
        Solution& out_result);
} // namespace neighborhoods
