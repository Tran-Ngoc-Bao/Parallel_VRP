#pragma once
#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <nlohmann/json.hpp>
#include "routes.hpp"

// -----------------------------------------------------------------------
// Penalty coefficients – global atomics
// -----------------------------------------------------------------------
namespace penalty {
    extern std::atomic<double> coeff[4];
    inline double get(int i) { return coeff[i].load(std::memory_order_relaxed); }
    inline void   set(int i, double v) { coeff[i].store(v, std::memory_order_relaxed); }
    void update(int i, double violation);
}

// Forward declare Logger (defined in logger.hpp)
struct Logger;

// -----------------------------------------------------------------------
// Solution
// -----------------------------------------------------------------------
struct Solution {
    std::vector<std::vector<std::shared_ptr<TruckRoute>>> truck_routes;
    std::vector<std::vector<std::shared_ptr<DroneRoute>>> drone_routes;

    std::vector<double> truck_working_time;
    std::vector<double> drone_working_time;

    double working_time           = 0;
    double energy_violation       = 0;
    double capacity_violation     = 0;
    double waiting_time_violation = 0;
    double fixed_time_violation   = 0;
    bool   feasible               = false;

    // Factory – computes all derived fields
    static Solution make(
        std::vector<std::vector<std::shared_ptr<TruckRoute>>> truck_routes,
        std::vector<std::vector<std::shared_ptr<DroneRoute>>> drone_routes);

    double cost() const;

    size_t hamming_distance(const Solution& other) const;

    void verify() const;

    // Build initial solution from problem data
    static Solution initialize();

    // Destroy-and-repair
    Solution destroy_and_repair(const std::vector<std::vector<double>>& edge_records) const;

    // Main tabu search loop
    static Solution tabu_search(Solution root, Logger& logger);

    // JSON serialization
    nlohmann::json to_json() const;
    static Solution from_json(const nlohmann::json& j);
};
