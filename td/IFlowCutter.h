#pragma once

#include <cstdint>
#include <functional>

#include "extern/flow-cutter-pace17/src/array_id_func.h"
#include "extern/flow-cutter-pace17/src/cell.h"

#include "td/TreeDecomposition.h"

namespace gpmc {

class PrimalGraph;

// FlowCutter knobs, always filled in by TDScorer from TDScorerConfig
// (the real home of these defaults); never used with member defaults.
struct IFlowCutterConfig {
    double  total_limit;
    int     iters;

    std::function<bool(int mindeg_width)> run_min_shortcut;
};

class IFlowCutter {
public:
    IFlowCutter(int n, int m, IFlowCutterConfig cfg);

    void importGraph(const PrimalGraph& g);
    TreeDecomposition constructTD();

private:
    int  compute_max_bag_size_of_order(const ArrayIDIDFunc& order);
    void test_new_order(const ArrayIDIDFunc& order, TreeDecomposition& td);

    TreeDecomposition output_tree_decomposition_of_order(
        ArrayIDIDFunc tail, ArrayIDIDFunc head, const ArrayIDIDFunc& order);
    TreeDecomposition output_tree_decomposition_of_multilevel_partition(
        const ArrayIDIDFunc& to_input_node_id, const std::vector<Cell>& cell_list);

    int nodes;
    int best_bag_size;

    ArrayIDIDFunc head, tail;

    IFlowCutterConfig cfg;
};

}
