#ifndef Instance_h
#define Instance_h

#include <vector>
#include "core/SolverTypes.h"

#include <gmpxx.h>
#include "mpfr/mpreal.h"

namespace GPMC {

template <class T_data>
class Instance {
public:
	Instance();

	void load			(std::istream& in, bool weighted, bool projected);
	bool addClause	(std::vector<Glucose::Lit>& lits, bool learnt=false);

	Glucose::lbool value (Glucose::Var x) const;
	Glucose::lbool value (Glucose::Lit p) const;

	// For Debug
	void toDimacs(std::ostream& out);

	// CNF formula
	int vars;
	std::vector<std::vector<Glucose::Lit>> clauses;
	std::vector<std::vector<Glucose::Lit>> learnts;

	// Counter Mode
	bool weighted;
	bool projected;

	// For WMC/WPMC
	std::vector<T_data> lit_weights;

	// For PMC/WPMC
	int npvars;
	std::vector<bool> ispvars;

	// Instance keeps temporal fixed literals. Preprocessor will eliminate fixed variables.
	std::vector<Glucose::lbool>	assigns;
	std::vector<Glucose::Lit>	assignedLits;

	// additional information
	int freevars;
	T_data gweight;

	// State
	bool unsat;

};

template <class T_data>
inline Glucose::lbool Instance<T_data>::value (Glucose::Var x) const { return assigns[x]; }
template <class T_data>
inline Glucose::lbool Instance<T_data>::value (Glucose::Lit p) const { return assigns[var(p)] ^ sign(p); }

}

#endif
