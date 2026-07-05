// Minimal libgpmc usage: weighted model counting (WMC). See pmc.cc for the
// unweighted counterpart.
//
// Build (from the gpmc-refactor repo root, after ./build.sh r or rs):
//   g++ -std=c++20 -I include samples/wmc.cc \
//       -L lib -lgpmc -lgmpxx -lgmp -o wmc_sample
#include <iostream>

#include <gpmc/CNF.h>
#include <gpmc/Counter.h>
#include <gpmc/semirings/Rational.h>

int main() {
    // vars 0,1, clause (0 v 1), weights w(0)=0.3/0.7, w(1)=0.4/0.6 (pos/neg).
    using gpmc::Rational;

    auto weight = [](mpq_class v) { return std::make_unique<Rational>(std::move(v)); };

    gpmc::CNF cnf;
    cnf.setNumVars(2);
    cnf.setWeightProto(weight(1));  // required before setWeight()
    cnf.addClause({gpmc::mkLit(0, false), gpmc::mkLit(1, false)});

    cnf.setWeight(gpmc::mkLit(0, false), weight({3, 10}));
    cnf.setWeight(gpmc::mkLit(0, true),  weight({7, 10}));
    cnf.setWeight(gpmc::mkLit(1, false), weight({2, 5}));
    cnf.setWeight(gpmc::mkLit(1, true),  weight({3, 5}));

    gpmc::Counter ctr;
    ctr.useSemiring<Rational>();

    ctr.setup(cnf);
    if (!ctr.okay()) {
        std::cout << "UNSATISFIABLE\n";
        return 0;
    }

    gpmc::SS result = ctr.count();
    std::cout << "weighted model count: " << result->to_string() << "\n";  // 29/50
    return 0;
}
