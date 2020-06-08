#include <gmpxx.h>
#include "core/Counter.h"
#include "mtl/Sort.h"

using namespace Glucose;
using namespace GPMC;

#define D_ASSIGN_BY_LONGLEARNTCL

//=================================================================================================
// Options:

static const char* _mc = "GPMC -- COUNTER";

static IntOption  opt_backjumping (_mc, "bj", "Controls backjumping (0=off, 1=limited, 2=full)", 0, IntRange(0,2));
static BoolOption opt_ibcp        (_mc, "ibcp", "Use implict BCP", false);
static BoolOption opt_presat      (_mc, "presat", "SAT solving as preprocessing", true);

// static const int NUM_PICKED_PCLS = 50;
// static const int SIZE_PICKED_PCL = 10;

//=================================================================================================
// Constructor/Destructor:

Counter::Counter() :
  verbosity_c        (0)
, nbackjumps         (0)
, nbackjumpstolim    (0)
, backjumping        (opt_backjumping)
, ibcp               (opt_ibcp)
, presat             (opt_presat)
//, postprocess        (false)
//, stopping           (false)
, limlevel           (0)
, bklevel_final      (0)
, npvars             (0)
, npvars_isolated    (0)
{
}

Counter::~Counter()
{
}

//=================================================================================================
// Methods for preprocessing

bool Counter::simplifyMC()
{
	// This must be executed only once before model counting.

	assert(decisionLevel() == 0);

	// failed literal check
	if(!FailedLiterals()) return false;

	double init_var_decay = var_decay;
	double init_clause_decay = clause_decay;

	// SAT solving as preprocessing
	if (presat && solve_() == l_False) return false;

	// Simplify the clause database according to the current assignment
	// and compact clauses and variables
	int varnum;
	vec<bool> occurred;
	CompactClauses(occurred,varnum);
	CompactVariables(occurred,varnum);

	simpDB_assigns = nAssigns();
	simpDB_props   = clauses_literals + learnts_literals;

	var_inc =1;
	cla_inc =1;
	var_decay    = init_var_decay;
	clause_decay = init_clause_decay;
	curRestart = 1;
	nbclausesbeforereduce = firstReduceDB;

	return true;
}

bool Counter::FailedLiterals() {
	int last_size;
	do {
		last_size = trail.size();

		for (Var v = 0; v < nVars(); v++)
			if (value(v) == l_Undef) {
				int sz = trail.size();
				uncheckedEnqueue(mkLit(v, true));
				CRef confl = propagate();

				for(int c = trail.size()-1; c >= sz; c--){
					Var x = var(trail[c]);
					assigns[x] = l_Undef;
				}
				qhead = sz;
				trail.shrink(trail.size() - sz);

				if(confl != CRef_Undef){
					sz = trail.size();
					uncheckedEnqueue(mkLit(v, false));
					confl = propagate();
					if(confl != CRef_Undef) return false;
				} else {
					sz = trail.size();
					uncheckedEnqueue(mkLit(v, false));
					confl = propagate();

					for(int c = trail.size()-1; c >= sz; c--){
						Var x = var(trail[c]);
						assigns[x] = l_Undef;
					}
					qhead = sz;
					trail.shrink(trail.size() - sz);

					if(confl != CRef_Undef){
						uncheckedEnqueue(mkLit(v, true));
						confl = propagate();
						if(confl != CRef_Undef) return false;
					}
				}
			}

	} while (trail.size() > last_size);

	return true;
}

void Counter::CompactClauses(vec<bool>& occurred, int& varnum)
{
	int i1, i2;
	int j1, j2;

	varnum = 0;
	occurred.clear();
	occurred.growTo(nVars(),false);

	for(i1 = 0, i2 = 0; i1 < clauses.size(); i1++)	{
		Clause& c = ca[clauses[i1]];
		for(j1 = 0, j2 = 0; j1 < c.size(); j1++) {
			if(value(c[j1]) == l_Undef)
				c[j2++] = c[j1];
			else if(value(c[j1]) == l_True) {
				removeClause(clauses[i1]);
				goto NEXTC;
			}
		}
		c.shrink(j1-j2);

		for(j1=0; j1 < c.size(); j1++) {
			Var v = var(c[j1]);
			if (!occurred[v]){
				occurred[v] = true;
				varnum++;
			}
		}

		clauses[i2++] = clauses[i1];
		NEXTC:;
	}
	clauses.shrink(i1-i2);

	for(i1 = 0, i2 = 0; i1 < learnts.size(); i1++)	{
		Clause& c = ca[learnts[i1]];
		for(j1 = 0, j2 = 0; j1 < c.size(); j1++) {
			if(value(c[j1]) == l_Undef)
				c[j2++] = c[j1];
			else if(value(c[j1]) == l_True) {
				removeClause(learnts[i1]);
				goto NEXTL;
			}
		}
		c.shrink(j1-j2);

		for(j1=0; j1 < c.size(); j1++) {
			Var v = var(c[j1]);
			if (!occurred[v]){
				occurred[v] = true;
				varnum++;
			}
		}

		learnts[i2++] = learnts[i1];
		NEXTL:;
	}
	learnts.shrink(i1-i2);

	// garbage collect & relocate only original clauses
	ClauseAllocator to(ca.size() - ca.wasted());
	for (int i = 0; i < clauses.size(); i++)
		ca.reloc(clauses[i], to);
	for (int i = 0; i < learnts.size(); i++)
		ca.reloc(learnts[i], to);
	if (verbosity_c >= 2)
		printf("c  Garbage collection:   %12d bytes => %12d bytes             |\n",
				ca.size()*ClauseAllocator::Unit_Size, to.size()*ClauseAllocator::Unit_Size);
	to.moveTo(ca);
}

