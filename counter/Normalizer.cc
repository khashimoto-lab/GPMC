#include "counter/Normalizer.h"

#include <chrono>

#include "core/Solver.h"

namespace gpmc {

struct SATSolver : public Glucose::Solver {
    const Glucose::vec<Glucose::lbool>& getAssigns() const { return assigns; }
};

NormalizationResult Normalizer::normalize(CNF& cnf) const {
    NormalizationResult result;
    auto t_start = std::chrono::steady_clock::now();

    result.vars_before    = cnf.num_vars_;
    result.pvars_before   = cnf.num_proj_vars_;
    result.clauses_before = (int)cnf.clauses_.size();

    SATSolver solver;
    for (int i = 0; i < cnf.num_vars_; i++)
        solver.newVar();
    Glucose::vec<Glucose::Lit> lits;
    for (const auto& c : cnf.clauses_) {
        lits.clear();
        for (Lit l : c) lits.push(Glucose::toLit(toInt(l)));
        solver.addClause(lits);
    }
    if (!solver.solve()) {
        result.sat = false;
        return result;
    }

    const Glucose::vec<Glucose::lbool>& assigns = solver.getAssigns();

    bool any_propagated  = solver.nAssigns() > 0;
    result.fixed_vars    = solver.nAssigns();
    std::vector<bool> occurred(cnf.num_vars_, false);
    auto& cls = cnf.clauses_;

    int i2 = 0;
    for (int i = 0; i < (int)cls.size(); i++) {
        auto& c = cls[i];
        bool satisfied = false;
        int j2 = 0;
        for (int j = 0; j < (int)c.size(); j++) {
            Glucose::lbool val = assigns[var(c[j])] ^ sign(c[j]);
            if (val == l_True)  { satisfied = true; break; }
            if (val == l_Undef) { c[j2++] = c[j]; }
        }
        if (!satisfied) {
            c.resize(j2);
            for (auto l : c) occurred[var(l)] = true;
            if (i != i2)
                cls[i2] = std::move(c);
            i2++;
        }
    }
    cls.resize(i2);

    std::vector<Var> map(cnf.num_vars_, -1);
    int new_idx = 0;
    std::vector<Var> nonpvars;

    std::vector<SS> new_weights;
    if (cnf.isWeighted()) {
        new_weights.resize(2 * cnf.num_vars_);

        // Continue the multiplier chain from whatever an earlier pass already
        // folded in (e.g. a gpmc-pp CNF read back via "c MUST MULTIPLY BY").
        result.multiplier = cnf.multiplier_ ? cnf.multiplier_->dup()
                                               : cnf.weightOne();
    } else {
        // Unweighted counterpart: continue the isolated-var count from a
        // read-back "c MUST SHIFT BY".
        result.isolated_pvars = cnf.isolated_vars_;
    }

    for (Var v = 0; v < cnf.num_vars_; v++) {
        if (occurred[v]) {
            if (cnf.is_proj_[v]) {
                map[v] = new_idx;
                if (cnf.isWeighted()) {
                    Lit op = mkLit(v, false), on = mkLit(v, true);
                    Lit np = mkLit(new_idx, false), nn = mkLit(new_idx, true);
                    new_weights[toInt(np)] = cnf.weight(op)->dup();
                    new_weights[toInt(nn)] = cnf.weight(on)->dup();
                }
                new_idx++;
            } else {
                nonpvars.push_back(v);
            }
        } else if (cnf.is_proj_[v]) {
            if (assigns[v] == l_Undef) {

                result.isolated_pvars++;
                if (cnf.isWeighted()) {

                    const SS& wp = cnf.weight(mkLit(v, false));
                    const SS& wn = cnf.weight(mkLit(v, true));
                    SS contrib = wp->add(*wn);
                    result.multiplier->mul_inplace(*contrib);
                }
            } else {

                if (cnf.isWeighted()) {
                    bool neg = (assigns[v] == l_False);
                    const SS& w = cnf.weight(mkLit(v, neg));
                    result.multiplier->mul_inplace(*w);
                }
            }
        }
    }

    cnf.num_proj_vars_ = new_idx;

    for (Var v : nonpvars)
        map[v] = new_idx++;

    cnf.num_vars_ = new_idx;

    if (cnf.isWeighted()) {
        new_weights.resize(2 * cnf.num_vars_);
        cnf.weights_ = std::move(new_weights);
    }

    for (auto& c : cnf.clauses_) {
        for (auto& l : c)
            l = mkLit(map[var(l)], sign(l));
        std::sort(c.begin(), c.end());
    }

    cnf.is_proj_.assign(cnf.num_proj_vars_, true);
    cnf.is_proj_.resize(cnf.num_vars_, false);

    if (any_propagated) {
        int before_dedup = (int)cnf.clauses_.size();
        std::sort(cnf.clauses_.begin(), cnf.clauses_.end());
        cnf.clauses_.erase(
            std::unique(cnf.clauses_.begin(), cnf.clauses_.end()),
            cnf.clauses_.end());
        result.dedup_removed = before_dedup - (int)cnf.clauses_.size();
    }

    result.vars_after    = cnf.num_vars_;
    result.pvars_after   = cnf.num_proj_vars_;
    result.nsvars        = cnf.num_proj_vars_;
    result.ndvars        = cnf.num_proj_vars_;
    result.clauses_after = (int)cnf.clauses_.size();
    auto t_end = std::chrono::steady_clock::now();
    result.elapsed_sec = std::chrono::duration<double>(t_end - t_start).count();

    return result;
}

}
