#include "td/PrimalGraph.h"

namespace gpmc {

PrimalGraph::PrimalGraph(int num_vars, const std::vector<std::vector<Lit>>& clauses) {
    clear();
    init(num_vars);
    for (const auto& cl : clauses)
        for (int i = 0; i < (int)cl.size(); i++)
            for (int j = i + 1; j < (int)cl.size(); j++)
                addEdge(var(cl[i]), var(cl[j]));
}

void PrimalGraph::init(int n) {
    adj_list_.assign(n, {});
    adj_mat_.clear();
    adj_mat_.reserve(n);
    for (int i = 0; i < n; i++) {
        adj_mat_.emplace_back(n);
    }
}

void PrimalGraph::clear() {
    edges_ = 0;
    adj_list_.clear();
    adj_mat_.clear();
}

void PrimalGraph::addEdge(int v1, int v2) {
    if (adj_mat_[v1].Get(v2)) return;
    adj_list_[v1].push_back(v2);
    adj_list_[v2].push_back(v1);
    adj_mat_[v1].SetTrue(v2);
    adj_mat_[v2].SetTrue(v1);
    edges_++;
}

}