void Counter::CompactVariables(const vec<bool>& occurred, int varnum)
{
	// Make a map for renumbering variables
	// so that each projection var's id is smaller than that of any non projection var.

	int new_idx = 0;
	vec<Var> map, nonpvars;
	occ_lists.growTo(2*varnum);
	map.growTo(nVars());
	nonpvars.capacity(nVars());

	vec<double> activity2;
	vec<char> polarity2;
	activity.copyTo(activity2);
	polarity.copyTo(polarity2);

	for(Var v=0; v < nVars(); v++)
		if(!occurred[v]){
			if(value(v) == l_Undef && ispvar[v]) npvars_isolated++;
			continue;
		}else{
			if(ispvar[v]) {
				map[v] = new_idx;
				activity[new_idx] = activity2[v];
				polarity[new_idx] = polarity2[v];
				new_idx++;
			}
			else nonpvars.push_(v);
		}
	npvars = new_idx;
	for(int i=0; i < nonpvars.size(); i++) {
		map[nonpvars[i]] = new_idx;
		activity[new_idx] = activity2[nonpvars[i]];
		polarity[new_idx] = polarity2[nonpvars[i]];
		new_idx++;
	}

	// Rename literals
	for(int i=0; i < clauses.size(); i++) {
		Clause& c = ca[clauses[i]];
		if(c.size() == 2) {
			c[0] = mkLit(map[var(c[0])], sign(c[0]));
			c[1] = mkLit(map[var(c[1])], sign(c[1]));
		} else
			for(int j = 0; j < c.size(); j++){
				c[j] = mkLit(map[var(c[j])], sign(c[j]));
				occ_lists[toInt(c[j])].push(i);
			}
	}
	for(int i=0; i < learnts.size(); i++) {
		Clause& c = ca[learnts[i]];
		if(c.size() == 2) {
			c[0] = mkLit(map[var(c[0])], sign(c[0]));
			c[1] = mkLit(map[var(c[1])], sign(c[1]));
		} else
			for(int j = 0; j < c.size(); j++)
				c[j] = mkLit(map[var(c[j])], sign(c[j]));
	}

	// Shrink and clear vectors on variables
	int diff = nVars() - varnum;

/*
	activity.clear();
	activity.growTo(varnum, 0);
	polarity.clear();
	polarity.growTo(varnum, true);
*/
	activity.shrink(diff);
	polarity.shrink(diff);

	bool dealloc = false;
	vardata  .clear(dealloc);
	assigns  .clear(dealloc);
	seen     .clear(dealloc);
	trail    .clear(dealloc);

	vardata  .growTo(varnum, mkVarData(CRef_Undef, 0));
	assigns  .growTo(varnum, l_Undef);
	seen     .growTo(varnum, 0);

	decision.shrink(diff);

	permDiff.clear();
	permDiff.growTo(varnum,0);

	trail.capacity(varnum+1);
	qhead = 0;

	// Reconstruct watch list
	watches.clear();
	watchesBin.clear();
	watches.init(mkLit(varnum,true));
	watchesBin.init(mkLit(varnum,true));
	clauses_literals = 0;
	for(int i=0; i<clauses.size(); i++) attachClause(clauses[i]);
	for(int i=0; i<learnts.size(); i++) attachClause(learnts[i]);

}


//=================================================================================================
// Major methods:

