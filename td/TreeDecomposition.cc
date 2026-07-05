#include "td/TreeDecomposition.h"

namespace gpmc {

// Post-order traversal via an explicit stack (avoids recursion depth issues
// on deep trees); returns the first node whose subtree mass reaches half.
int TreeDecomposition::centroid(int mass_vars, int root) const {
    if (numNodes() == 0) return -1;
    const long threshold = mass_vars / 2;

    struct Frame {
        int  v;
        int  parent;
        size_t next_child;
        long subtree_mass;
    };
    std::vector<Frame> stack;
    stack.push_back({root, -1, 0, 0});

    while (!stack.empty()) {
        Frame& f = stack.back();

        if (f.next_child < adj_[f.v].size()) {
            int ch = adj_[f.v][f.next_child++];
            if (ch != f.parent)
                stack.push_back({ch, f.v, 0, 0});
            continue;
        }

        long mass = f.subtree_mass;
        for (int x : bags_[f.v])
            if (x < mass_vars && (f.parent == -1 || !inBag(f.parent, x)))
                mass++;

        if (mass >= threshold)
            return f.v;

        stack.pop_back();
        if (!stack.empty())
            stack.back().subtree_mass += mass;
    }
    return root;
}

// Pre-order traversal via an explicit stack; depth only advances at bags
// that introduce at least one previously-unseen variable.
std::pair<std::vector<int>, int> TreeDecomposition::distanceFromCentroid(int centroid_bag) const {
    std::vector<int> distance(graph_nodes_, -1);
    int max_dist = 0;
    if (numNodes() == 0 || centroid_bag < 0) return {distance, max_dist};

    struct Item { int v; int parent; int depth; };
    std::vector<Item> stack;
    stack.push_back({centroid_bag, -1, 0});

    while (!stack.empty()) {
        Item it = stack.back();
        stack.pop_back();

        bool introduces_new = false;
        for (int x : bags_[it.v]) {
            if (distance[x] == -1) {
                distance[x] = it.depth;
                introduces_new = true;
                max_dist = std::max(max_dist, it.depth);
            }
        }
        int child_depth = introduces_new ? it.depth + 1 : it.depth;

        for (int ch : adj_[it.v])
            if (ch != it.parent)
                stack.push_back({ch, it.v, child_depth});
    }
    return {distance, max_dist};
}

}
