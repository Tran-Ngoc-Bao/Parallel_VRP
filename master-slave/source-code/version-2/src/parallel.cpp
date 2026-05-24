#include "parallel.hpp"

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <random>
#include <ctime>
#include <map>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "logger.hpp"
#include "solutions.hpp"

namespace parallel {
namespace {

constexpr int TAG_JOB    = 101;
constexpr int TAG_RESULT = 102;
constexpr int TAG_ELITE_PUSH = 104;  // worker -> master: push elite
constexpr int TAG_ELITE_PULL = 105;  // worker -> master: pull request
constexpr int TAG_ELITE_REPLY = 106; // master -> worker: reply to pull request

void send_string_impl(int dest, int tag, const std::string& payload)
{
    int size = static_cast<int>(payload.size());
    MPI_Send(&size, 1, MPI_INT, dest, tag, MPI_COMM_WORLD);
    if (size > 0) {
        MPI_Send(payload.data(), size, MPI_CHAR, dest, tag, MPI_COMM_WORLD);
    }
}

std::string recv_string_impl(int source, int tag)
{
    MPI_Status status{};
    int size = 0;
    MPI_Recv(&size, 1, MPI_INT, source, tag, MPI_COMM_WORLD, &status);
    std::string payload(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        MPI_Recv(payload.data(), size, MPI_CHAR, source, tag, MPI_COMM_WORLD, &status);
    }
    return payload;
}

void send_seed(int dest, std::size_t seed)
{
    nlohmann::json j;
    j["seed"] = seed;
    send_string_impl(dest, TAG_JOB, j.dump());
}

std::size_t recv_seed(int source)
{
    nlohmann::json j = nlohmann::json::parse(recv_string_impl(source, TAG_JOB));
    return j.value("seed", std::size_t{0});
}

void send_solution(int dest, int tag, const Solution& solution)
{
    send_string_impl(dest, tag, solution.to_json().dump());
}

Solution recv_solution(int source, int tag)
{
    return Solution::from_json(nlohmann::json::parse(recv_string_impl(source, tag)));
}

bool is_valid_solution_for_exchange(const Solution& s)
{
    if (!s.feasible) return false;
    try {
        s.verify();
        return true;
    } catch (...) {
        return false;
    }
}

struct EdgeLossStats {
    std::size_t edges_lost = 0;
    std::size_t total_edges_in_a = 0;
};

struct AssignmentMismatchStats {
    std::size_t mismatched_customers = 0;
    std::size_t total_customers = 0;
};

static EdgeLossStats
compute_edge_loss_from_a_to_b(const Solution& a, const Solution& b)
{
    auto pack_edge = [](std::size_t u, std::size_t v) -> uint64_t {
        if (u > v) std::swap(u, v);
        return (static_cast<uint64_t>(u) << 32) | static_cast<uint64_t>(v);
    };

    std::unordered_map<uint64_t, std::size_t> ea, eb;
    auto collect_edge_counts = [&](const Solution& sol, std::unordered_map<uint64_t, std::size_t>& out) {
        out.clear();
        for (const auto& truck_vehicle : sol.truck_routes) {
            for (const auto& route : truck_vehicle) {
                const auto& customers = route->data().customers;
                if (customers.size() < 2) continue;
                for (std::size_t i = 0; i < customers.size(); ++i) {
                    std::size_t u = customers[i];
                    std::size_t v = customers[(i + 1) % customers.size()];
                    out[pack_edge(u, v)] += 1;
                }
            }
        }
        for (const auto& drone_vehicle : sol.drone_routes) {
            for (const auto& route : drone_vehicle) {
                const auto& customers = route->data().customers;
                if (customers.size() < 2) continue;
                for (std::size_t i = 0; i < customers.size(); ++i) {
                    std::size_t u = customers[i];
                    std::size_t v = customers[(i + 1) % customers.size()];
                    out[pack_edge(u, v)] += 1;
                }
            }
        }
    };

    collect_edge_counts(a, ea);
    collect_edge_counts(b, eb);

    std::size_t edge_loss_raw = 0;
    std::size_t total_edges_a = 0;
    for (const auto &p : ea) total_edges_a += p.second;

    for (const auto &p : ea) {
        std::size_t ca = p.second;
        std::size_t cb = 0;
        auto itb = eb.find(p.first);
        if (itb != eb.end()) cb = itb->second;
        if (ca > cb) edge_loss_raw += (ca - cb);
    }

    return EdgeLossStats{edge_loss_raw, total_edges_a};
}

static AssignmentMismatchStats
compute_assignment_mismatch_from_a_to_b(const Solution& a, const Solution& b)
{
    const Config &cfg = global_config();
    std::size_t customers = cfg.customers_count;
    std::size_t assign_diff_count = 0;
    if (customers == 0) return AssignmentMismatchStats{0, 0};

    std::vector<int> assign_a(customers, -1);
    std::vector<int> assign_b(customers, -1);
    for (const auto &truck_vehicle : a.truck_routes) for (const auto &route : truck_vehicle)
        for (auto cid : route->data().customers) if (cid < customers) assign_a[cid] = 0;
    for (const auto &drone_vehicle : a.drone_routes) for (const auto &route : drone_vehicle)
        for (auto cid : route->data().customers) if (cid < customers) assign_a[cid] = 1;
    for (const auto &truck_vehicle : b.truck_routes) for (const auto &route : truck_vehicle)
        for (auto cid : route->data().customers) if (cid < customers) assign_b[cid] = 0;
    for (const auto &drone_vehicle : b.drone_routes) for (const auto &route : drone_vehicle)
        for (auto cid : route->data().customers) if (cid < customers) assign_b[cid] = 1;

    for (std::size_t cid = 0; cid < customers; ++cid) if (assign_a[cid] != assign_b[cid]) ++assign_diff_count;
    return AssignmentMismatchStats{assign_diff_count, customers};
}

static double count_diff_elite(
    const Solution& a,
    const Solution& b,
    double w_edge,
    double w_assign)
{
    if (w_edge == 1.0 && w_assign == 0.0) {
        EdgeLossStats edge_stats = compute_edge_loss_from_a_to_b(a, b);
        return (edge_stats.total_edges_in_a > 0)
            ? (static_cast<double>(edge_stats.edges_lost) /
               static_cast<double>(edge_stats.total_edges_in_a))
            : 0.0;
    }

    if (w_edge == 0.0 && w_assign == 1.0) {
        AssignmentMismatchStats assignment_stats = compute_assignment_mismatch_from_a_to_b(a, b);
        return (assignment_stats.total_customers > 0)
            ? (static_cast<double>(assignment_stats.mismatched_customers) /
               static_cast<double>(assignment_stats.total_customers))
            : 0.0;
    }

    EdgeLossStats edge_stats = compute_edge_loss_from_a_to_b(a, b);
    AssignmentMismatchStats assignment_stats = compute_assignment_mismatch_from_a_to_b(a, b);

    double normalized_edge = (edge_stats.total_edges_in_a > 0)
        ? (static_cast<double>(edge_stats.edges_lost) /
           static_cast<double>(edge_stats.total_edges_in_a))
        : 0.0;

    double assignment_diff = (assignment_stats.total_customers > 0)
        ? (static_cast<double>(assignment_stats.mismatched_customers) /
           static_cast<double>(assignment_stats.total_customers))
        : 0.0;

    return w_edge * normalized_edge + w_assign * assignment_diff;
}

struct ElitePool {
    explicit ElitePool(std::size_t keep_count) : keep_count(keep_count) {}