void Counter::countModels()
{
	int          backtrack_level;
	vec<Lit>     learnt_clause,selectors;
	unsigned int nblevels,szWoutSelectors;
	btStateT     bstate;

	limlevel = 0;
	bool noCurDLlit = false;

	for(auto i = 0; i < nVars(); i++) decision[i] = false;
	cmpmgr.init(nVars(),nPVars(),clauses,ca,cachesize);//,hasThreshold,norma);

	for(;;) {
		assert(!cmpmgr.topDecision().hasUnprocessedSplitComp());

		// state = 1;
		for(int i = 0; i < unitcls.size(); i++){
			checkedEnqueue(unitcls[i], CRef_Undef, 0); // 1
			activity[var(unitcls[i])] = 0;
		}
		if(decisionLevel() == 0) unitcls.clear();

		CRef confl = propagate();
		if(ibcp && confl == CRef_Undef)
			confl = implicitBCP();

		if(confl != CRef_Undef){
			// CONFLICT
			// state = 2;

			conflicts++; // conflictC++; conflictsRestarts++;
			if(conflicts % 5000 == 0 && var_decay < 0.95)
				var_decay += 0.01;

			if(decisionLevel() == 0) break; // return;

			learnt_clause.clear();
			selectors.clear();
			analyzeMC(confl, learnt_clause, selectors, backtrack_level, nblevels, szWoutSelectors, noCurDLlit);

			lbdQueue.push(nblevels);
			sumLBD += nblevels;

			CRef cr = CRef_Undef;
			if (learnt_clause.size() == 1){
				unitcls.push(learnt_clause[0]); nbUn++;
				if(var(learnt_clause[0])<npvars)
					punitcls.push(learnt_clause[0]);
			}
			else {
				cr = ca.alloc(learnt_clause, true);
				ca[cr].setLBD(nblevels); 
				ca[cr].setSizeWithoutSelectors(szWoutSelectors);
				if(nblevels<=2) nbDL2++; 		// stats
				if(ca[cr].size()==2) nbBin++; 	// stats
				learnts.push(cr);
				attachClause(cr);
				claBumpActivity(ca[cr]);
			}

			varDecayActivity();
			claDecayActivity();

			cmpmgr.topDecision().increaseModels(0);
			bstate = backjump(backtrack_level, learnt_clause[0], cr, noCurDLlit);
			noCurDLlit = false;

		} else {
			// NO CONFLICT
			// state = 3;
			assert(!cmpmgr.topDecision().hasUnprocessedSplitComp());

			// SPLIT THE CURRENT COMPONENT
			int nsplitcomps = cmpmgr.splitComponent(assigns);

			if(nsplitcomps == 0) {
				// NO NEW COMPONENT (#MODELS of the current component is found)
				cmpmgr.topDecision().increaseModels(1);
				// limlevel = decisionLevel();
				bstate = backtrack();
			}
			else {
				bstate = GO_TO_NEXT_COMP;

				// PROCESS COMPONENTS WITHOUT PROJECTION VARS FIRST
				lbool sat_status = l_Undef;
				while(cmpmgr.topDecision().hasUnprocessedSplitComp()
						&& (!cmpmgr.topComponent().hasPVar())){ // || (hasThreshold && cmpmgr.topDecision().getCurNorma()==1) || stopping)) {
					sat_status = solveSAT(); // SAT solving, i.e., try to find only one model

					if(sat_status == l_True) {
						cmpmgr.topDecision().increaseModels(1);
						cmpmgr.cacheModelCountOf(cmpmgr.topComponent().id(),1);
						cmpmgr.eraseComponentStackID();
						cmpmgr.popComponent();
					}
					else if(sat_status == l_False) break;
					else assert(false);
				}

				if(sat_status == l_False){
					while(cmpmgr.topDecision().hasUnprocessedSplitComp())
						cmpmgr.popComponent();
					cmpmgr.removeCachePollutions();

					cmpmgr.topDecision().increaseModels(0);
					bstate = backjump(bklevel_final, conflict[0], cr_final, noCurDLlit_final); // Note: the values of arguments are determined by solveSAT()

				} else if(!cmpmgr.topDecision().hasUnprocessedSplitComp()){
					bstate = backtrack();
				}

			}
		}

		if(bstate == EXIT) break; // return;
		else if(bstate == RESOLVED) continue;

		// state = 4;
		assert(cmpmgr.topComponent().hasPVar());

		if(conflicts>=(uint64_t)curRestart* nbclausesbeforereduce)
		{
			assert(learnts.size()>0);
			curRestart = (conflicts/ nbclausesbeforereduce)+1;
			reduceDB();
			nbclausesbeforereduce += incReduceDB;
		}

		// DECIDE A LITERAL FROM TOP COMPONENT
		Var dec_var = cmpmgr.pickBranchVar(activity);
		decisions++;

		cmpmgr.pushDecision(trail.size());
		trail_lim.push(trail.size());
		uncheckedEnqueue(mkLit(dec_var, polarity[dec_var]));
	}

	mpz_mul_2exp(npmodels.get_mpz_t (), cmpmgr.topDecision().totalModels().get_mpz_t (), nIsoPVars());
}

void Counter::checkedEnqueue(Lit p, CRef from, int lv) {
	if(value(p) != l_Undef) {
#if 0
		if(pos != 1){
			printf("c duplicate! lit %d, CRef %d : CDL %d, DDL %d, ADL %d, %s\n",
					toInt(p), from, decisionLevel(), lv, level(var(p)), value(p)==l_True?"True":"False");
		}
#endif
		return;
	}

	assert(value(p) == l_Undef);
	assigns[var(p)] = lbool(!sign(p));
	vardata[var(p)] = mkVarData(from, lv);
	trail.push_(p);
}

