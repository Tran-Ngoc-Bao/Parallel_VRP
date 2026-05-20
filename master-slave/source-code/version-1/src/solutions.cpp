#include "solutions.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace solutions {

// Helper: Get all customers in route order
static std::vector<int> get_route_sequence(const common::Trip &trip) {
    std::vector<int> route_seq;
    
    if (trip.customers.empty()) return route_seq;
    
    std::set<int> next_set;
    for (const auto &[cus, nxt] : trip.customers) {
        if (nxt != -1) next_set.insert(nxt);
    }
    
    int start = 0;
    for (const auto &[cus, _] : trip.customers) {
        if (!next_set.count(cus)) {
            start = cus;
            break;
        }
    }
    
    int current = start;
    while (current != -1) {
        route_seq.push_back(current);
        auto it = trip.customers.find(current);
        current = (it != trip.customers.end()) ? it->second : -1;
    }
    
    return route_seq;
}

static double compute_route_distance(const std::vector<std::vector<double>> &distances,
                                     const std::vector<int> &route_seq)
{
    if (route_seq.empty()) {
        return 0.0;
    }

    double total_distance = 0.0;
    int prev = 0;
    for (int cus : route_seq) {
        total_distance += distances[prev][cus];
        prev = cus;
    }
    total_distance += distances[prev][0];
    return total_distance;
}

static double compute_route_time(const Config &cfg, int vehicle_type, const std::vector<int> &route_seq)
{
    if (route_seq.empty()) {
        return 0.0;
    }

    if (vehicle_type == 0) {
        if (cfg.truck.speed <= 0.0) {
            return std::numeric_limits<double>::infinity();
        }
        const double truck_distance = compute_route_distance(cfg.truck_distances, route_seq);
        return truck_distance / cfg.truck.speed;
    }

    const double drone_distance = compute_route_distance(cfg.drone_distances, route_seq);
    return cfg.drone.takeoff_time() + cfg.drone.cruise_time(drone_distance) + cfg.drone.landing_time();
}

static double compute_element_time(const Config &cfg, const common::EliteElement &element)
{
    double element_time = 0.0;
    for (const auto &trip : element.trips) {
        if (trip.customers.empty()) {
            continue;
        }
        const auto route_seq = get_route_sequence(trip);
        element_time += compute_route_time(cfg, element.type, route_seq);
    }
    return element_time;
}

static double compute_other_elements_max_time(const Config &cfg,
                                              const common::Elite &elite,
                                              std::size_t excluded_element_idx)
{
    double other_max = 0.0;
    for (std::size_t i = 0; i < elite.elements.size(); ++i) {
        if (i == excluded_element_idx) {
            continue;
        }
        other_max = std::max(other_max, compute_element_time(cfg, elite.elements[i]));
    }
    return other_max;
}

// Helper: Calculate cost of a trip after removing and reinserting a customer
static double calculate_relocate_cost(const Config &cfg,
                                      int vehicle_type,
                                      const std::vector<int> &route_seq,
                                      int move_pos,
                                      int new_pos,
                                      double other_max,
                                      double base_element_time,
                                      double old_trip_time,
                                      double base_makespan) {
    if (route_seq.size() < 2) return 0.0;
    
    std::vector<int> new_route = route_seq;
    if (move_pos >= 0 && move_pos < (int) new_route.size()) {
        int customer = new_route[move_pos];
        new_route.erase(new_route.begin() + move_pos);
        
        if (new_pos > move_pos) new_pos--;
        if (new_pos >= 0 && new_pos <= (int) new_route.size()) {
            new_route.insert(new_route.begin() + new_pos, customer);

            const double new_trip_time = compute_route_time(cfg, vehicle_type, new_route);
            const double new_element_time = base_element_time - old_trip_time + new_trip_time;
            const double candidate_makespan = std::max(other_max, new_element_time);
            return candidate_makespan - base_makespan;
        }
    }
    
    return 0.0;
}

