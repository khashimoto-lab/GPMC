#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include "extern/CLI11/CLI11.hpp"

#include "cli/Options.h"
#include "cli/Report.h"
#include "cli/SignalHandling.h"
#include "cli/Version.hpp"
#include "cli/WeightCheck.h"
#include "utils/Timing.h"
#include <gpmc/CNF.h>
#include <gpmc/Counter.h>
#include <gpmc/Preprocessor.h>
#include <gpmc/semirings/Integer.h>
#include <gpmc/semirings/Rational.h>

// ===== Options -> gpmc config helpers =====
// Translate the raw CLI flags in Options into the config/mode types the
// library actually takes. main() itself starts below.

static gpmc::VarSelectorConfig make_selector_config(const Options& o) {
    gpmc::VarSelectorConfig sel;
    sel.mode         = static_cast<gpmc::VarSelectMode>(o.vs_mode);
    sel.params.w_freq = o.w_freq;
    sel.params.w_act  = o.w_act;
    sel.params.w_td   = o.w_td;

    sel.td.centroid_all_vars = o.td_centroid_all;
    sel.td.width_guard       = o.td_width_guard;
    sel.td.coef              = o.td_coef;
    sel.td.time_limit        = o.td_time;
    sel.td.iters             = o.td_iters;
    return sel;
}

static gpmc::CounterConfig make_config(const Options& o, const ResolvedMode& m) {
    gpmc::CounterConfig cfg;
    cfg.cache_mb              = o.cache_mb;
    cfg.cache_init_pow2       = o.cache_init_pow2;
    cfg.cache_unsat           = o.cache_unsat;
    cfg.inproc_simplify       = o.inproc_simplify;
    cfg.learn_unsat_comp      = o.learn_unsat_comp;
    cfg.use_exists_check      = o.use_exists_check;
    cfg.projected             = m.projected;

    if (m.weight_type == "rational")
        cfg.semiring_factory = [] { return std::make_unique<gpmc::Rational>(); };
    else
        cfg.semiring_factory = [] { return std::make_unique<gpmc::Integer>(); };
    return cfg;
}

using gpmc::cli::Clock;
using gpmc::cli::TimePoint;

// ===== main() =====

int main(int argc, char** argv) {
    TimePoint t_start = Clock::now();

    gpmc::cli::install_signal_handlers();

    // --- CLI parsing ---

    CLI::App app{std::string("gpmc ") + gpmc::VERSION + " (git: " + gpmc::GIT_COMMIT + ") — model counter"};
    app.set_version_flag("--version", std::string(gpmc::VERSION) + " (git: " + gpmc::GIT_COMMIT + ")");

    Options      o   = parse_options(app, argc, argv);
    ResolvedMode m   = resolve_mode(o.opt_projected, o.weight_type, o.mode_str);

    gpmc::Counter ctr;
    ctr.config = make_config(o, m);
    ctr.selector_config = make_selector_config(o);
    ctr.set_stop_flag(&gpmc::cli::g_stop);

    // --- Input ---

    // Prototype value of the counting semiring; in weighted mode it parses the
    // per-literal weight strings in the DIMACS comments.
    bool weighted = (m.weight_type != "none");
    gpmc::SS weight_proto = weighted ? ctr.config.semiring_factory() : nullptr;

    gpmc::CNF cnf;
    bool read_ok;
    if (o.filename.empty()) {
        read_ok = cnf.readDimacs(std::cin, weight_proto.get());
    } else {
        std::ifstream in(o.filename);
        if (!in) { std::fprintf(stderr, "Cannot open: %s\n", o.filename.c_str()); return 1; }
        read_ok = cnf.readDimacs(in, weight_proto.get());
    }
    if (!read_ok) {
        std::fprintf(stderr, "Error: failed to parse DIMACS input\n");
        return 1;
    }

    // completeMccWeights() and preprocess() both key off CNF::isProj(), so an
    // unprojected mode (mc/wmc) needs every variable marked projected before
    // either runs.
    if (!ctr.config.projected)
        cnf.setAllProjected();

    if (m.mcc_weight_complement) {
        if (!cnf.completeMccWeights()) {
            std::fprintf(stderr,
                "Error: one-sided weight out of (0,1); cannot apply MCC complement rule "
                "(supply both polarities explicitly to use a weight outside this range)\n");
            return 1;
        }
    } else if (!gpmc::cli::check_weights_complete(cnf)) {
        // completeMccWeights() always fills both polarities, so this check
        // only matters on the path that skips it. A weighted projection
        // variable missing one polarity entirely is on the caller; catch it
        // here with a message instead of letting Preprocessor/Counter's
        // internal invariant checks crash later.
        std::fprintf(stderr,
            "Error: weighted mode requires both polarity weights on every "
            "projection variable; supply both, or use --mode wmc/pwmc to "
            "complete a one-sided weight via the MCC convention (w, 1-w).\n");
        return 1;
    }

    gpmc::cli::print_input_info(o, m, cnf);

    // --- Preprocessing (optional) + Setup ---
    // "c o [Preprocessing]" only appears when -p/--preprocess is on; "c o
    // [Setup]" (from print_setup_report) always follows, reporting whatever
    // Counter::setup() did (selector build, and normalize/TD when they ran).

    if (o.preprocess) {
        std::printf("\nc o [Preprocessing]\n");
        std::fflush(stdout);

        gpmc::Preprocessor pp(make_preprocessor_config(o));
        auto res = pp.preprocess(cnf);
        gpmc::cli::print_preprocess_result(res, m, weighted);

        ctr.setup(cnf, res);
    } else {
        ctr.setup(cnf);
    }
    std::printf("\nc o [Setup]\n");
    gpmc::cli::print_setup_report(ctr.setupReport(), m);
    std::printf("\ns %s\n", ctr.okay() ? "SATISFIABLE" : "UNSATISFIABLE");

    // --- Counting ---

    std::printf("\nc o [Counting]\n");
    std::fflush(stdout);

    gpmc::cli::g_in_core = 1;
    gpmc::SS result = ctr.count();

    // --- Result ---

    const char* type_str = weighted ? (m.projected ? "pwmc" : "wmc")
                                    : (m.projected ? "pmc"  : "mc");
    ctr.printStats();
    std::printf("\nc o [Result]\n");
    if (ctr.interrupted())
        return gpmc::cli::print_interrupted(type_str, t_start);

    return gpmc::cli::print_result(result, o, type_str, t_start);
}