void Counter::analyzeMC(CRef confl, vec<Lit>& out_learnt,vec<Lit>&selectors, int& out_btlevel,unsigned int &lbd,unsigned int &szWithoutSelectors,bool &noCurDLlit)
{
	int pathC = 0;
	Lit p     = lit_Undef;

	// Generate conflict clause:
	//
	out_learnt.push();      // (leave room for the asserting literal)
	int index   = trail.size() - 1;

	do{
		assert(confl != CRef_Undef); // (otherwise should be UIP)
		Clause& c = ca[confl];

		// Special case for binary clauses
		// The first one has to be SAT
		if( p != lit_Undef && c.size()==2 && value(c[0])==l_False) {

			assert(value(c[1])==l_True);
			Lit tmp = c[0];
			c[0] =  c[1], c[1] = tmp;
		}

		if (c.learnt())
			claBumpActivity(c);

#ifdef DYNAMICNBLEVEL
		// DYNAMIC NBLEVEL trick (see competition'09 companion paper)
		if(c.learnt()  && c.lbd()>2) {
			unsigned int nblevels = computeLBDMC(c);
			if(nblevels+1<c.lbd() ) { // improve the LBD
				if(c.lbd()<=lbLBDFrozenClause) {
					c.setCanBeDel(false);
				}
				// seems to be interesting : keep it for the next round
				c.setLBD(nblevels); // Update it
			}
		}
#endif


		for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++){
			Lit q = c[j];

			if (!seen[var(q)] && level(var(q)) > 0){
				if(!isSelector(var(q)))
					varBumpActivity(var(q));
				seen[var(q)] = 1;
				if (level(var(q)) >= decisionLevel()) {
					pathC++;
#ifdef UPDATEVARACTIVITY
					// UPDATEVARACTIVITY trick (see competition'09 companion paper)
					if(!isSelector(var(q)) && (reason(var(q))!= CRef_Undef)  && ca[reason(var(q))].learnt())
						lastDecisionLevel.push(q);
#endif

				} else {
					if(isSelector(var(q))) {
						assert(value(q) == l_False);
						selectors.push(q);
					} else
						out_learnt.push(q);
				}
			}
		}

		// Select next clause to look at:
		// while (!seen[var(trail[index--])]);
		// modified by k-hasimt
		while(!seen[var(trail[index])]){
			if(index == trail_lim[decisionLevel()-1]) {
				noCurDLlit = true;	// This means no UIP in the current DL.
				break;
			}
			index--;
		}
		index--;
		p     = trail[index+1];
		confl = reason(var(p));
		seen[var(p)] = 0;
		pathC--;

	}while (pathC > 0);
	out_learnt[0] = ~p;

	// Simplify conflict clause:
	//
	int i, j;

	for(int i = 0;i<selectors.size();i++)
		out_learnt.push(selectors[i]);

	out_learnt.copyTo(analyze_toclear);
	if (ccmin_mode == 2){
		uint32_t abstract_level = 0;
		for (i = 1; i < out_learnt.size(); i++)
			abstract_level |= abstractLevel(var(out_learnt[i])); // (maintain an abstraction of levels involved in conflict)

		for (i = j = 1; i < out_learnt.size(); i++)
			if (reason(var(out_learnt[i])) == CRef_Undef || !litRedundant(out_learnt[i], abstract_level))
				out_learnt[j++] = out_learnt[i];

	}else if (ccmin_mode == 1){
		for (i = j = 1; i < out_learnt.size(); i++){
			Var x = var(out_learnt[i]);

			if (reason(x) == CRef_Undef)
				out_learnt[j++] = out_learnt[i];
			else{
				Clause& c = ca[reason(var(out_learnt[i]))];
				// Thanks to Siert Wieringa for this bug fix!
				for (int k = ((c.size()==2) ? 0:1); k < c.size(); k++)
					if (!seen[var(c[k])] && level(var(c[k])) > 0){
						out_learnt[j++] = out_learnt[i];
						break; }
			}
		}
	}else
		i = j = out_learnt.size();

	max_literals += out_learnt.size();
	out_learnt.shrink(i - j);
	tot_literals += out_learnt.size();


	/* ***************************************
      Minimisation with binary clauses of the asserting clause
      First of all : we look for small clauses
      Then, we reduce clauses with small LBD.
      Otherwise, this can be useless
	 */
	if(!incremental && out_learnt.size()<=lbSizeMinimizingClause) {
		minimisationWithBinaryResolution(out_learnt);
	}
	// Find correct backtrack level:
	//
	if (out_learnt.size() == 1)
		out_btlevel = 0;
	else{
		int max_i = 1;
		// Find the first literal assigned at the next-highest level:
		for (int i = 2; i < out_learnt.size(); i++)
			if (level(var(out_learnt[i])) > level(var(out_learnt[max_i])))
				max_i = i;
		// Swap-in this literal at index 1:
		Lit p             = out_learnt[max_i];
		out_learnt[max_i] = out_learnt[1];
		out_learnt[1]     = p;
		out_btlevel       = level(var(p));

		if(noCurDLlit){		// added by k-hasimt
			// In this case, out_learnt[0] is meaningless. So, get rid of it
			// and pick two literals with the first and second highest DLs among the remaining literals of out_learnt.

			out_learnt[0] = out_learnt[1];
			out_learnt[1] = out_learnt.last();
			out_learnt.shrink(1);

			if(out_learnt.size() == 1) {
				out_btlevel = 0;
			}else{
				max_i = 1;
				// Find the first literal assigned at the next-highest level:
				for (int i = 2; i < out_learnt.size(); i++)
					if (level(var(out_learnt[i])) > level(var(out_learnt[max_i])))
						max_i = i;
				// Swap-in this literal at index 1:
				Lit p             = out_learnt[max_i];
				out_learnt[max_i] = out_learnt[1];
				out_learnt[1]     = p;
				out_btlevel       = level(var(p));

				// noCurDLlit = (level(var(out_learnt[0])) == out_btlevel);
			}

			max_literals--;
			tot_literals--;
		}
	}


	// Compute the size of the clause without selectors (incremental mode)
	if(incremental) {
		szWithoutSelectors = 0;
		for(int i=0;i<out_learnt.size();i++) {
			if(!isSelector(var((out_learnt[i])))) szWithoutSelectors++;
			else if(i>0) break;
		}
	} else
		szWithoutSelectors = out_learnt.size();

	// Compute LBD
	lbd = computeLBDMC(out_learnt,out_learnt.size()-selectors.size());