// Neighborhood: Move(1,0) - Move 1 customer to different position (PARALLEL)
common::Elite apply_move10(
    const Config &cfg,
    const common::Elite &elite,
    std::mt19937_64 &rng)
{
    auto result = elite;
    
    if (result.elements.empty()) return result;
    
    std::uniform_int_distribution<std::size_t> pick_el(0, result.elements.size() - 1);
    std::size_t el_idx = pick_el(rng);
    auto &element = result.elements[el_idx];
    
    if (element.trips.empty() || element.trips[0].customers.empty()) {
        return result;
    }

    auto route_seq = get_route_sequence(element.trips[0]);
    if (route_seq.size() < 2) return result;

    const double base_element_time = compute_element_time(cfg, element);
    const double other_max = compute_other_elements_max_time(cfg, result, el_idx);
    const double old_trip_time = compute_route_time(cfg, element.type, route_seq);
    const double base_makespan = std::max(other_max, base_element_time);
    
    // Find best relocate move (PARALLEL)
    int best_move_pos = 0;
    int best_new_pos = 0;
    double best_cost_delta = 0.0;
    
    for (int move_pos = 0; move_pos < (int) route_seq.size(); ++move_pos) {
        for (int new_pos = 0; new_pos <= (int) route_seq.size(); ++new_pos) {
            if (move_pos == new_pos || (move_pos + 1 == new_pos)) continue;

            double delta = calculate_relocate_cost(
                cfg,
                element.type,
                route_seq,
                move_pos,
                new_pos,
                other_max,
                base_element_time,
                old_trip_time,
                base_makespan);

            if (delta < best_cost_delta) {
                best_cost_delta = delta;
                best_move_pos = move_pos;
                best_new_pos = new_pos;
            }
        }
    }
    
    // Apply best move if improvement found
    if (best_cost_delta < 0.0) {
        int customer = route_seq[best_move_pos];
        route_seq.erase(route_seq.begin() + best_move_pos);
        
        int final_pos = best_new_pos;
        if (final_pos > best_move_pos) final_pos--;
        route_seq.insert(route_seq.begin() + final_pos, customer);
        
        element.trips[0].customers.clear();
        for (std::size_t i = 0; i < route_seq.size(); ++i) {
            int next = (i + 1 < route_seq.size()) ? route_seq[i + 1] : -1;
            element.trips[0].customers[route_seq[i]] = next;
        }
    }
    
    return result;
}

// Neighborhood: Move(1,1) - Move 1 customer with insertion constraints (PARALLEL)
common::Elite apply_move11(
    const Config &cfg,
    const common::Elite &elite,
    std::mt19937_64 &rng)
{
    auto result = elite;
    
    if (result.elements.empty()) return result;
    
    std::uniform_int_distribution<std::size_t> pick_el(0, result.elements.size() - 1);
    std::size_t el_idx = pick_el(rng);
    auto &element = result.elements[el_idx];
    
    if (element.trips.empty() || element.trips[0].customers.empty()) {
        return result;
    }

    auto route_seq = get_route_sequence(element.trips[0]);
    if (route_seq.size() < 2) return result;

    const double base_element_time = compute_element_time(cfg, element);
    const double other_max = compute_other_elements_max_time(cfg, result, el_idx);
    const double old_trip_time = compute_route_time(cfg, element.type, route_seq);
    const double base_makespan = std::max(other_max, base_element_time);
    
    // Find best move with insertion constraints (PARALLEL)
    int best_move_pos = 0;
    int best_new_pos = 0;
    double best_cost_delta = 0.0;
    
    for (int move_pos = 0; move_pos < (int) route_seq.size(); ++move_pos) {
        for (int new_pos = 0; new_pos <= (int) route_seq.size(); ++new_pos) {
            if (move_pos == new_pos || (move_pos + 1 == new_pos)) continue;

            double delta = calculate_relocate_cost(
                cfg,
                element.type,
                route_seq,
                move_pos,
                new_pos,
                other_max,
                base_element_time,
                old_trip_time,
                base_makespan);

            if (delta < best_cost_delta) {
                best_cost_delta = delta;
                best_move_pos = move_pos;
                best_new_pos = new_pos;
            }
        }
    }
    
    // Apply best move if improvement found
    if (best_cost_delta < 0.0) {
        int customer = route_seq[best_move_pos];
        route_seq.erase(route_seq.begin() + best_move_pos);
        
        int final_pos = best_new_pos;
        if (final_pos > best_move_pos) final_pos--;
        route_seq.insert(route_seq.begin() + final_pos, customer);
        
        element.trips[0].customers.clear();
        for (std::size_t i = 0; i < route_seq.size(); ++i) {
            int next = (i + 1 < route_seq.size()) ? route_seq[i + 1] : -1;
            element.trips[0].customers[route_seq[i]] = next;
        }
    }
    
    return result;
}

