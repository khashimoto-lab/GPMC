#ifndef Counter_h
#define Counter_h

#include "core/Solver.h"
#include "core/Component.h"
#include "core/Config.h"
#include "core/Instance.h"
#include "preprocessor/Preprocessor.h"
#include "ddnnf/DecisionTree.h"

using namespace Glucose;

namespace GPMC {

#define GPMC_VERSION "gpmc-1.1.1-dev/chrono_cdcl"

enum progressT {
	INIT, LOADED, PREPROCESSED, COMPLETED_BYPP, COMPLETED, FAILED
};

enum btStateT {
	EXIT, RESOLVED, GO_TO_NEXT_COMP
};

template <class T_data>
class Counter: public Solver {
public:
	// Constructor/Destructor
	//
	Counter(Configuration& config);
	~Counter() { };

	// Methods
	//
	void load(std::istream& in); 					// load an input instance
	bool preprocess();								// preprocessing for simplification
	bool compact();												// skip preprocessing, only rename vars
	void setExtraVarScore();							// set exscore
	bool countModels();		    					// main count method
	void printStats() const;							// print statistics
	const T_data& getMC() { return npmodels; }	// get the computed count

	// remove literals after the size-th literal
	// do not remove decision literal
	void cancelUntilSize(int size)
	{
		assert (size >= trail_lim.last());

		for(int c = trail.size() - 1; c >= size; c--) {
				Var x = var(trail[c]);
				assigns[x] = l_Undef;
				if(phase_saving > 1 || ((phase_saving == 1) && c > trail_lim.last())) {
						polarity[x] = sign(trail[c]);
				}
				insertVarOrder(x);
		}
		qhead = size;
		trail.shrink(trail.size() - size);
	}

	void writeNNF();
	T_data mcDDNNF();

	int nPVars() const;				// The number of non-isolated projection variables
	int nIsoPVars() const;			// The number of isolated projection variables

	// Data members
	//
	bool sat;
	T_data npmodels;

	// options
	ConfigCounter config;
	ConfigTreeDecomposition tdconfig;

	/// statistics
	uint64_t conflicts_pre, decisions_pre, propagations_pre;
	uint64_t conflicts_sg, decisions_sg, propagations_sg;
	uint64_t sats, reduce_dbs_pre, simp_dbs, max_decs;
	double simplify_time;

protected:
	// Methods
	//
	bool simplify();

	// import a CNF instance
	void import();
	void loadWeight();

	// set extra var scores
	void computeTDScore();		// compute var score from tree decomposition
	void setGivenVarScore();		// set extra var score from file (from ins.score, consistently with ins.gmap)

	/// count main method
	void count_main();

	/// backtrack
	btStateT backtrack(); // int backtrack_level = -1); //, Lit lit = lit_Undef, CRef cr = CRef_Undef);
	void cancelCurDL();
	btStateT Resolve();

	/// SAT solving for the current component
	lbool solveSAT();
	lbool searchBelow(int start_level);

	void analyzeMC(CRef confl, vec<Lit>& out_learnt, vec<Lit> & selectors,
			int& out_btlevel, unsigned int &nblevels,
			unsigned int &szWithoutSelectors);
	unsigned int computeLBDMC(const vec<Lit> & lits, int end);
	unsigned int computeLBDMC(const Clause &c);

	/// Print stats of the component manager.
	void printStatsOfCM() const;

	// Data members
	//
	int npvars;			// boundary number between projection vars and non-projection vars
	int npvars_isolated;	// Number of isolated projection vars

	ComponentManager<T_data> cmpmgr;	// The manger for processing components
	vec<vec<int>> occ_lists;

	vec<T_data> lit_weight;	// literal weight
	T_data gweight;			// global weight

	vec<double> exscore;		// extra score for variable selection

	bool on_simp;				// in-processing simplification on/off

	progressT progress;		// counter progress

	int verbosity_c;			// verbosity level
	bool mc;					// mc or pmc
	bool wc;					// weighted or no-weighted

	PPMC::Preprocessor<T_data> pp;	// preprocessor
	Instance<T_data> ins;			// CNF instance (for preprocessor)
};

template <typename T_data>
inline int Counter<T_data>::nPVars() const {
	return npvars;
}
template <typename T_data>
inline int Counter<T_data>::nIsoPVars() const {
	return npvars_isolated;
}
template <typename T_data>
inline void Counter<T_data>::printStatsOfCM() const {
	cmpmgr.printStats();
}
}

#endif