#ifdef UPDATEVARACTIVITY
	// UPDATEVARACTIVITY trick (see competition'09 companion paper)
	if(lastDecisionLevel.size()>0) {
		for(int i = 0;i<lastDecisionLevel.size();i++) {
			if(ca[reason(var(lastDecisionLevel[i]))].lbd()<lbd)
				varBumpActivity(var(lastDecisionLevel[i]));
		}
		lastDecisionLevel.clear();
	}
#endif



	for(int j = 0; j < analyze_toclear.size(); j++) seen[var(analyze_toclear[j])] = 0;    // ('seen[]' is now cleared)
	for(int j = 0 ; j<selectors.size() ; j++) seen[var(selectors[j])] = 0;
}

Counter::btStateT Counter::backjump(int backtrack_level, Lit lit, CRef cr, bool noCurDLlit)
{
	assert(backtrack_level >= 0);
	if(decisionLevel()==0) return EXIT;

	if(noCurDLlit && backjumping == 1 && cr != CRef_Undef){
		if(level(var(ca[cr][0])) == level(var(ca[cr][1]))) {
			// backjump igoring limlevel
			nbackjumps++;
			cancelUntil(backtrack_level-1);
			cmpmgr.backjumpTo(backtrack_level-1);
			cmpmgr.removeCachePollutions();
			cmpmgr.topDecision().increaseModels(0);
			// if(hasThreshold) cmpmgr.topDecision().resetNorma();
			if(limlevel >= decisionLevel()) limlevel = cmpmgr.searchNewLimLevel();
			return RESOLVED;
		}
		else if(level(var(ca[cr][0])) <= limlevel)	// must backjump to dl smaller than level(var(ca[cr][0])
			limlevel = level(var(ca[cr][0])) - 1;
	}

	if(backjumping == 0)	// NO BACKJUMP
		return backtrack(backtrack_level, lit, cr);

	else if(backjumping == 1){	// LIMITED BACKJUMP
		if(backtrack_level == decisionLevel() - 1 || limlevel >= decisionLevel())
			return backtrack(backtrack_level, lit, cr);

		if(limlevel > backtrack_level){
			backtrack_level = limlevel ;
			nbackjumpstolim++;
		}
		nbackjumps++;
		cancelUntil(backtrack_level);
		cmpmgr.backjumpTo(backtrack_level);
		cmpmgr.removeCachePollutions();
		cmpmgr.topDecision().increaseModels(0); // Reset model count
		// if(hasThreshold) cmpmgr.topDecision().resetNorma();

		assert(lit != lit_Undef);
		if(cr == CRef_Undef)
			checkedEnqueue(lit, CRef_Undef, 0); // 3
		else
			uncheckedEnqueue(lit, cr);

		return RESOLVED;

	}else{	// FULL BACKJUMP
		nbackjumps++;
		cancelUntil(backtrack_level);
		cmpmgr.backjumpTo(backtrack_level);
		cmpmgr.removeCachePollutions();
		cmpmgr.topDecision().increaseModels(0);
		// if(hasThreshold) cmpmgr.topDecision().resetNorma();
		uncheckedEnqueue(lit, cr);
		return RESOLVED;
	}

	assert(false);
	return EXIT;
}

Counter::btStateT Counter::backtrack(int backtrack_level, Lit lit, CRef cr){
	// NOTE:
	// If CONFLICT then backtrack_level = -1 && lit = lit_Undef && cr = CRef_Undef.
	if(decisionLevel()==0) return EXIT;
	for(;;) {
		assert(decisionLevel() > 0);
		assert(!cmpmgr.topDecision().hasUnprocessedSplitComp());

		if(cmpmgr.topDecision().isFirstBranch()) {// && (!hasThreshold || !cmpmgr.topDecision().satisfyNorma()) && !stopping) {
			// FirstBranch
			limlevel = decisionLevel();
#if 0
			if(lit == lit_Undef)
				limlevel = decisionLevel();
			else if(limlevel >= decisionLevel()) //
				limlevel = cmpmgr.searchNewLimLevel();
#endif

			Lit dlit = trail[trail_lim.last()];
			cancelCurDL();

			if(backjumping == 1 && cr != CRef_Undef && decisionLevel() > backtrack_level){
				if(~dlit != lit) uncheckedEnqueue(~dlit);
				checkedEnqueue(lit, cr, decisionLevel()); // 5
			}
			else
				uncheckedEnqueue(~dlit);

			cmpmgr.topDecision().changeBranch();
			return RESOLVED;
		}
		else {
			// SecondBranch
			cmpmgr.prevDecision().increaseModels(cmpmgr.topDecision().totalModels());

			cmpmgr.cacheModelCountOf(cmpmgr.topComponent().id(),cmpmgr.topDecision().totalModels());
			cmpmgr.eraseComponentStackID();

			cancelCurDL();
			trail_lim.pop();
			cmpmgr.popDecision();
			cmpmgr.popComponent();

			if(cmpmgr.topDecision().isUnSAT()) {
				while(cmpmgr.topDecision().hasUnprocessedSplitComp())
					cmpmgr.popComponent();
				cmpmgr.removeCachePollutions();
			}
			else if(cmpmgr.topDecision().hasUnprocessedSplitComp()){
				limlevel = decisionLevel() + 1;
				return GO_TO_NEXT_COMP;
			}

			if(decisionLevel() == 0) return EXIT;
		}
	}

	assert(false);
	return EXIT;
}