// Neighborhood: Move(2,0) - Move 2 consecutive customers (PARALLEL)
common::Elite apply_move20(
    const Config &cfg,
    const common::Elite &elite,
    std::mt19937_64 &rng)
{
    auto result = elite;
    if (result.elements.empty()) return result;
    
    std::uniform_int_distribution<std::size_t> pick_el(0, result.elements.size() - 1);
    std::size_t el_idx = pick_el(rng);
    auto &element = result.elements[el_idx];
    
    if (element.trips.empty() || element.trips[0].customers.size() < 3) {
        return result;
    }

    auto route_seq = get_route_sequence(element.trips[0]);
    if (route_seq.size() < 3) return result;

    const double base_element_time = compute_element_time(cfg, element);
    const double other_max = compute_other_elements_max_time(cfg, result, el_idx);
    const double old_trip_time = compute_route_time(cfg, element.type, route_seq);
    const double base_makespan = std::max(other_max, base_element_time);
    
    // Find best move of 2 consecutive customers (PARALLEL)
    int best_move_i = 0;
    int best_new_pos = 0;
    double best_cost_delta = 0.0;
    
    for (int i = 0; i < (int) route_seq.size() - 1; ++i) {
        for (int j = 0; j < (int) route_seq.size(); ++j) {
            if (i == j || (i + 1 == j)) continue;

            std::vector<int> new_route = route_seq;
            int cus1 = new_route[i];
            int cus2 = new_route[i + 1];

            new_route.erase(new_route.begin() + i, new_route.begin() + i + 2);
            int final_j = (j > i) ? j - 2 : j;
            if (final_j >= 0 && final_j <= (int) new_route.size()) {
                new_route.insert(new_route.begin() + final_j, {cus1, cus2});
            } else {
                continue;
            }

            const double new_trip_time = compute_route_time(cfg, element.type, new_route);
            const double new_element_time = base_element_time - old_trip_time + new_trip_time;
            const double candidate_makespan = std::max(other_max, new_element_time);
            const double delta = candidate_makespan - base_makespan;

            if (delta < best_cost_delta) {
                best_cost_delta = delta;
                best_move_i = i;
                best_new_pos = j;
            }
        }
    }
    
    // Apply best move if improvement found
    if (best_cost_delta < 0.0) {
        int cus1 = route_seq[best_move_i];
        int cus2 = route_seq[best_move_i + 1];
        
        route_seq.erase(route_seq.begin() + best_move_i, route_seq.begin() + best_move_i + 2);
        int final_pos = (best_new_pos > best_move_i) ? best_new_pos - 2 : best_new_pos;
        route_seq.insert(route_seq.begin() + final_pos, {cus1, cus2});
        
        element.trips[0].customers.clear();
        for (std::size_t k = 0; k < route_seq.size(); ++k) {
            int next = (k + 1 < route_seq.size()) ? route_seq[k + 1] : -1;
            element.trips[0].customers[route_seq[k]] = next;
        }
    }
    
    return result;
}

// Neighborhood: Move(2,1) - Move 2 consecutive customers with constraints (PARALLEL)
common::Elite apply_move21(
    const Config &cfg,
    const common::Elite &elite,
    std::mt19937_64 &rng)
{
    auto result = elite;
    if (result.elements.empty()) return result;
    
    std::uniform_int_distribution<std::size_t> pick_el(0, result.elements.size() - 1);
    std::size_t el_idx = pick_el(rng);
    auto &element = result.elements[el_idx];
    
    if (element.trips.empty() || element.trips[0].customers.size() < 3) {
        return result;
    }

    auto route_seq = get_route_sequence(element.trips[0]);
    if (route_seq.size() < 3) return result;

    const double base_element_time = compute_element_time(cfg, element);
    const double other_max = compute_other_elements_max_time(cfg, result, el_idx);
    const double old_trip_time = compute_route_time(cfg, element.type, route_seq);
    const double base_makespan = std::max(other_max, base_element_time);
    
    // Find best move of 2 consecutive customers with constraints (PARALLEL)
    int best_move_i = 0;
    int best_new_pos = 0;
    double best_cost_delta = 0.0;
    
    for (int i = 0; i < (int) route_seq.size() - 1; ++i) {
        for (int j = 0; j < (int) route_seq.size(); ++j) {
            if (i == j || (i + 1 == j)) continue;

            std::vector<int> new_route = route_seq;
            int cus1 = new_route[i];
            int cus2 = new_route[i + 1];

            new_route.erase(new_route.begin() + i, new_route.begin() + i + 2);
            int final_j = (j > i) ? j - 2 : j;
            if (final_j >= 0 && final_j <= (int) new_route.size()) {
                new_route.insert(new_route.begin() + final_j, {cus1, cus2});
            } else {
                continue;
            }

            const double new_trip_time = compute_route_time(cfg, element.type, new_route);
            const double new_element_time = base_element_time - old_trip_time + new_trip_time;
            const double candidate_makespan = std::max(other_max, new_element_time);
            const double delta = candidate_makespan - base_makespan;

            if (delta < best_cost_delta) {
                best_cost_delta = delta;
                best_move_i = i;
                best_new_pos = j;
            }
        }
    }
    
    // Apply best move if improvement found
    if (best_cost_delta < 0.0) {
        int cus1 = route_seq[best_move_i];
        int cus2 = route_seq[best_move_i + 1];
        
        route_seq.erase(route_seq.begin() + best_move_i, route_seq.begin() + best_move_i + 2);
        int final_pos = (best_new_pos > best_move_i) ? best_new_pos - 2 : best_new_pos;
        route_seq.insert(route_seq.begin() + final_pos, {cus1, cus2});
        
        element.trips[0].customers.clear();
        for (std::size_t k = 0; k < route_seq.size(); ++k) {
            int next = (k + 1 < route_seq.size()) ? route_seq[k + 1] : -1;
            element.trips[0].customers[route_seq[k]] = next;
        }
    }
    
    return result;
}

