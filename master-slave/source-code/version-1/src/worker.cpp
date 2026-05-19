#include "worker.hpp"
#include "common.hpp"
#include "clusterize.hpp"
#include "config.hpp"
#include "solutions.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <limits>
#include <queue>
#include <set>
#include <random>
#include <string>
#include <stdexcept>
#include <vector>
#include <mpi.h>

const int PULL_ELITE_NO_IMPROVE_INTERVAL = 100;
const int STOP_AFTER_PULLS = 30;

static std::mt19937_64 make_rng(const Config &cfg) {
    if (cfg.seed) {
        return std::mt19937_64(*cfg.seed);
    }

    std::random_device rd;
    std::seed_seq seq{rd(), rd(), rd(), rd()};
    return std::mt19937_64(seq);
}

static double customer_demand(const Config &cfg, std::size_t customer) {
    return cfg.demands[customer];
}

static common::Trip route_to_trip(const std::vector<int> &route) {
    common::Trip trip;
    for (std::size_t i = 0; i < route.size(); ++i) {
        int next = (i + 1 < route.size()) ? route[i + 1] : -1;
        trip.customers[route[i]] = next;
    }
    return trip;
}

static double route_distance(const std::vector<std::vector<double>>& distances, const std::vector<int>& route) {
    if (route.empty()) {
        return 0.0;
    }

    double total_distance = 0.0;
    int prev = 0;
    for (int customer : route) {
        total_distance += distances[prev][customer];
        prev = customer;
    }
    total_distance += distances[prev][0];
    return total_distance;
}