void Counter::cancelCurDL()
{
	for (int c = trail.size()-1; c >= trail_lim.last(); c--){
		Var      x  = var(trail[c]);
		assigns [x] = l_Undef;
		polarity[x] = sign(trail[c]);
	}
	qhead = trail_lim.last();
	trail.shrink(trail.size() - trail_lim.last());
}

CRef Counter::implicitBCP(){
	// This is almost the same as that of SharpSAT 12.08.1.

	if(decisionLevel() == 0) return CRef_Undef;
	static vec<Lit> test_lits(nVars());
	static vec<bool> viewed_lits(nVars()*2);
	vec<Lit>    learnt_clause,selectors;
	int backtrack_level;
	unsigned int nblevels,szWoutSelectors;
	int stack_ofs = trail_lim[decisionLevel()-1];
	int num_curr_lits = 0;

	while(stack_ofs < trail.size()) {
		test_lits.clear();
		for(int i=stack_ofs; i<trail.size(); i++){
			vec<int>& ocl = occ_lists[toInt(~trail[i])];
			for(int j=0; j<ocl.size(); j++){
				Clause &c = ca[clauses[ocl[j]]];
				if(!satisfied(c)){
					for(int k=0; k<c.size(); k++){
						if(value(c[k])==l_Undef && !viewed_lits[toInt(~c[k])]){
							test_lits.push(~c[k]);
							viewed_lits[toInt(~c[k])] = true;
						}
					}
				}
			}
		}
		num_curr_lits = trail.size() - stack_ofs;
		stack_ofs = trail.size();
		for(int i = 0;i<test_lits.size();i++){
			viewed_lits[toInt(test_lits[i])] = false;
		}
		vector<double> scores;
		scores.clear();
		for(int i=0;i<test_lits.size();i++){
			scores.push_back(activity[var(test_lits[i])]); // we use activity score of vars instead of literals.
		}
		sort(scores.begin(),scores.end());
		num_curr_lits = 10 + num_curr_lits/20;
		double threshold = 0.0;
		if(scores.size() > num_curr_lits){
			threshold = scores[scores.size()-num_curr_lits];
		}

		for(int i = 0;i<test_lits.size();i++){
			if(value(test_lits[i]) == l_Undef && threshold <= activity[var(test_lits[i])])
			{
				newDecisionLevel();
				uncheckedEnqueue(test_lits[i]);

				CRef bSucceeded = propagate();
				if(bSucceeded == CRef_Undef){
					// NO CONFLICT
					cancelUntil(decisionLevel()-1);
					continue;
				}
				// CONFLICT
				CRef cr = CRef_Undef;
				learnt_clause.clear();
				selectors.clear();
				analyze(bSucceeded, learnt_clause, selectors, backtrack_level,nblevels,szWoutSelectors);
				cancelUntil(decisionLevel()-1);

				lbdQueue.push(nblevels);
				sumLBD += nblevels;

				if(learnt_clause.size() == 1){
					unitcls.push(learnt_clause[0]);nbUn++;
					checkedEnqueue(learnt_clause[0],CRef_Undef,0); // 1
				}else {
					cr = ca.alloc(learnt_clause, true);
					ca[cr].setLBD(nblevels);
					ca[cr].setSizeWithoutSelectors(szWoutSelectors);
					if(nblevels<=2) nbDL2++;
					if(ca[cr].size()==2) nbBin++;
					learnts.push(cr);
					attachClause(cr);
					claBumpActivity(ca[cr]);
					uncheckedEnqueue(learnt_clause[0],cr);
				}
				varDecayActivity();
				claDecayActivity();

				if((bSucceeded = propagate()) != CRef_Undef)
					return bSucceeded ;
			}
		}
	}
	return CRef_Undef;
}

lbool Counter::solveSAT(void)
{
	// state = 5;
	solves++;

	lbool   status = l_Undef;
	newDecisionLevel();

	vec<Var> vs; Var v;
	Component & c = cmpmgr.topComponent();
	for(auto i = 0; (v = c[i]) != var_Undef; i++) {
		decision[v] = true;
		assert(value(v) == l_Undef);
		if (value(v) == l_Undef) vs.push(v);
	}
	order_heap.build(vs);

	int sat_start_dl = decisionLevel();

	trailQueue.fastclear();
	lbdQueue.fastclear();
	sumLBD = 0; conflictsRestarts = 1;
	int curr_restarts = 0;
	while (status == l_Undef){
		status = searchBelow(sat_start_dl, 0);

		curr_restarts++;
	}

	for(auto i = 0; (v = c[i]) != var_Undef; i++)
		decision[v] = false;

	cancelUntil(sat_start_dl-1);
	return status;
}

