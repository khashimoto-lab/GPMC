// gpmc-pp: an experimental, researcher-facing standalone preprocessor (off by
// default, see GPMC_BUILD_PP). Its input-handling block (CNF read, projection
// setup, MCC weight completion) intentionally duplicates cli/Main.cc's rather
// than sharing a helper: Main.cc is meant to read top-to-bottom as a worked
// example of the library's counting API, and folding a chunk of that flow
// into a shared function for this secondary tool would obscure that. If you
// change one copy, check whether the other needs the same fix.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "extern/CLI11/CLI11.hpp"

#include "cli/Options.h"
#include "cli/Version.hpp"
#include "cli/WeightCheck.h"
#include <gpmc/CNF.h>
#include <gpmc/Preprocessor.h>
#include <gpmc/semirings/Rational.h>
#include <gpmc/semirings/Integer.h>

namespace {

std::string derive_output_path(const std::string& input) {
    std::string::size_type slash = input.find_last_of("/\\");
    std::string::size_type dot   = input.find_last_of('.');
    std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                       ? input.substr(0, dot) : input;
    return stem + "-pp.txt";
}

}

int main(int argc, char** argv) {
    using gpmc::SS;

    CLI::App app{std::string("gpmc-pp ") + gpmc::VERSION + " (git: " + gpmc::GIT_COMMIT
                 + ") — standalone CNF preprocessor"};
    app.set_version_flag("--version", std::string(gpmc::VERSION) + " (git: " + gpmc::GIT_COMMIT + ")");

    Options o;
    std::string output;
    app.add_option("input", o.filename, "Input CNF file (DIMACS); omit to read stdin");
    app.add_option("-o,--output", output,
        "Output file ('-' = stdout). Default: <input>-pp.txt, or stdout for stdin input");
    app.add_option("--mode", o.mode_str, "mc | pmc | wmc | pwmc");
    auto projected_opt = app.add_flag("--projected", o.opt_projected, "Projected counting (use c p show / vp)");
    auto wtype_opt = app.add_option("--weight-type", o.weight_type, "none | rational | integer");

    add_preprocessor_options(app, o);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    if (!o.mode_str.empty() && (projected_opt->count() > 0 || wtype_opt->count() > 0)) {
        std::fprintf(stderr, "Error: --mode cannot be combined with --projected or --weight-type\n");
        return 1;
    }

    ResolvedMode m = resolve_mode(o.opt_projected, o.weight_type, o.mode_str);

    gpmc::SS weight_proto = nullptr;
    if      (m.weight_type == "rational") weight_proto = std::make_unique<gpmc::Rational>();
    else if (m.weight_type == "integer")  weight_proto = std::make_unique<gpmc::Integer>();
    else if (m.weight_type != "none") {
        std::fprintf(stderr, "Error: unknown --weight-type '%s' (none | rational | integer)\n",
                     m.weight_type.c_str());
        return 1;
    }

    gpmc::CNF cnf;
    bool read_ok;
    if (o.filename.empty()) {
        read_ok = cnf.readDimacs(std::cin, weight_proto.get());
    } else {
        std::ifstream in(o.filename);
        if (!in) { std::fprintf(stderr, "Cannot open: %s\n", o.filename.c_str()); return 1; }
        read_ok = cnf.readDimacs(in, weight_proto.get());
    }
    if (!read_ok) { std::fprintf(stderr, "Error: failed to parse DIMACS input\n"); return 1; }

    // completeMccWeights() and preprocess() both key off CNF::isProj(), so an
    // unprojected mode (mc/wmc) needs every variable marked projected before
    // either runs.
    if (!m.projected)
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
        // here with a message instead of letting Preprocessor's internal
        // invariant check crash later.
        std::fprintf(stderr,
            "Error: weighted mode requires both polarity weights on every "
            "projection variable; supply both, or use --mode wmc/pwmc to "
            "complete a one-sided weight via the MCC convention (w, 1-w).\n");
        return 1;
    }

    std::fprintf(stderr, "c o input    %s\n", o.filename.empty() ? "stdin" : o.filename.c_str());
    std::fprintf(stderr, "c o vars %d  pvars %d  clauses %d\n",
                 cnf.numVars(), cnf.numProjVars(), cnf.numClauses());

    gpmc::PreprocessorConfig pp_cfg = make_preprocessor_config(o);

    bool to_stdout = (output == "-") || (output.empty() && o.filename.empty());
    std::string out_path = to_stdout ? "" :
        (output.empty() ? derive_output_path(o.filename) : output);
    pp_cfg.pp_verbose = to_stdout ? false : o.pp_verbose;

    gpmc::Preprocessor pp(pp_cfg);
    auto res = pp.preprocess(cnf);

    if (!res.sat) {
        std::fprintf(stderr, "c o UNSATISFIABLE (count is 0)\n");
    }
    std::fprintf(stderr, "c o preprocessed: vars %d  pvars %d  clauses %d  (%.3fs)\n",
                 cnf.numVars(), cnf.numProjVars(), cnf.numClauses(), res.elapsed_sec);

    std::string header = std::string("Preprocessed by GPMC ") + gpmc::VERSION
                       + " (git: " + gpmc::GIT_COMMIT + ")";

    std::ofstream fout;
    if (!to_stdout) {
        fout.open(out_path);
        if (!fout) { std::fprintf(stderr, "Cannot write: %s\n", out_path.c_str()); return 1; }
    }
    std::ostream& out = to_stdout ? std::cout : fout;

    if (!res.sat) {
        out << "c " << header << "\n";
        out << "p cnf 0 1\n0\n";
    } else {
        cnf.setMultiplier(std::move(res.multiplier));
        cnf.setIsolatedVars(res.isolated_pvars);
        cnf.writeDimacs(out, header);
    }

    if (!to_stdout)
        std::fprintf(stderr, "c o written  %s\n", out_path.c_str());
    return 0;
}
