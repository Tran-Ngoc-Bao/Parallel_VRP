#include "master.hpp"
#include "common.hpp"
#include "config.hpp"
#include "solutions.hpp"

#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <chrono>
#include <limits>
#include <vector>
#include <sstream>
#include <mpi.h>

const int ELITE_POOL_SIZE = 20;

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

static int count_diff_elite(const common::Elite &a, const common::Elite &b) {
    auto extract = [](const common::Elite &e) {
        std::map<int, int> m;
        for (const auto &el : e.elements)
            for (const auto &trip : el.trips)
                for (const auto &[cus, nxt] : trip.customers)
                    m[cus] = nxt;
        return m;
    };

    auto map_a = extract(a);
    auto map_b = extract(b);

    int diff = 0;

    for (const auto &[cus, next_a] : map_a) {
        if (map_b[cus] != next_a)
            diff++;
    }

    return diff;
}

static void push_elite(const common::Elite &e, int &elite_pool_count, std::vector<common::Elite> &elite_pool) {
    if (elite_pool_count < ELITE_POOL_SIZE) {
        elite_pool[elite_pool_count++] = e;
    } else {
        int replace = 0;
        int min_diff = count_diff_elite(e, elite_pool[0]);
        for (int i = 1; i < ELITE_POOL_SIZE; ++i) {
            const int diff = count_diff_elite(e, elite_pool[i]);
            if (diff < min_diff) {
                min_diff = diff;
                replace = i;
            }
        }
        elite_pool[replace] = e;
    }
}

static void pull_elite(common::Elite &e, const std::vector<common::Elite> &elite_pool, int elite_pool_count) {
    std::vector<int> candidate_indices;
    candidate_indices.reserve(elite_pool_count);

    for (int i = 0; i < elite_pool_count; ++i) {
        candidate_indices.push_back(i);
    }

    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, static_cast<int>(candidate_indices.size()) - 1);
    int idx = candidate_indices[dist(gen)];
    e = elite_pool[idx];
}

void master(int size) {
    int elite_pool_count = 0;
    std::vector<common::Elite> elite_pool(ELITE_POOL_SIZE);
    std::vector<char> worker_done(size, 0);
    int done_workers = 0;
    double global_best_cost = std::numeric_limits<double>::infinity();
    common::Elite global_best_elite;

    const Config &cfg = global_config();

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
