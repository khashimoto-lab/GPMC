#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace gpmc {

class TreeDecomposition {
public:
    void init(int num_bags) {
        adj_.assign(num_bags, {});
        bags_.assign(num_bags, {});
    }

    void addEdge(int b1, int b2) {
        adj_[b1].push_back(b2);
        adj_[b2].push_back(b1);
    }

    int numNodes() const { return (int)adj_.size(); }
    const std::vector<int>& neighbors(int b) const { return adj_[b]; }

    std::vector<std::vector<int>>& bags() { return bags_; }

    void setWidth(int w) { width_ = w; }
    int  width() const   { return width_; }

    void setNumGraphNodes(int n) { graph_nodes_ = n; }
    int  numGraphNodes() const   { return graph_nodes_; }

    // Centroid bag of the tree containing `root` (a forest may hold several
    // trees; pass the root of the one you want). mass_vars bounds which graph
    // nodes count toward subtree mass.
    int centroid(int mass_vars, int root = 0) const;

    // Returns (distance, max_dist): distance[x] counts prior bags that
    // introduced a new variable, not including x's own bag. Scoring only.
    std::pair<std::vector<int>, int> distanceFromCentroid(int centroid_bag) const;

private:
    bool inBag(int b, int x) const {
        return std::binary_search(bags_[b].begin(), bags_[b].end(), x);
    }

    std::vector<std::vector<int>> adj_;
    std::vector<std::vector<int>> bags_;
    int width_       = 0;
    int graph_nodes_ = 0;
};

}