lbool Counter::searchBelow(int start_dl, int nof_conflicts)
{
	int         backtrack_level;
	int         conflictC = 0;
	vec<Lit>    learnt_clause,selectors;
	unsigned int nblevels,szWoutSelectors;
	bool blocked=false;
	starts++;
	noCurDLlit_final=false;
	for (;;){
		CRef confl = propagate();
		if (confl != CRef_Undef){
			// CONFLICT
			conflicts++; conflictC++; conflictsRestarts++;
			if(conflicts%5000==0 && var_decay<0.95)
				var_decay += 0.01;

			if (decisionLevel() <= start_dl) {
				// LAST CONFLICT
				assert(decisionLevel() != 0);

				conflict.clear();
				selectors.clear();
				analyzeMC(confl, conflict,selectors, bklevel_final, nblevels,szWoutSelectors,noCurDLlit_final);
				lbdQueue.push(nblevels);
				sumLBD += nblevels;

				if(conflict.size() == 1){
					cr_final = CRef_Undef;
					unitcls.push(conflict[0]);
				}else {
					cr_final = ca.alloc(conflict, true);
					ca[cr_final].setLBD(nblevels);
					ca[cr_final].setSizeWithoutSelectors(szWoutSelectors);
					if(nblevels<=2) nbDL2++; // stats
					if(ca[cr_final].size()==2) nbBin++; // stats
					learnts.push(cr_final);
					attachClause(cr_final);
					claBumpActivity(ca[cr_final]);
				}
				return l_False;

			}

			trailQueue.push(trail.size());
			// BLOCK RESTART (CP 2012 paper)
			if( conflictsRestarts>LOWER_BOUND_FOR_BLOCKING_RESTART && lbdQueue.isvalid()  && trail.size()>R*trailQueue.getavg()) {
				lbdQueue.fastclear();
				nbstopsrestarts++;
				if(!blocked) {lastblockatrestart=starts;nbstopsrestartssame++;blocked=true;}
			}

			learnt_clause.clear();
			selectors.clear();
			analyze(confl, learnt_clause, selectors,backtrack_level,nblevels,szWoutSelectors);

			lbdQueue.push(nblevels);
			sumLBD += nblevels;

			cancelUntil(backtrack_level <= start_dl ? start_dl : backtrack_level);

			if (learnt_clause.size() == 1){
				checkedEnqueue(learnt_clause[0], CRef_Undef, 0); // 7
				unitcls.push(learnt_clause[0]);nbUn++;
				// uncheckedEnqueue(learnt_clause[0]);
			}else{
				CRef cr = ca.alloc(learnt_clause, true);
				ca[cr].setLBD(nblevels);
				ca[cr].setSizeWithoutSelectors(szWoutSelectors);
				if(nblevels<=2) nbDL2++; // stats
				if(ca[cr].size()==2) nbBin++; // stats
				learnts.push(cr);
				attachClause(cr);

				claBumpActivity(ca[cr]);
				uncheckedEnqueue(learnt_clause[0], cr);
			}
			varDecayActivity();
			claDecayActivity();

		}else{
			// Our dynamic restart, see the SAT09 competition compagnion paper
			if (( lbdQueue.isvalid() && ((lbdQueue.getavg()*K) > (sumLBD / conflictsRestarts))))
			{
				lbdQueue.fastclear();
				// progress_estimate = progressEstimate();
				cancelUntil(start_dl);
				return l_Undef;
			}


			// Simplify the set of problem clauses:
			//if (decisionLevel() == 0 && !simplify()) {
			//  return l_False;
			//}
			// Perform clause database reduction !
			if(conflicts>=(uint64_t)curRestart* nbclausesbeforereduce)
			{
				assert(learnts.size()>0);
				curRestart = (conflicts/ nbclausesbeforereduce)+1;
				reduceDB();
				nbclausesbeforereduce += incReduceDB;
			}

			decisions++;
			Lit next = pickBranchLit();
			if (next == lit_Undef) return l_True;

			// Increase decision level and enqueue 'next'
			newDecisionLevel();
			uncheckedEnqueue(next);
			// checkedEnqueue(next, CRef_Undef, decisionLevel(), 9);
		}
	}
	return l_Undef;
}

//struct pickpcl_lt {
//    ClauseAllocator& ca;
//    pickpcl_lt(ClauseAllocator& ca_) : ca(ca_) {}
//    bool operator () (CRef x, CRef y) {
//    	if(ca[x].size() > SIZE_PICKED_PCL && ca[y].size() > SIZE_PICKED_PCL) return 0;
//	    return ca[x].size() > ca[y].size();
//    }
//};