    void consider(Solution candidate, int source_worker)
    {
        if (!is_valid_solution_for_exchange(candidate)) {
            return;
        }

        if (solutions.size() < keep_count) {
            solutions.push_back({std::move(candidate), source_worker});
        } else {
            std::size_t most_similar_idx = 0;
            double min_diff = std::numeric_limits<double>::infinity();
            const Config &cfg = global_config();
            double w_edge = cfg.diversity_weight_edge;
            double w_assign = cfg.diversity_weight_assignment;
            for (std::size_t i = 0; i < solutions.size(); ++i) {
                double diff = count_diff_elite(candidate, solutions[i].sol, w_edge, w_assign);

                if (diff < min_diff) {
                    min_diff = diff;
                    most_similar_idx = i;
                }
            }

            solutions[most_similar_idx] = {std::move(candidate), source_worker};
        }
    }

    bool empty() const
    {
        return solutions.empty();
    }

    const Solution& pick_for_dispatch(int excluded_worker, std::mt19937& rng, cli::ElitePullStrategy strategy)
    {
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < solutions.size(); ++i) {
            if (solutions[i].source_worker != excluded_worker) {
                candidates.push_back(i);
            }
        }
        if (candidates.empty()) {
            ++solutions.front().pull_count;
            return solutions.front().sol;
        }