static double truck_route_time(const Config& cfg, const std::vector<int>& route) {
    if (route.empty()) {
        return 0.0;
    }
    if (cfg.truck.speed <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return route_distance(cfg.truck_distances, route) / cfg.truck.speed;
}

static double drone_route_time(const Config& cfg, const std::vector<int>& route) {
    if (route.empty()) {
        return 0.0;
    }
    return cfg.drone.takeoff_time() + cfg.drone.cruise_time(route_distance(cfg.drone_distances, route)) + cfg.drone.landing_time();
}

static double route_demand(const Config& cfg, const std::vector<int>& route) {
    return std::accumulate(
        route.begin(), route.end(), 0.0,
        [&](double acc, int customer) {
            return acc + customer_demand(cfg, static_cast<std::size_t>(customer));
        });
}

static LocalSolutionMetrics build_local_solution(const Config &cfg,
    const std::vector<std::vector<std::vector<int>>> &tr,
    const std::vector<std::vector<std::vector<int>>> &dr)
{
    LocalSolutionMetrics res;
    res.truck_working_time.assign(tr.size(), 0.0);
    res.drone_working_time.assign(dr.size(), 0.0);
    double working_time = 0.0;
    double energy_violation = 0.0;
    double capacity_violation = 0.0;
    double waiting_time_violation = 0.0;
    double fixed_time_violation = 0.0;

    // Trucks
    for (size_t t = 0; t < tr.size(); ++t) {
        double sum = 0.0;
        for (const auto &route : tr[t]) {
            double wt = truck_route_time(cfg, route);
            sum += wt;
            double capv = std::max(0.0, route_demand(cfg, route) - cfg.truck.capacity);
            capacity_violation += capv / cfg.truck.capacity;

            double accum = 0.0;
            int prev = 0;
            for (int customer : route) {
                accum += cfg.truck_distances[prev][customer] / cfg.truck.speed;
                waiting_time_violation += std::max(0.0, wt - accum - cfg.waiting_time_limit);
                prev = customer;
            }
        }
        res.truck_working_time[t] = sum;
        working_time = std::max(working_time, sum);
    }

    // Drones
    for (size_t d_idx = 0; d_idx < dr.size(); ++d_idx) {
        double sum = 0.0;
        for (const auto &route : dr[d_idx]) {
            double wt = drone_route_time(cfg, route);
            sum += wt;

            double payload = 0.0;
            for (int c : route) payload += customer_demand(cfg, static_cast<std::size_t>(c));
            capacity_violation += std::max(0.0, payload - cfg.drone.capacity()) / cfg.drone.capacity();

            double takeoff = cfg.drone.takeoff_time();
            double landing = cfg.drone.landing_time();
            double time = 0.0;
            double energy = 0.0;
            double weight = 0.0;
            int prev = 0;
            for (int customer : route) {
                double cruise = cfg.drone.cruise_time(cfg.drone_distances[prev][customer]);
                time += takeoff + cruise + landing;
                energy += cfg.drone.landing_power(weight) * landing
                       + cfg.drone.takeoff_power(weight) * takeoff
                       + cfg.drone.cruise_power(weight) * cruise;
                weight += customer_demand(cfg, static_cast<std::size_t>(customer));
                waiting_time_violation += std::max(0.0, wt - time - cfg.waiting_time_limit);
                prev = customer;
            }
            energy_violation += std::max(0.0, energy - cfg.drone.battery());
            fixed_time_violation += std::max(0.0, wt - cfg.drone.fixed_time());
        }
        res.drone_working_time[d_idx] = sum;
        working_time = std::max(working_time, sum);
    }

    energy_violation /= std::max(1.0, cfg.drone.battery());
    waiting_time_violation /= std::max(1.0, cfg.waiting_time_limit);
    fixed_time_violation /= std::max(1.0, cfg.drone.fixed_time());

    res.energy_violation = energy_violation;
    res.capacity_violation = capacity_violation;
    res.waiting_time_violation = waiting_time_violation;
    res.fixed_time_violation = fixed_time_violation;
    res.feasible = (energy_violation == 0.0 && capacity_violation == 0.0 && waiting_time_violation == 0.0 && fixed_time_violation == 0.0);
    return res;
}

static common::Elite build_initial_elite(const Config& cfg) {
    std::vector<std::size_t> index;
    index.reserve(cfg.customers_count);
    for (std::size_t i = 1; i <= cfg.customers_count; ++i) {
        index.push_back(i);
    }

    auto clusters = clusterize(index, std::max<std::size_t>(1, cfg.trucks_count));

    std::vector<std::vector<std::vector<int>>> truck_routes(cfg.trucks_count);
    std::vector<std::vector<std::vector<int>>> drone_routes(cfg.drones_count);

    std::vector<std::size_t> clusters_mapping(cfg.customers_count + 1, 0);
    for (std::size_t ci = 0; ci < clusters.size(); ++ci) {
        for (std::size_t c : clusters[ci]) {
            clusters_mapping[c] = ci;
        }
    }

    auto feasible_check = [&](const std::vector<std::vector<std::vector<int>>>& tr,
                              const std::vector<std::vector<std::vector<int>>>& dr) {
        return build_local_solution(cfg, tr, dr).feasible;
    };

    std::vector<bool> truckable(cfg.customers_count + 1, false);
    std::vector<bool> dronable_local(cfg.customers_count + 1, false);
    truckable[0] = true;
    dronable_local[0] = true;

    if (cfg.trucks_count > 0) {
        for (std::size_t c = 1; c <= cfg.customers_count; ++c) {
            truck_routes[0].push_back({static_cast<int>(c)});
            truckable[c] = feasible_check(truck_routes, drone_routes);
            truck_routes[0].pop_back();
        }
    }
    if (cfg.drones_count > 0) {
        for (std::size_t c = 1; c <= cfg.customers_count; ++c) {
            if (cfg.dronable[c]) {
                drone_routes[0].push_back({static_cast<int>(c)});
                dronable_local[c] = feasible_check(truck_routes, drone_routes);
                drone_routes[0].pop_back();
            }
        }
    }

    for (std::size_t c = 1; c <= cfg.customers_count; ++c) {
        if (!truckable[c] && !dronable_local[c]) {
            throw std::runtime_error("Customer " + std::to_string(c) +
                " cannot be served by neither trucks nor drones");
        }
    }

    struct State {
        double working_time;
        std::size_t vehicle;
        std::size_t parent;
        std::size_t index;
        bool is_truck;
        bool operator>(const State& o) const {
            return working_time > o.working_time;
        }
    };

    std::priority_queue<State, std::vector<State>, std::greater<State>> queue;

    std::mt19937_64 rng;
    if (cfg.seed) rng.seed(*cfg.seed);
    else          rng.seed(std::random_device{}());

    for (std::size_t ci = 0; ci < clusters.size(); ++ci) {
        auto& cluster = clusters[ci];
        if (cluster.empty()) continue;

        std::shuffle(cluster.begin(), cluster.end(), rng);
        for (std::size_t c : cluster) {
            if (truckable[c]) {
                std::size_t vehicle = (cfg.trucks_count > 0) ? (ci % cfg.trucks_count) : 0;
                queue.push({0.0, vehicle, 0, cluster.front(), true});
                break;
            }
        }

        std::sort(cluster.begin(), cluster.end(), [&](std::size_t a, std::size_t b) {
            return cfg.drone_distances[0][a] < cfg.drone_distances[0][b];
        });
        for (std::size_t c : cluster) {
            if (dronable_local[c]) {
                queue.push({0.0, ci, 0, cluster[0], false});
                break;
            }
        }
    }

    std::set<std::size_t> global_set;
    for (std::size_t i = 1; i <= cfg.customers_count; ++i) global_set.insert(i);

    auto truck_next = [&](std::size_t parent, std::size_t vehicle) {
        double best_d = std::numeric_limits<double>::infinity();
        std::size_t best_c = 0;
        const auto& cl = clusters[clusters_mapping[parent]];
        for (std::size_t c : cl) {
            if (truckable[c] && global_set.count(c) && cfg.truck_distances[parent][c] < best_d) {
                best_d = cfg.truck_distances[parent][c];
                best_c = c;
            }
        }
        if (best_c == 0) {
            for (std::size_t c : global_set) {
                if (truckable[c] && cfg.truck_distances[parent][c] < best_d) {
                    best_d = cfg.truck_distances[parent][c];
                    best_c = c;
                }
            }
        }
        if (best_c != 0) {
            auto metrics = build_local_solution(cfg, truck_routes, drone_routes);
            std::size_t vv = (metrics.truck_working_time.empty() ? 0 : std::min(vehicle, metrics.truck_working_time.size() - 1));
            double wt = metrics.truck_working_time[vv];
            State next_state{wt, vehicle, parent, best_c, true};
            queue.push(next_state);
        }
    };

    auto drone_next = [&](std::size_t parent, std::size_t vehicle) {
        double best_d = std::numeric_limits<double>::infinity();
        std::size_t best_c = 0;
        const auto& cl = clusters[clusters_mapping[parent]];
        for (std::size_t c : cl) {
            if (dronable_local[c] && global_set.count(c) && cfg.drone_distances[parent][c] < best_d) {
                best_d = cfg.drone_distances[parent][c];
                best_c = c;
            }
        }
        if (best_c == 0) {
            for (std::size_t c : global_set) {
                if (dronable_local[c] && cfg.drone_distances[parent][c] < best_d) {
                    best_d = cfg.drone_distances[parent][c];
                    best_c = c;
                }
            }
        }
        if (best_c != 0) {
            auto metrics = build_local_solution(cfg, truck_routes, drone_routes);
            std::size_t vv = (metrics.drone_working_time.empty() ? 0 : std::min(vehicle, metrics.drone_working_time.size() - 1));
            double wt = metrics.drone_working_time[vv];
            State next_state{wt, vehicle, parent, best_c, false};
            queue.push(next_state);
        }
    };

    while (!global_set.empty()) {
        if (queue.empty()) {
            throw std::runtime_error("Cannot construct initial solution – unservable customers remain");
        }

        State packed = queue.top();
        queue.pop();
        std::size_t v = packed.vehicle;

        auto& cl = clusters[clusters_mapping[packed.index]];
        auto it = std::find(cl.begin(), cl.end(), packed.index);

        if (it != cl.end()) {
            if (packed.is_truck) {
                if (packed.parent == 0) {
                    std::size_t vv = (truck_routes.empty() ? 0 : std::min(v, truck_routes.size() - 1));
                    truck_routes[vv].push_back({static_cast<int>(packed.index)});
                } else {
                    std::size_t vv = (truck_routes.empty() ? 0 : std::min(v, truck_routes.size() - 1));
                    truck_routes[vv].back().push_back(static_cast<int>(packed.index));
                }
            } else {
                if (packed.parent == 0) {
                    std::size_t vv = (drone_routes.empty() ? 0 : std::min(v, drone_routes.size() - 1));
                    drone_routes[vv].push_back({static_cast<int>(packed.index)});
                } else {
                    std::size_t vv = (drone_routes.empty() ? 0 : std::min(v, drone_routes.size() - 1));
                    drone_routes[vv].back().push_back(static_cast<int>(packed.index));
                }
            }

            if (feasible_check(truck_routes, drone_routes)) {
                cl.erase(it);
                global_set.erase(packed.index);

                if (packed.is_truck) {
                    truck_next(packed.index, v);
                } else {
                    drone_next(cfg.single_drone_route ? 0 : packed.index, v);
                }
            } else {
                if (packed.is_truck) {
                    std::size_t vv = (truck_routes.empty() ? 0 : std::min(v, truck_routes.size() - 1));
                    if (packed.parent == 0) {
                        truck_routes[vv].pop_back();
                    } else {
                        truck_routes[vv].back().pop_back();
                    }
                    if (!cfg.single_truck_route) {
                        truck_next(0, v);
                    }
                } else {
                    std::size_t vv = (drone_routes.empty() ? 0 : std::min(v, drone_routes.size() - 1));
                    if (packed.parent == 0) {
                        drone_routes[vv].pop_back();
                    } else {
                        drone_routes[vv].back().pop_back();
                    }
                    drone_next(0, v);
                }
            }
        } else {
            if (packed.is_truck) {
                truck_next(packed.parent, v);
            } else {
                drone_next(cfg.single_drone_route ? 0 : packed.parent, v);
            }
        }
    }

    if (cfg.drones_count > 0) {
        std::vector<std::vector<int>> all_routes;
        for (auto& routes : drone_routes) {
            for (auto& route : routes) {
                all_routes.push_back(route);
            }
        }

        std::sort(all_routes.begin(), all_routes.end(), [&](const auto& a, const auto& b) {
            return drone_route_time(cfg, a) > drone_route_time(cfg, b);
        });

        drone_routes.assign(cfg.drones_count, {});
        std::vector<double> wt(cfg.drones_count, 0.0);
        for (const auto& route : all_routes) {
            std::size_t best = std::min_element(wt.begin(), wt.end()) - wt.begin();
            drone_routes[best].push_back(route);
            wt[best] += drone_route_time(cfg, route);
        }
    } else {
        drone_routes.clear();
    }

    common::Elite elite;
    elite.elements.reserve(cfg.trucks_count + cfg.drones_count);

    for (std::size_t truck = 0; truck < cfg.trucks_count; ++truck) {
        common::EliteElement element;
        element.type = 0;
        element.vehicle_number = static_cast<int>(truck);
        for (const auto& route : truck_routes[truck]) {
            element.trips.push_back(route_to_trip(route));
        }
        elite.elements.push_back(std::move(element));
    }

    for (std::size_t drone = 0; drone < cfg.drones_count; ++drone) {
        common::EliteElement element;
        element.type = 1;
        element.vehicle_number = static_cast<int>(drone);
        for (const auto& route : drone_routes[drone]) {
            element.trips.push_back(route_to_trip(route));
        }
        elite.elements.push_back(std::move(element));
    }

    return elite;
}

void worker(int rank) {
    const Config &cfg = global_config();
    std::mt19937_64 rng = make_rng(cfg);

    common::Elite elite = build_initial_elite(cfg);
    elite.worker_rank = rank;
    
    double best_cost = solutions::compute_elite_cost(cfg, elite);
    std::cerr << "[Worker " << rank << "] Initial elite cost: " << best_cost << std::endl;
    
    elite.worker_rank = rank;
    auto buf = common::pack_elite(elite);
    int n = (int) buf.size();
    MPI_Send(&n, 1, MPI_INT, common::MASTER_RANK, common::TAG_PUSH_ELITE_WORKER_REQUEST, MPI_COMM_WORLD);
    MPI_Send(buf.data(), n, MPI_INT, common::MASTER_RANK, common::TAG_PUSH_ELITE_WORKER_REQUEST, MPI_COMM_WORLD);
    
    int no_improve_count = 0;
    int pull_count = 0;
    int iter = 0;
    std::vector<int> neighborhood_order = solutions::generate_neighborhood_order(rng);

    while (true) {
        const int nh_idx = neighborhood_order[iter % static_cast<int>(neighborhood_order.size())];
        common::Elite candidate = elite;
        iter++;

        switch (static_cast<solutions::Neighborhood>(nh_idx)) {
            case solutions::Neighborhood::Move10:
                candidate = solutions::apply_move10(cfg, candidate, rng);
                break;
            case solutions::Neighborhood::Move11:
                candidate = solutions::apply_move11(cfg, candidate, rng);
                break;
            case solutions::Neighborhood::Move20:
                candidate = solutions::apply_move20(cfg, candidate, rng);
                break;
            case solutions::Neighborhood::Move21:
                candidate = solutions::apply_move21(cfg, candidate, rng);
                break;
            case solutions::Neighborhood::Move22:
                candidate = solutions::apply_move22(cfg, candidate, rng);
                break;
            case solutions::Neighborhood::TwoOpt:
                candidate = solutions::apply_twoopt(cfg, candidate, rng);
                break;
            case solutions::Neighborhood::EjectionChain:
                candidate = solutions::apply_ejection_chain(cfg, candidate, rng);
                break;
            default:
                break;
        }

        double candidate_cost = solutions::compute_elite_cost(cfg, candidate);

        if (candidate_cost < best_cost) {
            elite = candidate;
            best_cost = candidate_cost;
            no_improve_count = 0;

            elite.worker_rank = rank;
            auto buf = common::pack_elite(elite);
            int n = (int) buf.size();
            MPI_Send(&n, 1, MPI_INT, common::MASTER_RANK, common::TAG_PUSH_ELITE_WORKER_REQUEST, MPI_COMM_WORLD);
            MPI_Send(buf.data(), n, MPI_INT, common::MASTER_RANK, common::TAG_PUSH_ELITE_WORKER_REQUEST, MPI_COMM_WORLD);
            continue;
        }

        no_improve_count++;

        if (no_improve_count % PULL_ELITE_NO_IMPROVE_INTERVAL == 0) {
            if (++pull_count > STOP_AFTER_PULLS) {
                break;
            }

            int req = rank;
            MPI_Send(&req, 1, MPI_INT, common::MASTER_RANK, common::TAG_PULL_ELITE_WORKER_REQUEST, MPI_COMM_WORLD);

            int n;
            MPI_Recv(&n, 1, MPI_INT, common::MASTER_RANK, common::TAG_ELITE_MASTER_SEND_PULLED, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (n == 0) continue;

            std::vector<int> buf(n);
            MPI_Recv(buf.data(), n, MPI_INT, common::MASTER_RANK, common::TAG_ELITE_MASTER_SEND_PULLED, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            common::Elite pulled = common::unpack_elite(buf);
            pulled.worker_rank = rank;
            elite = std::move(pulled);

            double pulled_cost = solutions::compute_elite_cost(cfg, elite);
            if (pulled_cost < best_cost) {
                best_cost = pulled_cost;
                no_improve_count = 0;
            }
            std::cerr << "[Worker " << rank << "] Pulled elite with cost " << pulled_cost << " (pull " << pull_count << ").\n";
        }
    }

    int done_rank = rank;
    MPI_Send(&done_rank, 1, MPI_INT, common::MASTER_RANK, common::TAG_WORKER_DONE, MPI_COMM_WORLD);

    std::cerr << "[Worker " << rank << "] Final best_cost: " << best_cost << '\n';
}