//void Counter::initCalcUpperBound(Counter& S)
//{
//	sort(S.clauses, pickpcl_lt(S.ca));	// no good for large instances...
//	sort(S.learnts, pickpcl_lt(S.ca));
//
//	vec<vec<Lit>> pcls;
//	pcls.clear();
//
//	for(int i=0; i < S.punitcls.size(); i++){
//		pcls.push();
//		pcls.last().push(S.punitcls[i]);
//	}
//
//	int cs = 0;
//	int ls = 0;
//	for(int i=0; i <= SIZE_PICKED_PCL; i++){
//		for (int j=cs; j < S.clauses.size() && pcls.size() < NUM_PICKED_PCLS + S.punitcls.size(); j++, cs++){
//			Clause& c = S.ca[S.clauses[j]];
//			if(c.size() != i) break;
//
//			bool allpvars = true;
//			for(int k = 0; k < i; k++){
//				if(var(c[k]) >= S.nPVars()){
//					allpvars = false;
//					break;
//				}
//			}
//			if(allpvars){
//				pcls.push();
//				for(int k = 0; k < i; k++)
//					pcls.last().push(c[k]);
//			}
//		}
//		for (int j=ls; j < S.learnts.size() && pcls.size() < NUM_PICKED_PCLS + S.punitcls.size(); j++, ls++){
//			Clause& c = S.ca[S.learnts[j]];
//			if(c.size() != i) break;
//
//			bool allpvars = true;
//			for(int k = 0; k < i; k++){
//				if(var(c[k]) >= S.nPVars()){
//					allpvars = false;
//					break;
//				}
//			}
//			if(allpvars){
//				pcls.push();
//				for(int k = 0; k < i; k++)
//					pcls.last().push(c[k]);
//			}
//		}
//	}
//
//	for(int i=0; i<S.nPVars(); i++){
//		registerAsPVar(i, true);
//		newVar();
//	}
//
//	vec<Lit> lits;
//	for(int i=0; i<pcls.size(); i++){
//		newVar();
//
//		pcls[i].copyTo(lits);
//		lits.push(mkLit(S.nPVars()+i, true));
//		addClause_(lits);
//
//		lits.clear();
//		lits.push();
//		lits.push(mkLit(S.nPVars()+i, false));	// assert(var(lits[0]) < var(lits[1]));
//		for(int j=0; j<pcls[i].size(); j++){
//			lits[0] = ~pcls[i][j];
//			addClause_(lits);
//		}
//	}
//	lits.clear();
//	for(int i=S.nPVars(); i < nVars(); i++)
//		lits.push(mkLit(i, true));
//	addClause_(lits);
//	registerAsPVar(nVars(), false);
//}

//=================================================================================================
// Methods for Debug

void Counter::toDimacsRaw()
{
	printf("cr ");
	for(int i = 0; i < npvars; i++) printf("%d ", i+1);
	printf("0\n");
	printf("p cnf %d %d\n", nVars(), nClauses());
	for (int i = 0; i < clauses.size(); i++){
		Clause& c = ca[clauses[i]];
		for (int i = 0; i < c.size(); i++)
			printf("%s%d ", sign(c[i]) ? "-" : "", var(c[i])+1);
		printf("0\n");
	}
}

void Counter::printModelIfGLT(int models){
	if(cmpmgr.topDecision().totalModels() >= models) {
		printf("DL%d: ", decisionLevel());
		mpz_out_str(NULL,10,cmpmgr.topDecision().totalModels().get_mpz_t());
		printf("\n");fflush(stdout);
	}
}

void Counter::printTrail(int from, int end){
	printf("trail:\n");
	for(int i=from; i<=end; i++) {
		printf("%d; %s%d; ", i, sign(trail[i])?"-":"", var(trail[i])+1);

		printClause(reason(var(trail[i])));
	}
}

//=================================================================================================
// Inline Methods
// NOTE: These are copies of the inline methods in Glucose.

inline unsigned int Counter::computeLBDMC(const vec<Lit> & lits,int end) {
	int nblevels = 0;
	MYFLAG++;

	if(incremental) { // ----------------- INCREMENTAL MODE
		if(end==-1) end = lits.size();
		unsigned int nbDone = 0;
		for(int i=0;i<lits.size();i++) {
			if(nbDone>=end) break;
			if(isSelector(var(lits[i]))) continue;
			nbDone++;
			int l = level(var(lits[i]));
			if (permDiff[l] != MYFLAG) {
				permDiff[l] = MYFLAG;
				nblevels++;
			}
		}
	} else { // -------- DEFAULT MODE. NOT A LOT OF DIFFERENCES... BUT EASIER TO READ
		for(int i=0;i<lits.size();i++) {
			int l = level(var(lits[i]));
			if (permDiff[l] != MYFLAG) {
				permDiff[l] = MYFLAG;
				nblevels++;
			}
		}
	}

	return nblevels;
}

inline unsigned int Counter::computeLBDMC(const Clause &c) {
	int nblevels = 0;
	MYFLAG++;

	if(incremental) { // ----------------- INCREMENTAL MODE
		int nbDone = 0;
		for(int i=0;i<c.size();i++) {
			if(nbDone>=c.sizeWithoutSelectors()) break;
			if(isSelector(var(c[i]))) continue;
			nbDone++;
			int l = level(var(c[i]));
			if (permDiff[l] != MYFLAG) {
				permDiff[l] = MYFLAG;
				nblevels++;
			}
		}
	} else { // -------- DEFAULT MODE. NOT A LOT OF DIFFERENCES... BUT EASIER TO READ
		for(int i=0;i<c.size();i++) {
			int l = level(var(c[i]));
			if (permDiff[l] != MYFLAG) {
				permDiff[l] = MYFLAG;
				nblevels++;
			}
		}
	}
	return nblevels;
}