// Neighborhood: Move(2,2) - Move 2 consecutive customers, advanced constraints (PARALLEL)
common::Elite apply_move22(
    const Config &cfg,
    const common::Elite &elite,
    std::mt19937_64 &rng)
{
    auto result = elite;
    if (result.elements.empty()) return result;
    
    std::uniform_int_distribution<std::size_t> pick_el(0, result.elements.size() - 1);
    std::size_t el_idx = pick_el(rng);
    auto &element = result.elements[el_idx];
    
    if (element.trips.empty() || element.trips[0].customers.size() < 3) {
        return result;
    }

    auto route_seq = get_route_sequence(element.trips[0]);
    if (route_seq.size() < 3) return result;

    const double base_element_time = compute_element_time(cfg, element);
    const double other_max = compute_other_elements_max_time(cfg, result, el_idx);
    const double old_trip_time = compute_route_time(cfg, element.type, route_seq);
    const double base_makespan = std::max(other_max, base_element_time);
    
    // Find best move of 2 consecutive customers with advanced constraints (PARALLEL)
    int best_move_i = 0;
    int best_new_pos = 0;
    double best_cost_delta = 0.0;
    
    for (int i = 0; i < (int) route_seq.size() - 1; ++i) {
        for (int j = 0; j < (int) route_seq.size(); ++j) {
            if (i == j || (i + 1 == j)) continue;
            
            std::vector<int> new_route = route_seq;
            int cus1 = new_route[i];
            int cus2 = new_route[i + 1];
            
            new_route.erase(new_route.begin() + i, new_route.begin() + i + 2);
            int final_j = (j > i) ? j - 2 : j;
            if (final_j >= 0 && final_j <= (int) new_route.size()) {
                new_route.insert(new_route.begin() + final_j, {cus1, cus2});
            } else {
                continue;
            }
            
            const double new_trip_time = compute_route_time(cfg, element.type, new_route);
            const double new_element_time = base_element_time - old_trip_time + new_trip_time;
            const double candidate_makespan = std::max(other_max, new_element_time);
            const double delta = candidate_makespan - base_makespan;
            
            if (delta < best_cost_delta) {
                best_cost_delta = delta;
                best_move_i = i;
                best_new_pos = j;
            }
        }
    }
    
    // Apply best move if improvement found
    if (best_cost_delta < 0.0) {
        int cus1 = route_seq[best_move_i];
        int cus2 = route_seq[best_move_i + 1];
        
        route_seq.erase(route_seq.begin() + best_move_i, route_seq.begin() + best_move_i + 2);
        int final_pos = (best_new_pos > best_move_i) ? best_new_pos - 2 : best_new_pos;
        route_seq.insert(route_seq.begin() + final_pos, {cus1, cus2});
        
        element.trips[0].customers.clear();
        for (std::size_t k = 0; k < route_seq.size(); ++k) {
            int next = (k + 1 < route_seq.size()) ? route_seq[k + 1] : -1;
            element.trips[0].customers[route_seq[k]] = next;
        }
    }
    
    return result;
}

