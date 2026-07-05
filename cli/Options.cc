#include "cli/Options.h"

#include <cstdio>
#include <cstdlib>

Options parse_options(CLI::App& app, int argc, char** argv) {
    Options o;

    app.add_option("input", o.filename, "Input CNF file (DIMACS); omit to read stdin");

    auto mode_opt = app.add_option("--mode", o.mode_str,
        "Counting mode: mc, pmc, wmc, pwmc")
       ->check(CLI::IsMember({"mc", "pmc", "wmc", "pwmc"}));
    auto projected_opt = app.add_flag("--projected", o.opt_projected,
        "Projected model counting (PMC/PWMC)");
    auto wtype_opt = app.add_option("--weight-type", o.weight_type,
        "Weight type: none (default), rational, integer")
       ->check(CLI::IsMember({"none", "rational", "integer"}));

    app.add_option("--vs-heuristic", o.vs_mode,
        "Variable selection heuristic: 0=VSADS (weighted sum), 1=VSADS_Lex, "
        "2=VSADS_TD (weighted sum + TD, exp coefficient), 3=VSADS_TD_Lex "
        "(default), 4=VSIDS (activity only), 5=DLCS (frequency only). "
        "0/1/4/5 skip TD construction; 2/3 build a TD score and fall "
        "back to 0/1 when no TD is accepted.")
       ->check(CLI::Range(0, 5));
    app.add_option("--freq-weight", o.w_freq,   "VSADS frequency weight (default 1.0)");
    app.add_option("--act-weight",  o.w_act,    "VSADS activity weight (default 1.0)");
    app.add_option("--td-weight",   o.w_td,     "VSADS_TD tree-decomposition weight (default 1.0)");

    app.add_flag("--td-centroid-all", o.td_centroid_all,
        "Centroid by all-variables mass instead of projection-variable mass");
    app.add_flag("--td-width-guard,!--no-td-width-guard", o.td_width_guard,
        "Reject a too-wide tree decomposition (width >= tw_var_limit*ndvars), "
        "letting the lexicographic order fall through to freq/activity "
        "(default off). By default the wide TD is kept and its centroid is used; "
        "--td-width-guard opts into rejecting it.");
    app.add_option("--td-coef", o.td_coef,
        "TD score base weight (default 100)");
    app.add_option("--td-time", o.td_time,
        "FlowCutter total CPU-time cap in seconds (default 0 = no cap; "
        "with --td-iters also 0, runs greedy heuristics + one FlowCutter pass)");
    app.add_option("--td-iters", o.td_iters,
        "Max FlowCutter passes (default 0 = no cap)")
       ->check(CLI::NonNegativeNumber);

    app.add_option("--cache-mb",   o.cache_mb,  "Component cache size in MB (default 4000)");
    app.add_option("--cache-init-pow2", o.cache_init_pow2,
                   "Initial cache hash table = 2^N buckets (default 20 = 1M)");
    app.add_flag("--cache-unsat", o.cache_unsat,
        "Cache UNSAT (zero) components too (default off)");

    app.add_flag("--inproc-simplify,--no-inproc-simplify{false}", o.inproc_simplify,
        "Fixed-level in-processing simplification in count_main (default: on)");
    app.add_flag("--learn-unsat-comp", o.learn_unsat_comp,
        "PMC/PWMC: learn a clause when a non-projection component is found UNSAT "
        "(internalSAT/countExists floor conflict; default off)");

    app.add_flag("--exists-check", o.use_exists_check,
        "PMC: use exists-check instead of internalSAT for non-projection components");
    app.add_flag("--preprocess,--no-preprocess{false}", o.preprocess,
        "SAT-based preprocessing before counting (default: on)");
    add_preprocessor_options(app, o);

    app.add_option("--digits", o.output_digits,
        "Significant digits for prec-sci output (default 15)")
       ->check(CLI::Range(1, 50));
    app.add_flag("--frac,--no-frac{false}", o.show_frac,
        "Emit the exact fraction for rational results (c s exact arb frac); "
        "can be very long. Integer results are always printed (default: off)");
    app.add_flag("--prec-sci,--no-prec-sci{false}", o.show_prec_sci,
        "Also emit decimal sci-notation (c s exact arb prec-sci) for rational results (default: on)");
    app.add_flag("--log10,--no-log10{false}", o.log10_output,
        "Emit log10-estimate when --mode is used (default: on)");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        std::exit(app.exit(e));
    }

    if (mode_opt->count() > 0 && (projected_opt->count() > 0 || wtype_opt->count() > 0)) {
        std::fprintf(stderr, "Error: --mode cannot be combined with --projected or --weight-type\n");
        std::exit(1);
    }

    return o;
}

void add_preprocessor_options(CLI::App& app, Options& o) {
    app.add_option("--pp-varlim", o.pp_varlim,
        "Above this #vars, use a light pass (1 round, no DVE) (default 1000000)");
    app.add_option("--pp-max-rounds", o.pp_max_rounds,
        "Max pipeline iterations; stops earlier at a var/clause fixed point (default 20)")
       ->check(CLI::Range(1, 1000));
    app.add_option("--pp-time", o.pp_time,
        "CPU-time budget for the iterated preprocessing in seconds "
        "(default 120; 0 = no cap; checked at each round boundary)");
    app.add_option("--pp-rounds", o.pp_rounds,
        "CaDiCaL simplify() rounds; deterministic tick-based budget (default 3)")
       ->check(CLI::Range(1, 1000));
    app.add_option("--pp-solve-conflicts", o.pp_solve_conflicts,
        "Conflict cap on phase 2's solve() from pipeline round 1 on; round 0 "
        "always runs uncapped (default 100000, 0 = uncapped)");
    app.add_flag("--pp-dve,--no-pp-dve{false}", o.pp_dve,
        "Preprocessing phase 3: definable variable elimination (default: on)");
    app.add_option("--dve-min-pvars", o.dve_min_pvars,
        "Skip DVE below this many projection variables (default 10)");
    app.add_option("--dve-low-freq-max", o.dve_low_freq_max,
        "Also admit a DVE candidate whose rarer polarity occurs at most this many "
        "times, regardless of the cheap test (default 4; 0 = off)");
    app.add_flag("--pp-verbose", o.pp_verbose,
        "Print per-phase preprocessing statistics (simplify / solve / DVE), not just "
        "the before/after summary (default: off)");
}