        auto mark_and_return = [&](std::size_t idx) -> const Solution& {
            ++solutions[idx].pull_count;
            return solutions[idx].sol;
        };

        auto pick_random = [&]() -> const Solution& {
            std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
            return mark_and_return(candidates[dist(rng)]);
        };

        auto pick_topk = [&](std::size_t k) -> const Solution& {
            std::vector<std::size_t> sorted = candidates;
            std::sort(sorted.begin(), sorted.end(), [&](std::size_t lhs, std::size_t rhs) {
                return solutions[lhs].sol.cost() < solutions[rhs].sol.cost();
            });
            k = std::min(k, sorted.size());
            std::uniform_int_distribution<std::size_t> dist(0, k - 1);
            return mark_and_return(sorted[dist(rng)]);
        };

        auto pick_rank_based = [&]() -> const Solution& {
            std::vector<std::size_t> sorted = candidates;
            std::sort(sorted.begin(), sorted.end(), [&](std::size_t lhs, std::size_t rhs) {
                return solutions[lhs].sol.cost() < solutions[rhs].sol.cost();
            });
            std::vector<double> weights(sorted.size(), 0.0);
            for (std::size_t i = 0; i < sorted.size(); ++i) {
                weights[i] = 1.0 / static_cast<double>(i + 1);
            }
            std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
            return mark_and_return(sorted[dist(rng)]);
        };

        auto pick_pullcount_based = [&]() -> const Solution& {
            std::vector<double> weights;
            weights.reserve(candidates.size());
            for (std::size_t idx : candidates) {
                weights.push_back(1.0 / (1.0 + static_cast<double>(solutions[idx].pull_count)));
            }
            std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
            return mark_and_return(candidates[dist(rng)]);
        };

        auto pick_diverse = [&]() -> const Solution& {
            const Config &cfg = global_config();
            std::size_t best_idx = candidates.front();
            double best_score = std::numeric_limits<double>::lowest();
            for (std::size_t idx : candidates) {
                double total_score = 0.0;
                std::size_t count = 0;
                for (std::size_t other = 0; other < solutions.size(); ++other) {
                    if (other == idx) continue;
                    double diff = count_diff_elite(
                        solutions[idx].sol,
                        solutions[other].sol,
                        cfg.diversity_weight_edge,
                        cfg.diversity_weight_assignment);
                    total_score += diff;
                    ++count;
                }
                double score = count > 0 ? total_score / static_cast<double>(count) : 0.0;
                if (score > best_score) {
                    best_score = score;
                    best_idx = idx;
                }
            }
            return mark_and_return(best_idx);
        };

        switch (strategy) {
            case cli::ElitePullStrategy::Random:    return pick_random();
            case cli::ElitePullStrategy::TopK: {
                // derive k from pool size (keep_count)
                std::size_t k = std::max<std::size_t>(1, keep_count / 2);
                return pick_topk(k);
            }
            case cli::ElitePullStrategy::Rank:      return pick_rank_based();
            case cli::ElitePullStrategy::PullCount: return pick_pullcount_based();
            case cli::ElitePullStrategy::Diverse:   return pick_diverse();
        }

        return pick_random();
    }

private:
    struct Entry {
        Solution sol;
        int source_worker;
        std::size_t pull_count = 0;
    };

    std::size_t keep_count;
    std::vector<Entry> solutions;
};

} // namespace

