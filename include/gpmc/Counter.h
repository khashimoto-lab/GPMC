#pragma once
#include <csignal>
#include <functional>
#include <memory>
#include <utility>

#include <gpmc/diagnostics/SetupReport.h>
#include <gpmc/config/CounterConfig.h>
#include <gpmc/config/VarSelectorConfig.h>
#include <gpmc/Semiring.h>

namespace gpmc {

struct PreprocessingResult;
class  CNF;
class  CounterImpl;

// Public counting API. A thin facade over CounterImpl (counter/CounterImpl.h,
// hidden): every method here just forwards. Kept thin so this header does not
// pull in Glucose's internal headers or the VarSelector hierarchy.
class Counter {
public:
    Counter();
    ~Counter();

    // Which algebra to count in, named by type: useSemiring<Integer>(),
    // useSemiring<Rational>(). Args are forwarded to T's constructor each time
    // setup() needs a fresh prototype value (Semiring is move-only, so a
    // factory has to be stored rather than a single shared instance).
    template<SemiringType T, typename... Args>
    void useSemiring(Args... args) {
        config.semiring_factory = [args...]() { return std::make_unique<T>(args...); };
    }

    // Escape hatch for callers that already have a factory function/lambda in
    // hand (e.g. one built at runtime) instead of a fixed type T.
    void useSemiring(std::function<SS()> factory) { config.semiring_factory = std::move(factory); }

    // Count only over the CNF's projection set (CNF::setProjected(Var), a
    // different method on a different class) instead of every variable.
    // Combined with useSemiring(), this is what picks PMC vs MC vs PWMC vs
    // WMC -- there is no separate mode enum, just this flag plus the algebra.
    void restrictToProjection(bool enable) { config.projected = enable; }

    // Build the branching strategy from selector_config and run setup.
    // setup() and count() may each only be called once per Counter (asserts
    // otherwise in debug builds) -- construct a new Counter to set up and
    // count another CNF.
    void setup(CNF& cnf, bool normalize = true);
    void setup(CNF& cnf, const struct PreprocessingResult& prep_result);

    // Number of solutions, or weighted sum of solutions. Returns null if the
    // search was interrupted (check interrupted() after calling).
    SS   count();
    void printStats() const;

    // Diagnostic summary of what the last setup() call did (normalize/TD
    // sub-steps run only conditionally). The cli layer owns printing it.
    const SetupReport& setupReport() const;

    // Interruption from outside (e.g. a signal handler).
    void set_stop_flag(volatile std::sig_atomic_t* flag);
    bool interrupted() const;

    // FALSE means setup() already found the instance UNSAT; no further
    // counting should be attempted.
    bool okay() const;

    CounterConfig      config;
    VarSelectorConfig  selector_config;

private:
    std::unique_ptr<CounterImpl> impl_;
};

}
