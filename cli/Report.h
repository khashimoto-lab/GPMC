#pragma once

#include <chrono>

#include "cli/Options.h"
#include <gpmc/CNF.h>
#include <gpmc/Counter.h>
#include <gpmc/Preprocessor.h>

namespace gpmc::cli {

using Clock     = std::chrono::steady_clock;
using TimePoint  = Clock::time_point;

// "c o [...]" / "c s [...]" line printers for main()'s read -> preprocess ->
// setup -> count -> result pipeline. Each corresponds to one stage; see
// cli/Main.cc for call order.

void print_input_info(const Options& o, const ResolvedMode& m, const gpmc::CNF& cnf);

void print_preprocess_result(const gpmc::PreprocessingResult& res,
                              const ResolvedMode& m, bool weighted);

void print_setup_report(const gpmc::SetupReport& rep, const ResolvedMode& m);

int print_interrupted(const char* type_str, TimePoint t_start);

// Prints the "c s"/"c o" result lines. count() only returns null on
// interruption, which main() routes to print_interrupted() instead, so
// result must be non-null here.
int print_result(const gpmc::SS& result, const Options& o,
                  const char* type_str, TimePoint t_start);

}
