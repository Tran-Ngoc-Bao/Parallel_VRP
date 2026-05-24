#include "master.hpp"
#include "common.hpp"
#include "config.hpp"
#include "solutions.hpp"

#include <iostream>
#include <algorithm>
#include <map>
#include <random>
#include <thread>
#include <chrono>
#include <limits>
#include <vector>
#include <sstream>
#include <mpi.h>

static void update_global_best_elite(const common::Elite &candidate,
                                     const Config &cfg,
                                     double &global_best_cost,
                                     common::Elite &global_best_elite) {
    const double candidate_cost = solutions::compute_elite_cost(cfg, candidate);
    if (candidate_cost < global_best_cost) {
        global_best_cost = candidate_cost;
        global_best_elite = candidate;
        std::cerr << "[Master] Updated global best elite with cost " << global_best_cost << " from worker " << candidate.worker_rank << "\n";
    }
}

static std::map<int, int> extract_next_map(const common::Elite &e) {
    std::map<int, int> next_map;
    for (const auto &el : e.elements) {
        for (const auto &trip : el.trips) {
            for (const auto &[cus, nxt] : trip.customers) {
                next_map[cus] = nxt;
            }
        }
    }
    return next_map;
}

static std::map<int, int> extract_vehicle_assignment_map(const common::Elite &e) {
    std::map<int, int> assign_map;

    for (const auto &el : e.elements) {
        for (const auto &trip : el.trips) {
            for (const auto &[cus, nxt] : trip.customers) {
                (void)nxt;
                assign_map[cus] = el.type;
            }
        }
    }
    return assign_map;
}

static int count_edge_diff_elite(const common::Elite &a, const common::Elite &b) {
    const auto map_a = extract_next_map(a);
    const auto map_b = extract_next_map(b);

    int diff = 0;

    for (const auto &[cus, next_a] : map_a) {
        auto it = map_b.find(cus);
        if (it == map_b.end() || it->second != next_a) {
            ++diff;
        }
    }

    return diff;
}

static int count_vehicle_assignment_diff_elite(const common::Elite &a, const common::Elite &b) {
    const auto map_a = extract_vehicle_assignment_map(a);
    const auto map_b = extract_vehicle_assignment_map(b);

    int diff = 0;

    for (const auto &[cus, assign_a] : map_a) {
        auto it = map_b.find(cus);
        if (it == map_b.end() || it->second != assign_a) {
            ++diff;
        }
    }

    return diff;
}

static double count_diff_elite(
    const common::Elite &a,
    const common::Elite &b,
    double w_edge,
    double w_assign
) {
    if (w_edge == 1.0 && w_assign == 0.0) {
        const auto edge_diff = count_edge_diff_elite(a, b);
        const auto edge_map = extract_next_map(a);
        return edge_map.empty()
            ? 0.0
            : static_cast<double>(edge_diff) / static_cast<double>(edge_map.size());
    }

    if (w_edge == 0.0 && w_assign == 1.0) {
        const auto assign_diff = count_vehicle_assignment_diff_elite(a, b);
        const auto assign_map = extract_vehicle_assignment_map(a);
        return assign_map.empty()
            ? 0.0
            : static_cast<double>(assign_diff) / static_cast<double>(assign_map.size());
    }

    const auto edge_map = extract_next_map(a);
    const auto assign_map = extract_vehicle_assignment_map(a);

    const double normalized_edge =
        edge_map.empty()
            ? 0.0
            : static_cast<double>(count_edge_diff_elite(a, b)) / static_cast<double>(edge_map.size());

    const double normalized_assign =
        assign_map.empty()
            ? 0.0
            : static_cast<double>(count_vehicle_assignment_diff_elite(a, b)) / static_cast<double>(assign_map.size());

    return w_edge * normalized_edge + w_assign * normalized_assign;
}

