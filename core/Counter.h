#ifndef Counter_h
#define Counter_h

#include "core/Solver.h"
#include "core/Component.h"

using namespace Glucose;

namespace GPMC {

class Counter: public Solver {
public:
	// Constructor/Destructor
	//
	Counter();
	~Counter();

	// Methods
	//
	bool simplifyMC();			// Simplification before model counting
	void countModels();			// Main count method

	void registerAsPVar(Var v, bool b);	// Register a projection variable (if b is true)
	int nPVars() const;			// The number of non-isolated projection variables
	int nIsoPVars() const;			// The number of isolated projection variables

	void printStatsOfCM();		// Print stats of the component manager.

	void clearCache() { cmpmgr.cache().deleteallentries(); }
	void initCalcUpperBound(Counter& S);

	void toDimacsRaw();			// For debug: Print the current formula.

	// Data members
	//
	mpz_class npmodels;				// The number of models (The number is set by solveMC())

	int verbosity_c;
	int nbackjumps;
	int nbackjumpstolim;

	int cachesize;

	// options
	int backjumping;
	bool ibcp;
	bool presat;

	bool hasThreshold;
	mpz_class norma;

	bool postprocess;
	bool stopping;

protected:
	enum btStateT {
		EXIT, RESOLVED, GO_TO_NEXT_COMP
	};

	// Methods
	//

	/// Preprocessing
	void CompactClauses(vec<bool>& occurred, int& varnum);			// Removed satisfied clauses and compact clauses after simplification
	void CompactVariables(const vec<bool>& occurred, int varnum);	// Renumber variables after simplification
	bool FailedLiterals();											// Failed literal probing

	void checkedEnqueue(Lit p, CRef from, int level);	// Enqueue a literal if value of literal is undefined.

	btStateT backjump(int backtrack_level, Lit lit = lit_Undef, CRef cr = CRef_Undef, bool flag = false);
	btStateT backtrack(int backtrack_level = -1, Lit lit = lit_Undef, CRef cr = CRef_Undef);

	void cancelCurDL();           // Cancel assignment at current decision level

	lbool solveSAT();				// SAT solving for the current component
	lbool searchBelow(int start_level, int nof_conflicts);
	void analyzeMC(CRef confl, vec<Lit>& out_learnt, vec<Lit> & selectors,
			int& out_btlevel, unsigned int &nblevels,
			unsigned int &szWithoutSelectors, bool &flag);

	unsigned int computeLBDMC(const vec<Lit> & lits, int end);
	unsigned int computeLBDMC(const Clause &c);

	CRef implicitBCP();

	// Data members
	//
	int limlevel;

	int bklevel_final;
	CRef cr_final;
	bool noCurDLlit_final;

	vec<char> ispvar;		// This is used only when preprocessing.
	int npvars;				// boundary number between projection vars and non-projection vars
	int npvars_isolated;	// Number of isolated projection vars

	vec<Lit> unitcls;			// The list of learnt unit clauses
	vec<Lit> punitcls;
	ComponentManager cmpmgr;	// The manger for processing components
	vec<vec<int>> occ_lists;

private:
	// For debug
	void printModelIfGLT(int models);
	void printTrail(int from, int end);

};

// Inline methods
inline void Counter::registerAsPVar(Var v, bool b) {
	if (v >= ispvar.size())
		ispvar.growTo(v + 1, 0);
	ispvar[v] = b;
	npvars += b;
}
inline int Counter::nPVars() const {
	return npvars;
}
inline int Counter::nIsoPVars() const {
	return npvars_isolated;
}

inline void Counter::printStatsOfCM() {
	cmpmgr.printStats();
}

}

#endif
