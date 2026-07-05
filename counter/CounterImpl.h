#pragma once
#include <chrono>
#include <csignal>

#include "core/Solver.h"

#include "include/gpmc/diagnostics/SetupReport.h"
#include "counter/VarSelector.h"
#include "include/gpmc/CNF.h"
#include "include/gpmc/Semiring.h"
#include "include/gpmc/config/CounterConfig.h"
#include "include/gpmc/config/VarSelectorConfig.h"

namespace gpmc {

struct PreprocessingResult;

class ComponentManager;

// The actual counting engine. Counter (include/gpmc/Counter.h) is a thin
// public facade that owns one of these behind a unique_ptr; every public
// Counter method just forwards here. Kept out of the public headers because
// it inherits Glucose::Solver and pulls in Glucose's internal headers.
class CounterImpl : public Glucose::Solver {
public:
    CounterImpl();
    ~CounterImpl() override;

    // Build the branching strategy from selector_config and run setup.
    void setup(CNF& cnf, bool normalize = true);
    void setup(CNF& cnf, const struct PreprocessingResult& prep_result);

    // Number of solutions, or weighted sum of solutions. Returns null if the
    // search was interrupted (check interrupted() after calling).
    SS   count();
    void printStats() const;

    // Diagnostic summary of what the last setup() call did (normalize/TD
    // sub-steps run only conditionally). The cli layer owns printing it.
    const SetupReport& setupReport() const { return setup_report_; }

    // Interruption from outside (e.g. a signal handler). Defined out-of-line
    // because it also forwards the flag into cmpmgr_ (incomplete type here).
    void set_stop_flag(volatile std::sig_atomic_t* flag);
    bool interrupted() const { return interrupted_; }

    CounterConfig      config;
    VarSelectorConfig  selector_config;

private:
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;

    // External interruption request (e.g. SIGINT/timeout).
    volatile std::sig_atomic_t* stop_        = nullptr;
    bool                        interrupted_ = false;

    // setup() and count() may each only be called once per instance -- count()
    // leaves learnt clauses/activity in the inherited Glucose::Solver state,
    // and setup() builds on top of that state rather than resetting it.
    // Construct a new Counter to set up and count another CNF.
    bool   setup_done_       = false;
    bool   counted_          = false;

    // count() can skip the search (UNSAT or no variables left).
    bool   trivial_          = false;

    // Counting mode.
    bool   is_weighted_      = false;
    bool   is_projected_     = false;

    // # of variables.
    int    nsvars_           = 0;
    int    ndvars_           = 0;
    int    npvars_isolated_  = 0;

    // Counting algebra: zero/one factory, running weight product, and
    // per-literal weights.
    SS                  prototype_;
    SS                  multiplier_;
    std::vector<SS>     lit_weight_;

    // Learnt units pending re-enqueue.
    Glucose::vec<Glucose::Lit> unitcls_;

    // Component splitting/cache, and decision-variable strategy.
    std::unique_ptr<ComponentManager> cmpmgr_;
    std::unique_ptr<VarSelector>      selector_;

    // Filled in by setup(); see setupReport().
    SetupReport setup_report_;

    // PMC/PWMC: whether a non-projection component is SAT at all.
    int    exists_pre_dl_ = 0;
    bool   exists_found_  = false;

    // Run statistics.
    int      nrmvsat_        = 0;
    uint64_t nsatchecks_     = 0;
    double   count_time_sec_ = 0.0;

    // Core setup, with the branching strategy already built. The public setup()
    // overloads build it from selector_config and delegate here.
    void setup(CNF& cnf, std::unique_ptr<VarSelector> selector, bool normalize = true);
    void setup(CNF& cnf, std::unique_ptr<VarSelector> selector,
               const struct PreprocessingResult& prep_result);

    // Called from setup().
    void reserveVars(int n);
    void loadClauses(const CNF& cnf);
    void initSemiring(CNF& cnf);

    // Runs the selector's static (formula-only) precomputation and records
    // its name/TD summary into setup_report_. Called at the end of both
    // setup() overloads, once selector_ is in place.
    void prepareSelector(const CNF& cnf);

    // Apply the isolated-var shift / folded weight factor to a raw count.
    SS applyGlobalFactors(SS count) const;

    // Main search, called from count().
    void count_main();
    bool simplify();
    void analyzeMC(Glucose::CRef confl, Glucose::vec<Glucose::Lit>& out_learnt,
                   int& out_btlevel, unsigned int& lbd,
                   unsigned int& szWithoutSelectors);
    Glucose::lbool internalSAT();

    enum class BtState { EXIT, RESOLVED, GO_NEXT_COMP };
    BtState backtrack();
    void    cancelCurDL();

    // PMC/PWMC: alternative to internalSAT() for non-projection components.
    Glucose::lbool countExists();
    Var            selectExists(const Component& comp) const;
    BtState        backtrackExists();
};

}