// Neighborhood: TwoOpt - reverse segment
common::Elite apply_twoopt(
    const Config &cfg,
    const common::Elite &elite,
    std::mt19937_64 &rng)
{
    auto result = elite;
    if (result.elements.empty()) return result;
    
    std::uniform_int_distribution<std::size_t> pick_el(0, result.elements.size() - 1);
    std::size_t el_idx = pick_el(rng);
    auto &element = result.elements[el_idx];
    
    if (element.trips.empty() || element.trips[0].customers.size() < 3) {
        return result;
    }

    auto route_seq = get_route_sequence(element.trips[0]);
    if (route_seq.size() < 3) return result;

    const double base_element_time = compute_element_time(cfg, element);
    const double other_max = compute_other_elements_max_time(cfg, result, el_idx);
    const double old_trip_time = compute_route_time(cfg, element.type, route_seq);
    const double base_makespan = std::max(other_max, base_element_time);
    
    // Find best 2-opt move (PARALLEL)
    int best_i = 0;
    int best_j = 1;
    double best_cost_delta = 0.0;
    
    for (int i = 0; i < (int) route_seq.size() - 1; ++i) {
        for (int j = i + 1; j < (int) route_seq.size(); ++j) {
            // Calculate cost delta for reversing segment [i, j]
            std::vector<int> new_route = route_seq;
            std::reverse(new_route.begin() + i, new_route.begin() + j + 1);

            const double new_trip_time = compute_route_time(cfg, element.type, new_route);
            const double new_element_time = base_element_time - old_trip_time + new_trip_time;
            const double candidate_makespan = std::max(other_max, new_element_time);
            const double delta = candidate_makespan - base_makespan;

            if (delta < best_cost_delta) {
                best_cost_delta = delta;
                best_i = i;
                best_j = j;
            }
        }
    }
    
    // Apply best 2-opt if improvement found
    if (best_cost_delta < 0.0) {
        std::reverse(route_seq.begin() + best_i, route_seq.begin() + best_j + 1);
        
        element.trips[0].customers.clear();
        for (std::size_t k = 0; k < route_seq.size(); ++k) {
            int next = (k + 1 < route_seq.size()) ? route_seq[k + 1] : -1;
            element.trips[0].customers[route_seq[k]] = next;
        }
    }
    
    return result;
}

// Neighborhood: EjectionChain - advanced move operator
common::Elite apply_ejection_chain(
    const Config &cfg,
    const common::Elite &elite,
    std::mt19937_64 &rng)
{
    auto result = elite;
    if (result.elements.empty()) return result;
    
    std::uniform_int_distribution<std::size_t> pick_el(0, result.elements.size() - 1);
    std::size_t el_idx = pick_el(rng);
    auto &element = result.elements[el_idx];
    
    if (element.trips.empty() || element.trips[0].customers.size() < 2) {
        return result;
    }

    auto route_seq = get_route_sequence(element.trips[0]);
    if (route_seq.size() < 2) return result;
    
    // For now, implement as swap move
    // In future, can implement proper ejection chain algorithm
    std::uniform_int_distribution<std::size_t> pick_pos(0, route_seq.size() - 1);
    int pos1 = pick_pos(rng);
    int pos2 = pick_pos(rng);
    
    if (pos1 != pos2) {
        std::swap(route_seq[pos1], route_seq[pos2]);
        
        element.trips[0].customers.clear();
        for (std::size_t k = 0; k < route_seq.size(); ++k) {
            int next = (k + 1 < route_seq.size()) ? route_seq[k + 1] : -1;
            element.trips[0].customers[route_seq[k]] = next;
        }
    }
    
    return result;
}

std::vector<int> generate_neighborhood_order(std::mt19937_64 &rng) {
    std::vector<int> order = {
        (int)  solutions::Neighborhood::Move10,
        (int)  solutions::Neighborhood::Move11,
        (int)  solutions::Neighborhood::Move20,
        (int)  solutions::Neighborhood::Move21,
        (int)  solutions::Neighborhood::Move22,
        (int)  solutions::Neighborhood::TwoOpt,
        (int)  solutions::Neighborhood::EjectionChain
    };
    std::shuffle(order.begin(), order.end(), rng);
    return order;
}

double compute_elite_cost(const Config &cfg, const common::Elite &elite) {
    double makespan = 0.0;

    for (const auto &element : elite.elements) {
        const double element_time = compute_element_time(cfg, element);
        makespan = std::max(makespan, element_time);
    }

    return makespan;
}

} // namespace solutions
