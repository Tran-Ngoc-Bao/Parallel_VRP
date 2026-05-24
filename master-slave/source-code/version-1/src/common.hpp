#pragma once
#include <iostream>
#include <vector>
#include <map>

namespace common {

inline constexpr int MASTER_RANK = 0;

inline constexpr int TAG_PUSH_ELITE_WORKER_REQUEST = 1;
inline constexpr int TAG_PULL_ELITE_WORKER_REQUEST = 2;
inline constexpr int TAG_ELITE_MASTER_SEND_PULLED = 3;
inline constexpr int TAG_WORKER_DONE = 4;

struct Trip {
    std::map<int, int> customers; // customers[cus] = next customer (-1: last customer)
};

struct EliteElement {
    int type; // 0: truck, 1: drone
    int vehicle_number;
    std::vector<Trip> trips;
};

struct Elite {
    int worker_rank = -1;
    std::size_t pull_count = 0;
    std::vector<EliteElement> elements;
};

std::vector<int> pack_elite(const Elite &e);
Elite unpack_elite(const std::vector<int> &buf);
void print_elite(const Elite &e, std::ostream &os = std::cout);

} // namespace common