Solution run_master(int world_size)
{
    const auto t0 = std::chrono::steady_clock::now();
    const Config base_cfg = global_config();
    // Keep a valid fallback so verify() never receives an empty solution.
    Solution best_solution = Solution::initialize();
    const std::size_t elite_keep_count = std::clamp(static_cast<std::size_t>(base_cfg.elite_pool_factor * static_cast<double>(base_cfg.customers_count)), static_cast<std::size_t>(5), static_cast<std::size_t>(50));
    ElitePool elite_pool(elite_keep_count);
    const auto t1 = std::chrono::steady_clock::now();
    std::size_t next_seed = base_cfg.seed ? *base_cfg.seed : 1;

    std::vector<bool> worker_running(static_cast<std::size_t>(world_size), false);
    std::size_t active_workers = 0;
    std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));

    auto start_worker = [&](int worker_rank) {
        send_seed(worker_rank, next_seed++);
        worker_running[static_cast<std::size_t>(worker_rank)] = true;
        ++active_workers;
    };

    for (int worker_rank = 1; worker_rank < world_size; ++worker_rank) {
        start_worker(worker_rank);
    }

    while (active_workers > 0) {
        bool processed = false;

        // Handle elite pushes from workers
        for (;;) {
            MPI_Status status{};
            int ready = 0;
            MPI_Iprobe(MPI_ANY_SOURCE, TAG_ELITE_PUSH, MPI_COMM_WORLD, &ready, &status);
            if (!ready) break;
            processed = true;
            const int worker_rank = status.MPI_SOURCE;
            Solution elite = recv_solution(worker_rank, TAG_ELITE_PUSH);
            if (is_valid_solution_for_exchange(elite)) {
                if (!best_solution.feasible || elite.cost() < best_solution.cost()) {
                    best_solution = elite;
                }
                elite_pool.consider(std::move(elite), worker_rank);
            }
        }

        // Handle elite pull from workers
        for (;;) {
            MPI_Status status{};
            int ready = 0;
            MPI_Iprobe(MPI_ANY_SOURCE, TAG_ELITE_PULL, MPI_COMM_WORLD, &ready, &status);
            if (!ready) break;
            processed = true;
            const int worker_rank = status.MPI_SOURCE;
            std::string req = recv_string_impl(worker_rank, TAG_ELITE_PULL);
            if (elite_pool.empty()) {
                // No elite yet: send current best fallback instead of an empty solution.
                send_solution(worker_rank, TAG_ELITE_REPLY, best_solution);
            } else {
                const Solution& elite_to_send = elite_pool.pick_for_dispatch(
                    worker_rank, rng, base_cfg.elite_pull_strategy);
                send_solution(worker_rank, TAG_ELITE_REPLY, elite_to_send);
            }
        }

        // Handle final results from workers
        for (;;) {
            MPI_Status status{};
            int ready = 0;
            MPI_Iprobe(MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &ready, &status);
            if (!ready) break;
            processed = true;
            const int worker_rank = status.MPI_SOURCE;
            Solution result = recv_solution(worker_rank, TAG_RESULT);
            if (is_valid_solution_for_exchange(result) && (!best_solution.feasible || result.cost() < best_solution.cost())) {
                best_solution = result;
            }
            if (is_valid_solution_for_exchange(result)) {
                elite_pool.consider(std::move(result), worker_rank);
            }
            worker_running[static_cast<std::size_t>(worker_rank)] = false;
            --active_workers;
        }

        if (!processed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    const auto t2 = std::chrono::steady_clock::now();

    const double init_sec = std::chrono::duration<double>(t1 - t0).count();
    const double loop_sec = std::chrono::duration<double>(t2 - t1).count();
    const double total_sec = std::chrono::duration<double>(t2 - t0).count();
    std::cerr << std::fixed << std::setprecision(6)
              << "Timing (mode=parallel-master-slave, unit=s): init="
              << init_sec << " search=" << loop_sec
              << " total=" << total_sec << "\n";

    Logger logger;
    logger.finalize(best_solution, 0, 0, 0, 0, 0, 0.0, 0.0);
    return best_solution;
}

void run_worker(int rank)
{
    const Config base_cfg = global_config();

    std::size_t seed = recv_seed(0);

    Config worker_cfg = base_cfg;
    worker_cfg.seed = seed;
    worker_cfg.disable_logging = true;
    set_global_config(worker_cfg);
    Solution root = Solution::initialize();

    Solution::EliteHooks hooks;
    hooks.push_elite = [&](std::size_t, const Solution& elite) {
        if (is_valid_solution_for_exchange(elite)) {
            send_solution(0, TAG_ELITE_PUSH, elite);
        }
    };
    hooks.pull_elite = [&](std::size_t iteration, Solution& pulled_elite) -> bool {
        nlohmann::json j;
        j["iteration"] = iteration;
        send_string_impl(0, TAG_ELITE_PULL, j.dump());
        pulled_elite = recv_solution(0, TAG_ELITE_REPLY);
        return is_valid_solution_for_exchange(pulled_elite);
    };

    Logger logger;
    Solution result = Solution::tabu_search(root, logger, &hooks);
    send_solution(0, TAG_RESULT, result);
}

} // namespace parallel