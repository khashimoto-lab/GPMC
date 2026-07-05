#include "cli/Report.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "cli/SignalHandling.h"
#include <gpmc/semirings/Integer.h>
#include <gpmc/semirings/Rational.h>
#include "utils/Timing.h"

namespace gpmc::cli {

namespace {

using Duration = std::chrono::duration<double>;

// log10 of |result|, via mpf_get_d_2exp (mpz_get_d/mpq_get_d overflow to inf
// on very large numerator/denominator, which log10-estimate must not).
double compute_log10(const gpmc::SS& result) {
    if (!result || result->is_zero())
        return -std::numeric_limits<double>::infinity();
    if (auto* r = dynamic_cast<gpmc::Rational*>(result.get())) {
        const mpq_class& q = r->value();
        mpf_t num_f, den_f;
        mpf_init2(num_f, 128); mpf_init2(den_f, 128);
        mpf_set_z(num_f, q.get_num().get_mpz_t());
        mpf_set_z(den_f, q.get_den().get_mpz_t());
        mpf_div(num_f, num_f, den_f);
        signed long int exp2;
        double mantissa = mpf_get_d_2exp(&exp2, num_f);
        mpf_clear(num_f); mpf_clear(den_f);
        return std::log10(std::abs(mantissa)) + exp2 * std::log10(2.0);
    }
    if (auto* n = dynamic_cast<gpmc::Integer*>(result.get())) {
        mpf_t f; mpf_init2(f, 128);
        mpf_set_z(f, n->value().get_mpz_t());
        signed long int exp2;
        double mantissa = mpf_get_d_2exp(&exp2, f);
        mpf_clear(f);
        return std::log10(std::abs(mantissa)) + exp2 * std::log10(2.0);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// True if result < 0. Only Rational carries negative weights in practice
// (MCC allows negative per-literal weights); Integer results (MC/PMC) are
// never negative. This is a CLI-layer, MCC-format concern (log10-estimate
// vs. neglog10-estimate), so it stays out of the Semiring interface itself.
bool is_negative_result(const gpmc::SS& result) {
    if (auto* r = dynamic_cast<gpmc::Rational*>(result.get()))
        return r->value() < 0;
    return false;
}

} // namespace

void print_input_info(const Options& o, const ResolvedMode& m, const gpmc::CNF& cnf) {
    if (!o.mode_str.empty())
        std::printf("c o %-24s %s\n", "mode",  o.mode_str.c_str());
    std::printf("c o %-24s %s\n", "input", o.filename.empty() ? "stdin" : o.filename.c_str());
    std::printf("c o   %-22s %d\n", "vars",    cnf.numVars());
    if (m.projected)
        std::printf("c o   %-22s %d\n", "pvars",   cnf.numProjVars());
    std::printf("c o   %-22s %d\n", "clauses", cnf.numClauses());
    std::fflush(stdout);
}

void print_preprocess_result(const gpmc::PreprocessingResult& res,
                              const ResolvedMode& m, bool weighted) {
    std::printf("c o   %-22s %d\n",  "vars_after",     res.vars_after);
    if (m.projected)
        std::printf("c o   %-22s %d\n",  "pvars_after",    res.pvars_after);
    std::printf("c o   %-22s %d\n",  "clauses_after",  res.clauses_after);
    std::printf("c o   %-22s %d\n",  "fixed_vars",     res.fixed_vars);
    std::printf("c o   %-22s %d\n",  "isolated_pvars", res.isolated_pvars);
    std::printf("c o   %-22s %d\n",  "pp_rounds",      res.pp_rounds);
    if (weighted) {
        std::string mult = res.multiplier ? res.multiplier->to_string() : "1";
        std::printf("c o   %-22s %s\n",  "multiplier",  mult.c_str());
    }
    std::printf("c o   %-22s %.3f\n","time (sec)",      res.elapsed_sec);
    std::fflush(stdout);
}

void print_setup_report(const gpmc::SetupReport& rep, const ResolvedMode& m) {
    if (!rep.selector_name.empty())
        std::printf("c o   %-22s %s\n", "selector", rep.selector_name.c_str());

    // setup() skips building a selector when the CNF has no variables left
    // (e.g. normalize/preprocessing fixed everything, or the input started
    // empty) -- say so explicitly rather than leaving a bare header.
    if (rep.selector_name.empty() && !rep.normalize && !rep.td)
        std::printf("c o   (no variables left)\n");

    if (rep.normalize) {
        const auto& n = *rep.normalize;
        std::printf("c o   [normalize]\n");
        if (!n.sat) {
            std::printf("c o     %-20s %s\n", "result", "UNSAT");
        } else {
            std::printf("c o     %-20s %d\n",  "vars_after",     n.vars_after);
            if (m.projected)
                std::printf("c o     %-20s %d\n",  "pvars_after",    n.pvars_after);
            std::printf("c o     %-20s %d\n",  "clauses_after",  n.clauses_after);
            std::printf("c o     %-20s %d\n",  "fixed_vars",     n.fixed_vars);
            std::printf("c o     %-20s %d\n",  "isolated_pvars", n.isolated_pvars);
            std::printf("c o     %-20s %d\n",  "dedup_removed",  n.dedup_removed);
            std::printf("c o     %-20s %.3f\n","time (sec)",     n.elapsed_sec);
        }
    }

    if (rep.td) {
        const auto& td = *rep.td;
        std::printf("c o   [td]\n");
        std::printf("c o     %-20s %s\n", "td_used", td.computed ? "yes" : "no");
        if (!td.computed)
            std::printf("c o     %-20s %s\n", "skip_reason", td.skip_reason);
        if (td.width >= 0) {
            std::printf("c o     %-20s %d\n", "graph_edges", td.graph_edges);
            std::printf("c o     %-20s %d\n", "width",       td.width);
            std::printf("c o     %-20s %d\n", "bags",        td.bags);
        }
        if (td.computed && td.coef_used != 0.0)
            std::printf("c o     %-20s %.6g\n", "coef", td.coef_used);
        std::printf("c o     %-20s %.3f\n", "time (sec)", td.elapsed_sec);
    }
    std::fflush(stdout);
}

int print_interrupted(const char* type_str, TimePoint t_start) {
    std::printf("c s type %s\n", type_str);
    double total_sec = Duration(Clock::now() - t_start).count();
    std::printf("c o %-24s %.3f\n", "total_time (sec)",     total_sec);
    std::printf("c o %-24s %.3f\n", "total_cpu_time (sec)", gpmc::cpu_sec());
    std::printf("c o %-24s interrupted by signal %d\n", "status",
                 static_cast<int>(gpmc::cli::g_stop_sig));
    return 1;
}

int print_result(const gpmc::SS& result, const Options& o,
                  const char* type_str, TimePoint t_start) {
    // count() only returns null on interruption, which main() already routes
    // to print_interrupted() instead of here.
    assert(result && "print_result: result must be non-null (see count())");
    std::printf("c s type %s\n", type_str);
    // --frac only gates the Rational fraction, which can be very long; an
    // Integer result (unweighted MC/PMC) is always short, and has no
    // prec-sci-style alternative, so it is always printed.
    bool is_rational = dynamic_cast<gpmc::Rational*>(result.get()) != nullptr;
    if (o.show_frac || !is_rational)
        std::printf("c s exact arb %s %s\n",
            result->notation().c_str(), result->to_string().c_str());
    if (o.show_prec_sci)
        if (auto* r = dynamic_cast<gpmc::Rational*>(result.get()))
            std::printf("c s exact arb prec-sci %s\n",
                r->to_string_prec_sci(o.output_digits).c_str());
    if (o.log10_output && !o.mode_str.empty()) {
        const char* line = is_negative_result(result) ? "neglog10-estimate" : "log10-estimate";
        std::printf("c s %s %.16e\n", line, compute_log10(result));
    }
    double total_sec = Duration(Clock::now() - t_start).count();
    std::printf("c o %-24s %.3f\n", "total_time (sec)",     total_sec);
    std::printf("c o %-24s %.3f\n", "total_cpu_time (sec)", gpmc::cpu_sec());
    std::printf("c o %-24s %s\n",   "status",         "ok");
    return 0;
}

}
