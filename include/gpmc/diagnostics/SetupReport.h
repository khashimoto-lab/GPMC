#pragma once

#include <optional>
#include <string>

namespace gpmc {

// Diagnostic-only summary of what Counter::setup() did; the numbers here have
// no bearing on counting itself. Counter fills this in and hands it back so
// the cli layer can decide how (or whether) to print it.
struct SetupReport {

    struct Normalize {
        // false iff the input was found UNSAT before a selector could be built
        // -- either normalize's BCP found a conflict (preprocessing off), or
        // preprocessing itself did (preprocessing on). The fields below are
        // unset in that case.
        bool sat            = true;
        int vars_after     = 0;
        int pvars_after    = 0;
        int clauses_after  = 0;
        int fixed_vars     = 0;
        int isolated_pvars = 0;
        int dedup_removed  = 0;
        double elapsed_sec = 0.0;
    };

    struct TD {
        bool computed         = false;
        const char* skip_reason = nullptr;
        int    graph_edges    = 0;
        int    width          = -1;
        int    bags           = 0;
        double coef_used      = 0.0;
        double elapsed_sec    = 0.0;
    };

    std::string selector_name;

    // Present only when the corresponding step ran (normalize=true / a
    // TD-aware selector), matching what could previously be inferred from
    // which "c o [...]" section appeared.
    std::optional<Normalize> normalize;
    std::optional<TD>        td;
};

}