static void push_elite(const common::Elite &e,
                       int &elite_pool_count,
                       std::vector<common::Elite> &elite_pool) {
    int pool_capacity = static_cast<int>(elite_pool.size());
    if (elite_pool_count < pool_capacity) {
        elite_pool[elite_pool_count++] = e;
    } else {
        const Config &cfg = global_config();
        const double w_edge = cfg.diversity_weight_edge;
        const double w_assign = cfg.diversity_weight_assignment;

        int replace = 0;
        double min_diff = count_diff_elite(e, elite_pool[0], w_edge, w_assign);

        for (int i = 1; i < pool_capacity; ++i) {
            const double diff = count_diff_elite(e, elite_pool[i], w_edge, w_assign);
            if (diff < min_diff) {
                min_diff = diff;
                replace = i;
            }
        }

        elite_pool[replace] = e;
    }
}

static void pull_elite(common::Elite &e, std::vector<common::Elite> &elite_pool, int elite_pool_count) {
    const Config &cfg = global_config();

    std::vector<int> candidate_indices;
    candidate_indices.reserve(elite_pool_count);
    for (int i = 0; i < elite_pool_count; ++i) candidate_indices.push_back(i);

    static std::random_device rd;
    static std::mt19937_64 gen(rd());

    auto mark_and_return = [&](int idx) -> common::Elite& {
        ++elite_pool[idx].pull_count;
        return elite_pool[idx];
    };

    auto pick_random = [&]() -> common::Elite& {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(candidate_indices.size()) - 1);
        return mark_and_return(candidate_indices[dist(gen)]);
    };

    auto pick_topk = [&](int k) -> common::Elite& {
        std::vector<int> sorted = candidate_indices;
        std::sort(sorted.begin(), sorted.end(), [&](int lhs, int rhs) {
            return solutions::compute_elite_cost(cfg, elite_pool[lhs]) <
                   solutions::compute_elite_cost(cfg, elite_pool[rhs]);
        });
        k = std::min(k, static_cast<int>(sorted.size()));
        std::uniform_int_distribution<int> dist(0, k - 1);
        return mark_and_return(sorted[dist(gen)]);
    };

    auto pick_rank_based = [&]() -> common::Elite& {
        std::vector<int> sorted = candidate_indices;
        std::sort(sorted.begin(), sorted.end(), [&](int lhs, int rhs) {
            return solutions::compute_elite_cost(cfg, elite_pool[lhs]) <
                   solutions::compute_elite_cost(cfg, elite_pool[rhs]);
        });
        std::vector<double> weights(sorted.size(), 0.0);
        for (std::size_t i = 0; i < sorted.size(); ++i) weights[i] = 1.0 / static_cast<double>(i + 1);
        std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
        return mark_and_return(sorted[dist(gen)]);
    };

    auto pick_pullcount_based = [&]() -> common::Elite& {
        std::vector<double> weights;
        weights.reserve(candidate_indices.size());
        for (int idx : candidate_indices) {
            weights.push_back(1.0 / (1.0 + static_cast<double>(elite_pool[idx].pull_count)));
        }
        std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
        return mark_and_return(candidate_indices[dist(gen)]);
    };

    auto pick_diverse = [&]() -> common::Elite& {
        int best_idx = candidate_indices.front();
        double best_score = std::numeric_limits<double>::lowest();
        for (int idx : candidate_indices) {
            double score_sum = 0.0;
            int count = 0;
            for (int other = 0; other < elite_pool_count; ++other) {
                if (other == idx) continue;
                score_sum += count_diff_elite(elite_pool[idx], elite_pool[other], cfg.diversity_weight_edge, cfg.diversity_weight_assignment);
                ++count;
            }
            const double score = count > 0 ? score_sum / static_cast<double>(count) : 0.0;
            if (score > best_score) {
                best_score = score;
                best_idx = idx;
            }
        }
        return mark_and_return(best_idx);
    };

    common::Elite &picked = [&]() -> common::Elite& {
        switch (cfg.elite_pull_strategy) {
            case cli::ElitePullStrategy::Random:    return pick_random();
            case cli::ElitePullStrategy::TopK: {
                int k = std::max(1, static_cast<int>(elite_pool.size() / 2));
                return pick_topk(k);
            }
            case cli::ElitePullStrategy::Rank:      return pick_rank_based();
            case cli::ElitePullStrategy::PullCount: return pick_pullcount_based();
            case cli::ElitePullStrategy::Diverse:   return pick_diverse();
        }
        return pick_random();
    }();

    e = picked;
}

