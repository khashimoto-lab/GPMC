// Minimal libgpmc usage: projected model counting (PMC). See wmc.cc for the
// weighted counterpart.
//
// Build (from the gpmc-refactor repo root, after ./build.sh r or rs):
//   g++ -std=c++20 -I include samples/pmc.cc \
//       -L lib -lgpmc -lgmpxx -lgmp -o pmc_sample
#include <iostream>

#include <gpmc/CNF.h>
#include <gpmc/Counter.h>

int main() {
    // vars 0,1,2 with 0 <-> 1, projected onto {0,2} (var 1 quantified out).
    gpmc::CNF cnf;
    cnf.setNumVars(3);
    cnf.setProjected(0);
    cnf.setProjected(2);
    cnf.addClause({gpmc::mkLit(0, true),  gpmc::mkLit(1, false)});  // -0 v  1
    cnf.addClause({gpmc::mkLit(0, false), gpmc::mkLit(1, true)});   //  0 v -1

    gpmc::Counter ctr;
    ctr.restrictToProjection(true);  // + default Integer semiring = PMC

    ctr.setup(cnf);
    if (!ctr.okay()) {
        std::cout << "UNSATISFIABLE\n";
        return 0;
    }

    gpmc::SS result = ctr.count();
    std::cout << "projected model count: " << result->to_string() << "\n";  // 4
    return 0;
}
