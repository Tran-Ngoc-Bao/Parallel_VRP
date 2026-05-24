#include "common.hpp"
#include <set>

namespace common {

static void pack_int(std::vector<int> &buf, int x) {
    buf.push_back(x);
}

static int unpack_int(const std::vector<int> &buf, size_t &i) {
    return buf[i++];
}

std::vector<int> pack_elite(const Elite &e) {
    std::vector<int> buf;
    buf.reserve(128);

    pack_int(buf, e.worker_rank);
    pack_int(buf, static_cast<int>(e.pull_count));
    pack_int(buf, (int) e.elements.size());
    for (const auto &el : e.elements) {
        pack_int(buf, el.type);
        pack_int(buf, el.vehicle_number);

        pack_int(buf, (int) el.trips.size());
        for (const auto &tr : el.trips) {
            pack_int(buf, (int) tr.customers.size());
            for (const auto &[cus, nxt] : tr.customers) {
                pack_int(buf, cus);
                pack_int(buf, nxt);
            }
        }
    }
    return buf;
}

Elite unpack_elite(const std::vector<int> &buf) {
    Elite e;
    size_t i = 0;

    e.worker_rank = unpack_int(buf, i);
    e.pull_count = static_cast<std::size_t>(unpack_int(buf, i));
    int elements_count = unpack_int(buf, i);
    e.elements.resize(elements_count);

    for (int ei = 0; ei < elements_count; ++ei) {
        auto &el = e.elements[ei];
        el.type = unpack_int(buf, i);
        el.vehicle_number = unpack_int(buf, i);

        int trips_count = unpack_int(buf, i);
        el.trips.resize(trips_count);

        for (int ti = 0; ti < trips_count; ++ti) {
            auto &tr = el.trips[ti];

            int customers_count = unpack_int(buf, i);

            for (int ci = 0; ci < customers_count; ++ci) {
                int cus = unpack_int(buf, i);
                int nxt = unpack_int(buf, i);
                tr.customers[cus] = nxt;
            }
        }
    }
    return e;
}

void print_elite(const Elite &e, std::ostream &os) {
    os << "Elite(worker_rank=" << e.worker_rank
       << ", elements=" << e.elements.size() << ")\n";

    for (std::size_t ei = 0; ei < e.elements.size(); ++ei) {
        const auto &el = e.elements[ei];
        const char *type_name = (el.type == 0) ? "truck" : "drone";

        os << "  [Element " << ei << "] type=" << type_name
           << "(" << el.type << ")"
           << ", vehicle=" << el.vehicle_number
           << ", trips=" << el.trips.size() << "\n";

        for (std::size_t ti = 0; ti < el.trips.size(); ++ti) {
            const auto &tr = el.trips[ti];
            os << "    - Trip " << ti << ": ";

            if (tr.customers.empty()) {
                os << "<empty>\n";
                continue;
            }

            std::set<int> next_nodes;
            for (const auto &[cus, nxt] : tr.customers) {
                (void) cus;
                if (nxt != -1) {
                    next_nodes.insert(nxt);
                }
            }

            std::vector<int> starts;
            starts.reserve(tr.customers.size());
            for (const auto &[cus, nxt] : tr.customers) {
                (void) nxt;
                if (!next_nodes.count(cus)) {
                    starts.push_back(cus);
                }
            }
            if (starts.empty()) {
                starts.push_back(tr.customers.begin()->first);
            }

            std::set<int> visited;
            bool first_chain = true;

            auto print_chain = [&](int start) {
                if (!first_chain) {
                    os << " | ";
                }
                first_chain = false;

                int current = start;
                bool first_node = true;
                while (true) {
                    if (!first_node) {
                        os << "->";
                    }
                    first_node = false;
                    os << current;
                    visited.insert(current);

                    auto it = tr.customers.find(current);
                    if (it == tr.customers.end()) {
                        os << "->?";
                        break;
                    }

                    int nxt = it->second;
                    if (nxt == -1) {
                        os << "->-1";
                        break;
                    }
                    if (visited.count(nxt)) {
                        os << "->" << nxt << "(cycle)";
                        break;
                    }
                    current = nxt;
                }
            };

            for (int start : starts) {
                if (!visited.count(start)) {
                    print_chain(start);
                }
            }

            for (const auto &[cus, nxt] : tr.customers) {
                (void) nxt;
                if (!visited.count(cus)) {
                    print_chain(cus);
                }
            }
            os << "\n";
        }
    }
}

} // namespace common
