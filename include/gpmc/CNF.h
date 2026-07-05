#pragma once
#include <istream>
#include <string>
#include <vector>

#include <gpmc/Semiring.h>
#include <gpmc/Types.h>

namespace gpmc {

class Normalizer;

class CNF {
    friend class Normalizer;
    friend class Preprocessor;

    int num_vars_ = 0;
    int num_proj_vars_ = 0;
    std::vector<bool> is_proj_;

    std::vector<std::vector<Lit>> clauses_;

    // Literal-weight table, indexed by toInt(l) == 2*var + sign.
    // Slot l holds the weight of literal l (null if unset). The table is grown
    // lazily and only ever populated in weighted mode, so an empty weights_ is
    // the canonical signal for "unweighted" (see isWeighted()).
    std::vector<SS> weights_;

    // Prototype value of the counting semiring; its one() seeds unset weights.
    SS weight_proto_;

    // Global model-count multiplier accumulated while preprocessing rewrites the
    // formula (the contribution of variables/clauses that get folded away). The
    // final count must be multiplied by this. Emitted as the "c MUST MULTIPLY BY"
    // comment by writeDimacs and read back by readDimacs, so a preprocessed CNF
    // round-trips through a file. null means "no factor" (== one).
    SS multiplier_;

    // Unweighted counterpart of multiplier_: the number of variables folded away
    // as free (isolated). The final model count must be shifted left by this.
    // Emitted as "c MUST SHIFT BY" and read back by readDimacs. Only meaningful
    // in unweighted mode; weighted mode folds free vars into multiplier_ instead.
    int isolated_vars_ = 0;

public:

    // --- construction ---

    void addClause(std::vector<Lit> lits);
    void setNumVars(int n) { num_vars_ = n; }
    void setAllProjected() {
        num_proj_vars_ = num_vars_;
        is_proj_.assign(num_vars_, true);
    }
    void setProjected(Var v) {
        if ((int)is_proj_.size() <= v) is_proj_.resize(v + 1, false);
        if (!is_proj_[v]) {
            is_proj_[v] = true;
            num_proj_vars_++;
        }
    }
    void setWeight(Lit l, SS w) {
        int i = toInt(l);
        if ((int)weights_.size() <= i) weights_.resize(i + 1);
        weights_[i] = std::move(w);
    }
    void clearWeight(Lit l) {
        int i = toInt(l);
        if (i < (int)weights_.size()) weights_[i] = nullptr;
    }
    void setWeightProto(SS s) { weight_proto_ = std::move(s); }
    void setMultiplier(SS m) { multiplier_ = std::move(m); }
    void setIsolatedVars(int n) { isolated_vars_ = n; }

    // --- I/O ---

    // weight_proto: a prototype value of the counting semiring used to parse
    // per-literal weight strings (proto->parse). nullptr means unweighted, in
    // which case any weight comments are ignored.
    bool readDimacs(std::istream& in,
                    const Semiring* weight_proto = nullptr);

    // Read as weighted input in the algebra T, named the same way as
    // Counter::useSemiring<T>() so a call site reads as one matched pair,
    // e.g. cnf.readDimacsWeighted<Rational>(in); ctr.useSemiring<Rational>();
    template<SemiringType T, typename... Args>
    bool readDimacsWeighted(std::istream& in, Args... args) {
        T proto(args...);
        return readDimacs(in, &proto);
    }

    void writeDimacs(std::ostream& out,
                     const std::string& header_comment = "") const;

    // Complete one-sided / omitted projection-variable weights per the MCC
    // weighted-track rules, and strip any weight off non-projection variables.
    // Call once after the mode is resolved and before preprocessing.
    // Returns false if a one-sided weight is outside (0,1) -- the complement
    // rule assumes a probability-like weight, so MCC requires both polarities
    // to be given explicitly to use e.g. a negative weight.
    bool completeMccWeights();

    // --- queries ---

    int  numVars()     const { return num_vars_; }
    int  numProjVars() const { return num_proj_vars_; }
    int  numClauses()  const { return static_cast<int>(clauses_.size()); }
    bool isProj(Var v) const { return v < (int)is_proj_.size() && is_proj_[v]; }
    const std::vector<std::vector<Lit>>& clauses() const { return clauses_; }

    const SS& weight(Lit l) const;
    bool hasWeight(Lit l) const {
        int i = toInt(l);
        return i < (int)weights_.size() && weights_[i] != nullptr;
    }
    SS weightOne() const;
    bool hasWeightProto() const { return weight_proto_ != nullptr; }
    const SS& multiplier() const { return multiplier_; }
    int  isolatedVars() const { return isolated_vars_; }

    // Weighted iff some literal carries a weight, or a folded-in multiplier is
    // present. The latter survives even after preprocessing has eliminated every
    // weighted variable (e.g. a gpmc-pp CNF read back via "c MUST MULTIPLY BY"),
    // so it must count as weighted on its own. See the weights_ contract above.
    bool isWeighted() const { return !weights_.empty() || multiplier_ != nullptr; }
};

}
