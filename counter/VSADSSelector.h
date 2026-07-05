#pragma once
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "extern/glucose/mtl/Vec.h"

#include "counter/Component.h"
#include "include/gpmc/diagnostics/SetupReport.h"
#include "counter/TDScorer.h"
#include "counter/VarSelector.h"
#include "counter/VarSelectorArgs.h"

namespace gpmc {

// Run a tree-decomposition pass over the formula, returning the per-variable
// score plus a diagnostic summary. Shared by the two TD-aware selectors so
// their prepareStatic() stays a one-liner. exp_coef is the only knob that
// differs between them: sum selectors pass true, lex false.
inline std::pair<std::vector<double>, SetupReport::TD>
buildTDScore(const TDScorerConfig& td_cfg, const CNF& cnf, int ndvars, bool exp_coef) {
    TDScorer scorer(td_cfg);
    TDScorerResult res = scorer.compute(cnf, ndvars, exp_coef);

    SetupReport::TD report;
    report.computed    = res.computed;
    report.skip_reason = res.skip_reason;
    report.graph_edges = res.graph_edges;
    report.width       = res.width;
    report.bags        = res.bags;
    report.coef_used   = (res.computed && exp_coef) ? res.coef_used : 0.0;
    report.elapsed_sec = res.elapsed_sec;

    std::vector<double> score(cnf.numVars(), 0.0);
    if (res.computed)
        score = std::move(res.score);

    return {std::move(score), report};
}

// VSADS: score = w_freq * frequency + w_act * (activity / var_inc).
// Picks the variable with the highest weighted sum. Also serves VSIDS
// (w_freq = 0) and DLCS (w_act = 0) by zeroing one weight.
class VSADSSelector final : public VarSelector {
    const std::vector<int>*     frequency_ = nullptr;
    const Glucose::vec<double>* activity_  = nullptr;
    const double*               var_inc_   = nullptr;
    double w_freq_;
    double w_act_;

public:
    VSADSSelector(double w_freq, double w_act)
        : w_freq_(w_freq), w_act_(w_act) {}

    const char* name() const override {
        if (w_freq_ == 0.0) return "vsids";
        if (w_act_  == 0.0) return "dlcs";
        return "vsads";
    }

    void bind(const VarSelectorArgs& dyn) override {
        frequency_ = &dyn.frequency;
        activity_  = &dyn.activity;
        var_inc_   = &dyn.var_inc;
    }

    double computeScore(Var v) const override {
        const auto& freq = *frequency_;
        const auto& act  = *activity_;
        return w_freq_ * freq[v]
             + w_act_  * act[v] / *var_inc_;
    }
};

// VSADS_TD: VSADS plus a tree-decomposition term, all summed with weights:
// score = w_freq * frequency + w_act * (activity / var_inc) + w_td * tdscore.
// exp_coef scales the TD term by a width-driven exponential coefficient.
class VSADSTDSelector final : public VarSelector {
    const std::vector<int>*     frequency_ = nullptr;
    const Glucose::vec<double>* activity_  = nullptr;
    const double*               var_inc_   = nullptr;
    std::vector<double> tdscore_;
    SetupReport::TD td_report_;
    TDScorerConfig td_cfg_;
    double w_freq_;
    double w_act_;
    double w_td_;

public:
    VSADSTDSelector(TDScorerConfig td_cfg,
                    double w_freq,
                    double w_act,
                    double w_td)
        : td_cfg_(td_cfg)
        , w_freq_(w_freq)
        , w_act_(w_act)
        , w_td_(w_td)
    {}

    const char* name() const override { return "vsads_td"; }

    void prepareStatic(const CNF& cnf, int ndvars) override {
        std::tie(tdscore_, td_report_) = buildTDScore(td_cfg_, cnf, ndvars, /*exp_coef=*/true);
    }

    void bind(const VarSelectorArgs& dyn) override {
        frequency_ = &dyn.frequency;
        activity_  = &dyn.activity;
        var_inc_   = &dyn.var_inc;
    }

    double computeScore(Var v) const override {
        const auto& freq = *frequency_;
        const auto& act  = *activity_;
        return w_freq_ * freq[v]
             + w_act_  * act[v] / *var_inc_
             + w_td_   * tdscore_[v];
    }

    std::optional<SetupReport::TD> tdReport() const override { return td_report_; }
};

// VSADS_Lex: strict 2-level lexicographic order, frequency > activity.
// activity only breaks ties when frequency is exactly equal.
class VSADSLexSelector final : public VarSelector {
    const std::vector<int>*     frequency_ = nullptr;
    const Glucose::vec<double>* activity_  = nullptr;

public:
    VSADSLexSelector() = default;

    const char* name() const override { return "vsads_lex"; }

    void bind(const VarSelectorArgs& dyn) override {
        frequency_ = &dyn.frequency;
        activity_  = &dyn.activity;
    }

    Var select(const Component& comp) const override {
        const auto& frequency = *frequency_;
        const auto& activity  = *activity_;
        Var    best   = VAR_UNDEF;
        double best_f = -1.0;
        double best_a = -1.0;
        for (int i = 0; i < comp.nDVarsInComp(); i++) {
            Var    v = comp[i];
            double f = frequency[v];
            double a = activity[v];
            if (f > best_f) {
                best_f = f; best_a = a; best = v;
            } else if (f == best_f && a > best_a) {
                best_a = a; best = v;
            }
        }
        return best;
    }
};

// VSADS_TD_Lex: strict 3-level lexicographic order, tdscore > frequency >
// activity. Matches the GPMC v1.1.1 default branching order. The TD score has
// top priority so component decomposition is preserved on TD-friendly instances.
class VSADSTDLexSelector final : public VarSelector {
    const std::vector<int>*     frequency_ = nullptr;
    const Glucose::vec<double>* activity_  = nullptr;
    std::vector<double> tdscore_;
    SetupReport::TD td_report_;
    TDScorerConfig td_cfg_;

public:
    explicit VSADSTDLexSelector(TDScorerConfig td_cfg)
        : td_cfg_(std::move(td_cfg)) {}

    const char* name() const override { return "vsads_td_lex"; }

    void prepareStatic(const CNF& cnf, int ndvars) override {
        std::tie(tdscore_, td_report_) = buildTDScore(td_cfg_, cnf, ndvars, /*exp_coef=*/false);
    }

    void bind(const VarSelectorArgs& dyn) override {
        frequency_ = &dyn.frequency;
        activity_  = &dyn.activity;
    }

    Var select(const Component& comp) const override {
        const auto& frequency = *frequency_;
        const auto& activity  = *activity_;
        Var    best    = VAR_UNDEF;
        double best_td = -1.0;
        double best_f  = -1.0;
        double best_a  = -1.0;
        for (int i = 0; i < comp.nDVarsInComp(); i++) {
            Var    v  = comp[i];
            double td = tdscore_[v];
            double f  = frequency[v];
            double a  = activity[v];
            if (td > best_td) {
                best_td = td; best_f = f; best_a = a; best = v;
            } else if (td == best_td) {
                if (f > best_f) {
                    best_f = f; best_a = a; best = v;
                } else if (f == best_f && a > best_a) {
                    best_a = a; best = v;
                }
            }
        }
        return best;
    }

    std::optional<SetupReport::TD> tdReport() const override { return td_report_; }
};

}
