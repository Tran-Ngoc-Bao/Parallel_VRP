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

static double count_diff_elite(const common::Elite &a, const common::Elite &b) {
    std::map<int, int> map_a;
    std::map<int, int> map_b;

    for (const auto &el : a.elements) {
        for (const auto &trip : el.trips) {
            for (const auto &[cus, nxt] : trip.customers) {
                map_a[cus] = nxt;
            }
        }
    }

    for (const auto &el : b.elements) {
        for (const auto &trip : el.trips) {
            for (const auto &[cus, nxt] : trip.customers) {
                map_b[cus] = nxt;
            }
        }
    }

    int diff = 0;

    for (const auto &[cus, next_a] : map_a) {
        auto it = map_b.find(cus);
        if (it == map_b.end() || it->second != next_a) {
            ++diff;
        }
    }

    return static_cast<double>(diff);
}

static void push_elite(const common::Elite &e,
                       int &elite_pool_count,
                       std::vector<common::Elite> &elite_pool) {
    int pool_capacity = static_cast<int>(elite_pool.size());
    if (elite_pool_count < pool_capacity) {
        elite_pool[elite_pool_count++] = e;
    } else {
        auto choose_replacement = [&](bool prefer_pulled_only) -> int {
            int chosen = 0;
            double min_diff = std::numeric_limits<double>::infinity();
            bool found = false;
            for (int i = 0; i < pool_capacity; ++i) {
                if (prefer_pulled_only && elite_pool[i].pull_count == 0) continue;
                const double diff = count_diff_elite(e, elite_pool[i]);
                if (!found || diff < min_diff) {
                    min_diff = diff;
                    chosen = i;
                    found = true;
                }
            }
            return found ? chosen : 0;
        };

        int replace = choose_replacement(true);
        if (elite_pool[replace].pull_count == 0) {
            replace = choose_replacement(false);
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
                score_sum += count_diff_elite(elite_pool[idx], elite_pool[other]);
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

std::size_t compute_elite_pool_size(const Config& cfg, int world_size) {
    constexpr std::size_t kMinClamp = 5;
    constexpr std::size_t kMaxClamp = 50;

    const double scaled = cfg.elite_pool_factor * static_cast<double>(cfg.customers_count) * std::sqrt(static_cast<double>(world_size - 1));
    const std::size_t derived = static_cast<std::size_t>(std::ceil(scaled));
    return std::clamp(derived, kMinClamp, kMaxClamp);
}

std::size_t compute_min_pull_elites_per_worker(const Config& cfg, int world_size) {
    constexpr std::size_t kMinClamp = 1;
    constexpr std::size_t kMaxClamp = 30;

    const double scaled = cfg.min_pull_elites_per_worker_factor * std::sqrt(static_cast<double>(cfg.customers_count)) / static_cast<double>(world_size - 1);
    const std::size_t derived = static_cast<std::size_t>(std::ceil(scaled));
    return std::clamp(derived, kMinClamp, kMaxClamp);
}

void master(int size) {
    const Config &cfg = global_config();
    int elite_pool_count = 0;
    const std::size_t elite_keep_count = compute_elite_pool_size(cfg, size);
    std::vector<common::Elite> elite_pool(elite_keep_count);
    std::vector<char> worker_done(size, 0);
    int done_workers = 0;
    double global_best_cost = std::numeric_limits<double>::infinity();
    common::Elite global_best_elite;    

    MPI_Status status;
    int iterations = 0;
    const int min_pull_elites_per_worker = compute_min_pull_elites_per_worker(cfg, size);
    std::vector<int> worker_pull_requests(size, 0);
    std::vector<char> worker_running(size, 0);
    for (int r = 1; r < size; ++r) worker_running[r] = 1;
    bool stop_after_min_pulls = false;
    
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
            ++worker_pull_requests[worker_rank];
            if (!stop_after_min_pulls) {
                bool all_reached = true;
                for (int r = 1; r < size; ++r) {
                    if (worker_running[r] && worker_pull_requests[r] < min_pull_elites_per_worker) {
                        all_reached = false; break;
                    }
                }
                if (all_reached) stop_after_min_pulls = true;
            }

            if (stop_after_min_pulls) {
                int n = -1;
                MPI_Send(&n, 1, MPI_INT, worker_rank, common::TAG_ELITE_MASTER_SEND_PULLED, MPI_COMM_WORLD);
            } else if (elite_pool_count == 0) {
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
