#pragma once

#include <string>

#include "extern/CLI11/CLI11.hpp"

#include <gpmc/Preprocessor.h>

// Raw CLI arguments, one field per flag/option. See cli/Options.cc for the
// CLI11 definitions (help text, defaults, validation) of each field.
struct Options {
    std::string filename;
    std::string mode_str;
    std::string weight_type  = "none";
    bool        show_frac     = false;
    bool        show_prec_sci = true;
    bool        opt_projected = false;
    bool        use_exists_check = false;
    bool        preprocess    = true;
    bool        log10_output = true;
    int         vs_mode      = 3;
    int         output_digits = 15;
    size_t      cache_mb     = 4000;
    int         cache_init_pow2 = 20;
    bool        cache_unsat  = false;
    bool        inproc_simplify  = true;
    bool        learn_unsat_comp = false;
    double      w_freq       = 1.0;
    double      w_act        = 1.0;
    double      w_td         = 1.0;
    int         pp_varlim    = 1000000;
    int         pp_rounds    = 3;
    int         pp_max_rounds = 20;
    double      pp_time      = 120.0;
    int         pp_solve_conflicts = 100000;
    bool        pp_dve       = true;
    int         dve_min_pvars = 10;
    int         dve_low_freq_max = 4;
    bool        pp_verbose   = false;
    bool        td_centroid_all = false;
    bool        td_width_guard  = false;
    double      td_coef      = 100.0;
    double      td_time      = 0.0;
    int         td_iters     = 0;
};

// Registers all gpmc CLI flags/options on app, parses argv, and returns the
// resulting Options. Exits the process on a parse error or on the
// --mode/--projected/--weight-type mutual-exclusion check.
Options parse_options(CLI::App& app, int argc, char** argv);

// Registers the 9 preprocessing-pipeline flags (--pp-varlim..--pp-verbose),
// shared by gpmc (via parse_options) and gpmc-pp (cli/PreprocMain.cc) so
// their help text/defaults/constraints can't drift apart between the two
// binaries. Does not call app.parse(); the caller does that itself.
void add_preprocessor_options(CLI::App& app, Options& o);

// --mode/--projected/--weight-type resolved into the three knobs the rest of
// main() actually needs.
struct ResolvedMode {
    bool        projected;
    std::string weight_type;
    bool        mcc_weight_complement;
};

// --mode, when set, overrides --projected/--weight-type with one of the four
// MC Competition presets (mc/pmc/wmc/pwmc); otherwise passes them through.
// Shared by cli/Main.cc and cli/PreprocMain.cc, which keep separate Options
// structs but resolve mode identically.
inline ResolvedMode resolve_mode(bool opt_projected, const std::string& weight_type,
                                  const std::string& mode_str) {
    ResolvedMode m{ opt_projected, weight_type, false };
    if      (mode_str == "mc")   { m.projected = false; m.weight_type = "none"; }
    else if (mode_str == "pmc")  { m.projected = true;  m.weight_type = "none"; }
    else if (mode_str == "wmc")  { m.projected = false; m.weight_type = "rational"; m.mcc_weight_complement = true; }
    else if (mode_str == "pwmc") { m.projected = true;  m.weight_type = "rational"; m.mcc_weight_complement = true; }
    return m;
}

// Translates Options' pp_*/dve_* fields into PreprocessorConfig. Shared by
// cli/Main.cc and cli/PreprocMain.cc, which both drive gpmc::Preprocessor
// with the same knobs.
inline gpmc::PreprocessorConfig make_preprocessor_config(const Options& o) {
    gpmc::PreprocessorConfig cfg;
    cfg.var_limit             = o.pp_varlim;
    cfg.rounds                = o.pp_rounds;
    cfg.max_rounds            = o.pp_max_rounds;
    cfg.pp_time_limit         = o.pp_time;
    cfg.solve_conflict_limit  = o.pp_solve_conflicts;
    cfg.pp_dve                = o.pp_dve;
    cfg.dve_min_pvars         = o.dve_min_pvars;
    cfg.dve_low_freq_max      = o.dve_low_freq_max;
    cfg.pp_verbose            = o.pp_verbose;
    return cfg;
}
