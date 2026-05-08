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
constexpr int TAG_PROGRESS = 103;
constexpr int TAG_ELITE_UPDATE = 104;
constexpr double kTolerance = 0.001;
constexpr std::size_t kEliteKeepCount = 10;

struct Job {
    bool stop = false;
    bool has_root = false;
    std::size_t seed = 0;
    std::size_t segment_iterations = 0;
    std::size_t round = 0;
    std::string root_json;
};

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

nlohmann::json job_to_json(const Job& job)
{
    nlohmann::json j;
    j["stop"] = job.stop;
    j["has_root"] = job.has_root;
    j["seed"] = job.seed;
    j["segment_iterations"] = job.segment_iterations;
    j["round"] = job.round;
    if (job.has_root) {
        j["root"] = nlohmann::json::parse(job.root_json);
    }
    return j;
}

Job job_from_json(const nlohmann::json& j)
{
    Job job;
    job.stop = j.value("stop", false);
    job.has_root = j.value("has_root", false);
    job.seed = j.value("seed", std::size_t{0});
    job.segment_iterations = j.value("segment_iterations", std::size_t{0});
    job.round = j.value("round", std::size_t{0});
    if (job.has_root && j.contains("root")) {
        job.root_json = j.at("root").dump();
    }
    return job;
}

void send_job(int dest, const Job& job)
{
    send_string_impl(dest, TAG_JOB, job_to_json(job).dump());
}

Job recv_job(int source)
{
    return job_from_json(nlohmann::json::parse(recv_string_impl(source, TAG_JOB)));
}

void send_result(int dest, const Solution& solution)
{
    send_string_impl(dest, TAG_RESULT, solution.to_json().dump());
}

void send_solution(int dest, int tag, const Solution& solution)
{
    send_string_impl(dest, tag, solution.to_json().dump());
}

Solution recv_result(int source)
{
    return Solution::from_json(nlohmann::json::parse(recv_string_impl(source, TAG_RESULT)));
}

Solution recv_solution(int source, int tag)
{
    return Solution::from_json(nlohmann::json::parse(recv_string_impl(source, tag)));
}

std::size_t derive_segment_iterations(const Config& cfg, int world_size)
{
    if (cfg.fix_iteration) {
        return *cfg.fix_iteration;
    }

    std::size_t base = std::max<std::size_t>(1, cfg.customers_count * cfg.adaptive_iterations);
    std::size_t workers = world_size > 1 ? static_cast<std::size_t>(world_size - 1) : 1;
    return std::max<std::size_t>(1000, base / workers);
}

static std::size_t compute_edge_difference(const Solution& a, const Solution& b)
{
    auto pack_edge = [](std::size_t u, std::size_t v) -> uint64_t {
        if (u > v) std::swap(u, v);
        return (static_cast<uint64_t>(u) << 32) | static_cast<uint64_t>(v);
    };

    auto pack_typed_edge = [&](std::size_t u, std::size_t v, int vehicle_type) -> uint64_t {
        return (static_cast<uint64_t>(vehicle_type & 1) << 63) | pack_edge(u, v);
    };

    auto collect_edge_counts = [&](const Solution& sol, std::unordered_map<uint64_t, std::size_t>& out) {
        for (const auto& truck_vehicle : sol.truck_routes) {
            for (const auto& route : truck_vehicle) {
                const auto& customers = route->data().customers;
                if (customers.size() < 2) continue;
                for (std::size_t i = 0; i < customers.size(); ++i) {
                    std::size_t u = customers[i];
                    std::size_t v = customers[(i + 1) % customers.size()];
                    out[pack_typed_edge(u, v, 0)] += 1;
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
                    out[pack_typed_edge(u, v, 1)] += 1;
                }
            }
        }
    };

    std::unordered_map<uint64_t, std::size_t> ea, eb;
    collect_edge_counts(a, ea);
    collect_edge_counts(b, eb);

    std::unordered_set<uint64_t> keys;
    for (const auto& p : ea) keys.insert(p.first);
    for (const auto& p : eb) keys.insert(p.first);

    std::size_t diff = 0;
    for (auto key : keys) {
        std::size_t ca = 0;
        std::size_t cb = 0;
        auto ita = ea.find(key);
        if (ita != ea.end()) ca = ita->second;
        auto itb = eb.find(key);
        if (itb != eb.end()) cb = itb->second;
        diff += (ca > cb) ? (ca - cb) : (cb - ca);
    }

    return diff;
}

