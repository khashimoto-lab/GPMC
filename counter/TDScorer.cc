#include "counter/TDScorer.h"

#include <algorithm>
#include <cmath>

#include "td/IFlowCutter.h"
#include "td/PrimalGraph.h"
#include "td/TreeDecomposition.h"
#include "utils/Timing.h"

namespace gpmc {

TDScorerResult TDScorer::compute(const CNF& cnf, int ndvars, bool exp_coef) const {
    TDScorerResult res;
    const int nvars  = cnf.numVars();

    const double t0 = now_sec();
    auto finish = [&](const char* reason) -> TDScorerResult& {
        res.skip_reason = reason;
        res.elapsed_sec = now_sec() - t0;
        return res;
    };

    if (!(nvars > config_.min_vars && nvars <= config_.var_limit))
        return finish("var_limit");

    if (!(ndvars > config_.min_dvars))
        return finish("too_few_dvars");

    PrimalGraph graph(nvars, cnf.clauses());
    res.graph_edges = graph.numEdges();
    const double density = (double)graph.numEdges() / ((long)nvars * nvars);
    const double ratio   = (double)graph.numEdges() / nvars;
    if (!(density <= config_.dense_limit && ratio <= config_.ratio_limit))
        return finish("graph_density");

    IFlowCutterConfig fc_cfg;
    fc_cfg.total_limit   = config_.time_limit;
    fc_cfg.iters         = config_.iters;

    if (ndvars > 0) {
        const double usable_w = config_.tw_var_limit * ndvars;
        fc_cfg.run_min_shortcut = [usable_w](int mindeg_width) {
            return (double)mindeg_width < 4.0 * usable_w;
        };
    }
    IFlowCutter fc(nvars, graph.numEdges(), fc_cfg);
    fc.importGraph(graph);
    graph.clear();
    TreeDecomposition td = fc.constructTD();

    if (td.numNodes() == 0)
        return finish("td_construction_failed");

    res.width = td.width();
    res.bags  = td.numNodes();

    if (config_.width_guard
            && !((double)td.width() / ndvars < config_.tw_var_limit))
        return finish("width_too_large");

    // Lex selectors compare only the ordering, so the raw [0,1] distance score
    // suffices (coef = 1). Sum selectors add the TD term in, so they scale it by
    // a width-driven exponential coefficient.
    double coef = 1.0;
    if (exp_coef) {
        const double rt = (td.width() > 0) ? (double)nvars / td.width() : 41.0;
        coef = (rt > 40) ? 1e7 : std::min(config_.coef * std::exp(rt) / nvars, 1e7);
    }
    res.coef_used = coef;

    const int mass_vars = config_.centroid_all_vars ? td.numGraphNodes() : ndvars;
    res.score.assign(nvars, 0.0);

    // The decomposition may be a forest (one tree per connected component of the
    // primal graph). Score each tree independently: find its own centroid and
    // hand out distance-based scores normalized by that tree's own max distance,
    // so every component is treated on equal footing.
    const int num_bags = td.numNodes();
    std::vector<bool> bag_visited(num_bags, false);
    bool any_scored = false;

    for (int root = 0; root < num_bags; root++) {
        if (bag_visited[root]) continue;

        const int centroid = td.centroid(mass_vars, root);

        auto [dist, max_dist] = td.distanceFromCentroid(centroid);

        // distanceFromCentroid walks exactly this tree's bags; record them as
        // visited so the outer loop skips them as roots of other trees.
        {
            std::vector<int> stack{centroid};
            bag_visited[centroid] = true;
            while (!stack.empty()) {
                int b = stack.back(); stack.pop_back();
                for (int nb : td.neighbors(b))
                    if (!bag_visited[nb]) { bag_visited[nb] = true; stack.push_back(nb); }
            }
        }

        // Single-bag (or zero-radius) trees carry no gradient; skip them.
        if (max_dist == 0)
            continue;

        any_scored = true;

        for (int v = 0; v < nvars; v++) {
            if (dist[v] < 0) continue;
            res.score[v] = coef * (double)(max_dist - dist[v]) / max_dist;
        }
    }

    if (!any_scored)
        return finish("all_same_distance");

    res.computed    = true;
    res.elapsed_sec = now_sec() - t0;
    return res;
}

}
