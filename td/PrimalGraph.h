#pragma once

#include <vector>

#include "include/gpmc/Types.h"
#include "utils/Bitset.h"

namespace gpmc {

class PrimalGraph {
public:
    PrimalGraph(int num_vars, const std::vector<std::vector<Lit>>& clauses);

    void clear();

    int numEdges() const { return edges_; }
    const std::vector<int>& neighbors(int v) const { return adj_list_[v]; }

private:
    void init(int n);
    void addEdge(int v1, int v2);

    int edges_;
    std::vector<std::vector<int>> adj_list_;
    std::vector<Bitset>           adj_mat_;
};

}