struct ElitePool {
    explicit ElitePool(std::size_t keep_count) : keep_count(keep_count) {}

    void consider(Solution candidate, int source_worker)
    {
        if (!candidate.feasible) {
            return;
        }

        if (solutions.size() < keep_count) {
            solutions.push_back({std::move(candidate), source_worker});
        } else {
            std::size_t most_similar_idx = 0;
            std::size_t min_diff = std::numeric_limits<std::size_t>::max();
            for (std::size_t i = 0; i < solutions.size(); ++i) {
                std::size_t diff = compute_edge_difference(candidate, solutions[i].sol);
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

    const Solution& best() const
    {
        auto it = std::min_element(solutions.begin(), solutions.end(),
                                   [](const Entry& a, const Entry& b) {
                                       return a.sol.cost() < b.sol.cost();
                                   });
        return it->sol;
    }

    const Solution& pick_for_broadcast(int excluded_worker, std::mt19937& rng) const
    {
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < solutions.size(); ++i) {
            if (solutions[i].source_worker != excluded_worker) {
                candidates.push_back(i);
            }
        }
        if (candidates.empty()) {
            return solutions.front().sol;
        }
        std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
        return solutions[candidates[dist(rng)]].sol;
    }

    const Solution& pick_for_dispatch(int excluded_worker, std::mt19937& rng) const
    {
        return pick_for_broadcast(excluded_worker, rng);
    }

private:
    struct Entry {
        Solution sol;
        int source_worker;
    };

    std::size_t keep_count;
    std::vector<Entry> solutions;
};

} // namespace

Solution run_master(int world_size)
{
    const auto t0 = std::chrono::steady_clock::now();
    const Config base_cfg = global_config();
    const std::size_t rounds = std::max<std::size_t>(1, base_cfg.parallel_rounds);
    const std::size_t segment_iterations = derive_segment_iterations(base_cfg, world_size);

    Solution best_solution = Solution::initialize();
    ElitePool elite_pool(kEliteKeepCount);
    elite_pool.consider(best_solution, 0);
    const auto t1 = std::chrono::steady_clock::now();
    std::size_t next_seed = base_cfg.seed ? *base_cfg.seed : 1;
    std::vector<std::size_t> remaining_rounds(static_cast<std::size_t>(world_size), 0);
    std::vector<bool> worker_active(static_cast<std::size_t>(world_size), false);
    std::size_t active_workers = 0;
    std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));

    auto dispatch = [&](int worker_rank, std::size_t round) {
        const Solution& root = elite_pool.pick_for_dispatch(worker_rank, rng);
        Job job;
        job.has_root = true;
        job.root_json = root.to_json().dump();
        job.seed = next_seed++;
        job.segment_iterations = segment_iterations;
        job.round = round;
        send_job(worker_rank, job);
        remaining_rounds[static_cast<std::size_t>(worker_rank)] = rounds > round + 1 ? rounds - round - 1 : 0;
        worker_active[static_cast<std::size_t>(worker_rank)] = true;
    };

    auto broadcast_elite = [&](int except_rank) {
        if (elite_pool.empty()) {
            return;
        }
        for (int worker_rank = 1; worker_rank < world_size; ++worker_rank) {
            if (worker_rank == except_rank) {
                continue;
            }
            if (!worker_active[static_cast<std::size_t>(worker_rank)]) {
                continue;
            }
            const Solution& elite_to_send = elite_pool.pick_for_broadcast(worker_rank, rng);
            send_solution(worker_rank, TAG_ELITE_UPDATE, elite_to_send);
        }
    };

    for (int worker_rank = 1; worker_rank < world_size; ++worker_rank) {
        dispatch(worker_rank, 0);
        ++active_workers;
    }

    while (active_workers > 0) {
        bool processed = false;
        bool improved = false;
        int improve_source = -1;

        for (;;) {
            MPI_Status status{};
            int ready = 0;
            MPI_Iprobe(MPI_ANY_SOURCE, TAG_PROGRESS, MPI_COMM_WORLD, &ready, &status);
            if (!ready) {
                break;
            }

            processed = true;
            Solution progress = recv_solution(status.MPI_SOURCE, TAG_PROGRESS);
            if (progress.feasible) {
                if (progress.cost() < best_solution.cost()) {
                    best_solution = progress;
                }
                elite_pool.consider(std::move(progress), status.MPI_SOURCE);
                improved = true;
                improve_source = status.MPI_SOURCE;
            }
        }

        if (improved) {
            broadcast_elite(improve_source);
        }

        improved = false;
        improve_source = -1;
        for (;;) {
            MPI_Status status{};
            int ready = 0;
            MPI_Iprobe(MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &ready, &status);
            if (!ready) {
                break;
            }

            processed = true;
            const int worker_rank = status.MPI_SOURCE;
            Solution result = recv_result(worker_rank);
            if (result.feasible) {
                if (result.cost() < best_solution.cost()) {
                    best_solution = result;
                }
                elite_pool.consider(std::move(result), worker_rank);
                improved = true;
                improve_source = worker_rank;
            }

            if (remaining_rounds[static_cast<std::size_t>(worker_rank)] > 0) {
                dispatch(worker_rank, rounds - remaining_rounds[static_cast<std::size_t>(worker_rank)]);
            } else {
                Job stop_job;
                stop_job.stop = true;
                send_job(worker_rank, stop_job);
                worker_active[static_cast<std::size_t>(worker_rank)] = false;
                --active_workers;
            }
        }

        if (improved) {
            broadcast_elite(improve_source);
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
    (void)rank;
    const Config base_cfg = global_config();

    for (;;) {
        for (;;) {
            MPI_Status status{};
            int ready = 0;
            MPI_Iprobe(0, TAG_ELITE_UPDATE, MPI_COMM_WORLD, &ready, &status);
            if (!ready) {
                break;
            }
            (void)recv_solution(0, TAG_ELITE_UPDATE);
        }

        Job job = recv_job(0);
        if (job.stop) {
            break;
        }

        Config worker_cfg = base_cfg;
        worker_cfg.seed = job.seed;
        worker_cfg.fix_iteration = job.segment_iterations;
        worker_cfg.disable_logging = true;
        set_global_config(worker_cfg);

        Solution root = job.has_root
            ? Solution::from_json(nlohmann::json::parse(job.root_json))
            : Solution::initialize();

        const std::size_t sync_interval = std::max<std::size_t>(1, job.segment_iterations / 20);
        Solution::SyncHooks hooks;
        hooks.sync_interval = sync_interval;
        hooks.push_incumbent = [&](std::size_t, const Solution& incumbent) {
            if (incumbent.feasible) {
                send_solution(0, TAG_PROGRESS, incumbent);
            }
        };
        hooks.pull_elite = [&](std::size_t, Solution& pulled_elite) -> bool {
            bool has_update = false;
            for (;;) {
                MPI_Status status{};
                int ready = 0;
                MPI_Iprobe(0, TAG_ELITE_UPDATE, MPI_COMM_WORLD, &ready, &status);
                if (!ready) {
                    break;
                }
                pulled_elite = recv_solution(0, TAG_ELITE_UPDATE);
                has_update = true;
            }
            return has_update;
        };

        Logger logger;
        Solution result = Solution::tabu_search(root, logger, &hooks);
        send_result(0, result);
    }
}

} // namespace parallel