void master(int size) {
    const Config &cfg = global_config();
    int elite_pool_count = 0;
    const std::size_t elite_keep_count = std::clamp(static_cast<std::size_t>(cfg.elite_pool_factor * static_cast<double>(cfg.customers_count)), static_cast<std::size_t>(5), static_cast<std::size_t>(50));
    std::vector<common::Elite> elite_pool(elite_keep_count);
    std::vector<char> worker_done(size, 0);
    int done_workers = 0;
    double global_best_cost = std::numeric_limits<double>::infinity();
    common::Elite global_best_elite;    

    MPI_Status status;
    int iterations = 0;
    
    while (done_workers < size - 1) {
        iterations++;
        int flag_push = 0, flag_pull = 0, flag_done = 0;

        MPI_Iprobe(MPI_ANY_SOURCE, common::TAG_PUSH_ELITE_WORKER_REQUEST, MPI_COMM_WORLD, &flag_push, &status);

        if (flag_push) {
            int n;
            MPI_Recv(&n, 1, MPI_INT, status.MPI_SOURCE, common::TAG_PUSH_ELITE_WORKER_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            std::vector<int> buf(n);
            MPI_Recv(buf.data(), n, MPI_INT, status.MPI_SOURCE, common::TAG_PUSH_ELITE_WORKER_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            common::Elite new_elite = common::unpack_elite(buf);
            update_global_best_elite(new_elite, cfg, global_best_cost, global_best_elite);
            push_elite(new_elite, elite_pool_count, elite_pool);
        }

        MPI_Iprobe(MPI_ANY_SOURCE, common::TAG_PULL_ELITE_WORKER_REQUEST, MPI_COMM_WORLD, &flag_pull, &status);
        
        if (flag_pull) {
            int worker_rank;
            MPI_Recv(&worker_rank, 1, MPI_INT, status.MPI_SOURCE, common::TAG_PULL_ELITE_WORKER_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (elite_pool_count == 0) {
                int n = 0;
                MPI_Send(&n, 1, MPI_INT, worker_rank, common::TAG_ELITE_MASTER_SEND_PULLED, MPI_COMM_WORLD);
            } else {
                common::Elite pulled_elite;
                pull_elite(pulled_elite, elite_pool, elite_pool_count);

                auto buf = common::pack_elite(pulled_elite);
                int n = (int) buf.size();
                MPI_Send(&n, 1, MPI_INT, worker_rank, common::TAG_ELITE_MASTER_SEND_PULLED, MPI_COMM_WORLD);
                MPI_Send(buf.data(), n, MPI_INT, worker_rank, common::TAG_ELITE_MASTER_SEND_PULLED, MPI_COMM_WORLD);
            }
        }

        MPI_Iprobe(MPI_ANY_SOURCE, common::TAG_WORKER_DONE, MPI_COMM_WORLD, &flag_done, &status);

        if (flag_done) {
            int worker_rank;
            MPI_Recv(&worker_rank, 1, MPI_INT, status.MPI_SOURCE, common::TAG_WORKER_DONE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (worker_rank > common::MASTER_RANK && worker_rank < size && !worker_done[worker_rank]) {
                worker_done[worker_rank] = 1;
                done_workers++;
            }
        }

        if (!flag_push && !flag_pull && !flag_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    {
        std::ostringstream out;
        out << "[Master] Final global_best_cost: " << global_best_cost
            << " (received from worker " << global_best_elite.worker_rank << ")\n";
        out << "[Master] Final global_best_elite:\n";
        common::print_elite(global_best_elite, out);
        std::cerr << out.str();
    }
}
