#include <gmpxx.h>
#include "core/Counter.h"
#include "mtl/Sort.h"
#include "utils/System.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace Glucose;
using namespace GPMC;

//=================================================================================================
// Options:

static const char* _mc = "GPMC -- COUNTER";
static BoolOption opt_mc       (_mc, "mc", "Model counting (all vars are projection vars)", false);
static BoolOption opt_postproc (_mc, "pp", "Postprocessing", false);

static BoolOption opt_bj       (_mc, "bj", "Backjumping", true);
static DoubleOption opt_bj_thd (_mc, "bjthd", "Backjumping threshold", 0.5, DoubleRange(0, false, 1, false));

static BoolOption opt_simp     (_mc, "rmvsatcl", "Remove satisfied clauses", true);
static IntOption  opt_simp_thd (_mc, "rmvsatclthd", "Thereshold of removing satisfied clauses", 2, IntRange(0,INT32_MAX));

//=================================================================================================
// Constructor/Destructor:

Counter::Counter() :
  npmodels           (0)
, norma              (0)
, on_bj              (opt_bj)
, bjthd              (opt_bj_thd)
, on_simp            (opt_simp)
, hasThreshold       (false)
, postprocessing     (opt_postproc)
, stopping           (false)
, verbosity_c        (0)
, mc                 (opt_mc)
, conflicts_pre      (0)
, decisions_pre      (0)
, propagations_pre   (0)
, conflicts_sg       (0)
, decisions_sg       (0)
, propagations_sg    (0)
, sats               (0)
, nbackjumps         (0)
, nbackjumps_sp      (0)
, reduce_dbs_pre     (0)
, simp_dbs           (0)
, real_stime         (realTime())
, simplify_time      (0.0)
, npvars             (0)
, npvars_isolated    (0)
, limlevel           (0)
, last_suc           (false)
, last_bklevel       (0)
, last_cr            (CRef_Undef)
, last_lit           (lit_Undef)
{
	verbosity = 0;
	showModel = false;
}

Counter::~Counter()
{
}

//=================================================================================================
// Methods for simplifying

bool Counter::presimplify()
{
	assert(decisionLevel() == 0);

	if(!FailedLiterals()) return false;

	if(solve_() == l_False) return false;

	conflicts_pre = conflicts;
	decisions_pre = decisions;
	propagations_pre = propagations;
	reduce_dbs_pre = nbReduceDB;

	Compact();

	simpDB_assigns = nAssigns();
	simpDB_props   = clauses_literals + learnts_literals;

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

void Counter::Compact() {
	assert(decisionLevel() == 0);

	int varnum = 0;
	vec<bool> occurred;
	occurred.growTo(nVars(), false);

	// Compact Clauses
	CompactClauses(clauses, occurred, varnum);
	CompactClauses(learnts, occurred, varnum);

	// Compact Variables
	int new_idx = 0;
	vec<Var> map, nonpvars;
	occ_lists.growTo(2*varnum);
	map.growTo(nVars());
	nonpvars.capacity(nVars()-npvars);

	vec<double> activity2;
	vec<char> polarity2;
	activity.copyTo(activity2);
	activity.shrink(nVars()-varnum);
	polarity.copyTo(polarity2);
	polarity.shrink(nVars()-varnum);

	for(Var v=0; v < nVars(); v++) {
		if(occurred[v]) {
			if(ispvar[v]) {
				map[v] = new_idx;
				activity[new_idx] = activity2[v];
				polarity[new_idx] = polarity2[v];
				new_idx++;
			}
			else nonpvars.push_(v);
		} else {
			if(value(v) == l_Undef && ispvar[v]) npvars_isolated++;
		}
	}
	npvars = new_idx;
	for(int i=0; i < nonpvars.size(); i++) {
		map[nonpvars[i]] = new_idx;
		activity[new_idx] = activity2[nonpvars[i]];
		polarity[new_idx] = polarity2[nonpvars[i]];
		new_idx++;
	}
	activity2.clear();
	polarity2.clear();

	// Replace literals according to map
	RewriteClauses(clauses, map);
	RewriteClauses(learnts, map);

	// reset
	reinit(varnum);
	checkGarbage(0.05);
}

void Counter::RewriteClauses(const vec<CRef>& cs, const vec<Var>& map)
{
	for(int i=0; i < cs.size(); i++) {
		Clause& c = ca[cs[i]];
		if(c.size() == 2) {
			c[0] = mkLit(map[var(c[0])], sign(c[0]));
			c[1] = mkLit(map[var(c[1])], sign(c[1]));
		} else {
			for(int j = 0; j < c.size(); j++)
				c[j] = mkLit(map[var(c[j])], sign(c[j]));
		}
	}
}

void Counter::CompactClauses(vec<CRef>& cs, vec<bool>& occurred, int& varnum)
{
	int i1, i2;
	int j1, j2;

	for(i1 = 0, i2 = 0; i1 < cs.size(); i1++)	{
		detachClause(cs[i1]);

		Clause& c = ca[cs[i1]];
		for(j1 = 0, j2 = 0; j1 < c.size(); j1++) {
			if(value(c[j1]) == l_Undef)
				c[j2++] = c[j1];
			else if(value(c[j1]) == l_True) {
				removeClauseNoDetach(cs[i1]);
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

		cs[i2++] = cs[i1];
		NEXTC:;
	}
	cs.shrink(i1-i2);
}

void Counter::removeClauseNoDetach(CRef cr) {
	Clause& c = ca[cr];

	// Don't leave pointers to free'd memory!
	if (locked(c)) vardata[var(c[0])].reason = CRef_Undef;
	c.mark(1);
	ca.free(cr);
}

void Counter::reinit(int varnum)
{
	qhead = 0;
	trail.clear();

	vardata  .clear();
	vardata  .growTo(varnum, mkVarData(CRef_Undef, 0));
	assigns  .clear();
	assigns  .growTo(varnum, l_Undef);
	seen     .clear();
	seen     .growTo(varnum, 0);

	decision .clear();
	decision .growTo(varnum, false);

	permDiff.clear();
	permDiff.growTo(varnum,0);

	watches.cleanAll();
	watches.clear();
	for(int i=0; i<varnum; i++) {
		watches.init(mkLit(varnum, false));
		watches.init(mkLit(varnum, true ));
	}
	watchesBin.cleanAll();
	watchesBin.clear();
	for(int i=0; i<varnum; i++) {
		watchesBin.init(mkLit(i, false));
		watchesBin.init(mkLit(i, true ));
	}

	for(int i=0; i<clauses.size(); i++) attachClause(clauses[i]);
	for(int i=0; i<learnts.size(); i++) attachClause(learnts[i]);
}

bool Counter::simplify()
{
	if (!ok || propagate() != CRef_Undef)
		return ok = false;

	if (nAssigns() == simpDB_assigns || (simpDB_props > 0))
		return true;

	double cpu_time = cpuTime();

	// Remove satisfied clauses:
	removeSatisfied(learnts);
	if (remove_satisfied)        // Can be turned off.
		removeSatisfied(clauses);
	// checkGarbage();
	// rebuildOrderHeap();

	simpDB_assigns = nAssigns();
	simpDB_props   = clauses_literals + learnts_literals;   // (shouldn't depend on stats really, but it will do for now)

	simp_dbs++;
	simplify_time += cpuTime() - cpu_time;

	if(simp_dbs >= opt_simp_thd)
		on_simp = false;
	return true;
}

//=================================================================================================
// Major methods:
void Counter::countModels()
{
	solves = starts = 0;
	count_main();
	cancelUntil(0);
}

void Counter::count_main()
{
	int          backtrack_level;
	vec<Lit>     learnt_clause,selectors;
	unsigned int nblevels,szWoutSelectors;

	btStateT bstate = RESOLVED;
	limlevel = 0;
	cmpmgr.init(nVars(),nPVars(),clauses,ca,hasThreshold,norma);

	for(;;) {
		assert(!cmpmgr.topDecision().hasUnprocessedSplitComp());

		for(int i = 0; i < unitcls.size(); i++){
			enqueue(unitcls[i]);
			vardata[var(unitcls[i])].level = 0;
			activity[var(unitcls[i])] = 0;
		}
		if(decisionLevel() == 0) unitcls.clear();

		CRef confl = propagate();

		if(confl != CRef_Undef){
			// CONFLICT

			conflicts++;
			if(conflicts % 5000 == 0 && var_decay < 0.95)
				var_decay += 0.01;

			if(decisionLevel() == 0) break;

			learnt_clause.clear();
			selectors.clear();
			bool suc = analyzeMC(confl, learnt_clause, selectors, backtrack_level, nblevels, szWoutSelectors);
			if(!suc) {
				nbackjumps_sp++;
				cancelUntil(backtrack_level);
				cmpmgr.backjumpTo(backtrack_level);
				cmpmgr.removeCachePollutions();
				cmpmgr.topDecision().increaseModels(0);	// reset
				if(hasThreshold) cmpmgr.topDecision().resetNorma();
				bstate = RESOLVED;
				continue;
			}

			CRef cr = CRef_Undef;
			if (learnt_clause.size() == 1){
				unitcls.push(learnt_clause[0]); nbUn++;
			}
			else {
				cr = ca.alloc(learnt_clause, true);
				learnts.push(cr);
				ca[cr].setLBD(nblevels); 
				ca[cr].setSizeWithoutSelectors(szWoutSelectors);
				if(nblevels<=2) nbDL2++;
				if(ca[cr].size()==2) nbBin++;

				attachClause(cr);
				claBumpActivity(ca[cr]);
			}

			varDecayActivity();
			claDecayActivity();

			int level = std::max(backtrack_level, limlevel);
			bool canjump = on_bj && level+1 < decisionLevel() && (double)level/decisionLevel() < opt_bj_thd;
			if(canjump) {
				nbackjumps++;
				cancelUntil(level);
				cmpmgr.backjumpTo(level);
				cmpmgr.removeCachePollutions();
				cmpmgr.topDecision().increaseModels(0);	// reset
				if(hasThreshold) cmpmgr.topDecision().resetNorma();
				if(cr != CRef_Undef) uncheckedEnqueue(learnt_clause[0], cr);
				bstate = RESOLVED;

				if(nbackjumps > nPVars()) {
					on_bj = false;
				}
			}
			else {
				cmpmgr.topDecision().increaseModels(0);	// set 0 (no model found at the current branch)
				bstate = backtrack(backtrack_level, learnt_clause[0], cr);
			}

		} else {
			// NO CONFLICT
			assert(!cmpmgr.topDecision().hasUnprocessedSplitComp());

			// SPLIT THE CURRENT COMPONENT
			int nsplitcomps = cmpmgr.splitComponent(assigns);

			if(nsplitcomps == 0) {
				// NO NEW COMPONENT (#MODELS of the current component is found)
				cmpmgr.topDecision().increaseModels(1);
				bstate = backtrack();
			}
			else {
				bstate = GO_TO_NEXT_COMP;

				// PROCESS COMPONENTS WITHOUT PROJECTION VARS FIRST
				lbool sat_status = l_Undef;
				while(cmpmgr.topDecision().hasUnprocessedSplitComp()
						&& (!cmpmgr.topComponent().hasPVar() || (hasThreshold && cmpmgr.topDecision().getCurNorma()==1) || asynch_interrupt)) {
					sat_status = solveSAT(); // SAT solving, i.e., try to find only one model

					if(sat_status == l_True) {
						sats++;
						cmpmgr.topDecision().increaseModels(1);
						if(!cmpmgr.topComponent().hasPVar())
							cmpmgr.cacheModelCountOf(cmpmgr.topComponent().id(),1);
						cmpmgr.eraseComponentStackID();
						cmpmgr.popComponent();
					}
					else if(sat_status == l_False) break;
					else { assert(false); abort(); }
				}

				if(sat_status == l_False){
					while(cmpmgr.topDecision().hasUnprocessedSplitComp())
						cmpmgr.popComponent();
					cmpmgr.removeCachePollutions();

					if(!last_suc) {
						nbackjumps_sp++;
						cancelUntil(last_bklevel);
						cmpmgr.backjumpTo(last_bklevel);
						cmpmgr.removeCachePollutions();
						cmpmgr.topDecision().increaseModels(0);	// reset
						if(hasThreshold) cmpmgr.topDecision().resetNorma();
						bstate = RESOLVED;
						continue;
					}

					int level = std::max(last_bklevel, limlevel);
					bool lastcanjump = on_bj && level+1 < decisionLevel() && (double)level/decisionLevel() < opt_bj_thd;
					if(lastcanjump) {
						nbackjumps++;
						cancelUntil(level);
						cmpmgr.backjumpTo(level);
						cmpmgr.removeCachePollutions();
						cmpmgr.topDecision().increaseModels(0);	// reset
						if(hasThreshold) cmpmgr.topDecision().resetNorma();
						if(last_cr != CRef_Undef) uncheckedEnqueue(last_lit, last_cr);
						bstate = RESOLVED;

						if(nbackjumps > nPVars()) {
							on_bj = false;
						}
					} else {
						cmpmgr.topDecision().increaseModels(0);
						bstate = backtrack(last_bklevel, last_lit, last_cr);
					}
				} else if(!cmpmgr.topDecision().hasUnprocessedSplitComp()){
					bstate = backtrack();
				}
			}
		}

		if(bstate == EXIT) break;
		else if(bstate == RESOLVED) continue;

		assert(cmpmgr.topComponent().hasPVar());

		if (on_simp && decisionLevel() <= limlevel && cmpmgr.checkfixedDL() && !simplify()) {
			cmpmgr.topDecision().increaseModels(0);
			bstate = backtrack();
			if(bstate == EXIT) break;
			else { assert(false); abort(); }
		}

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

		newDecisionLevel();
		cmpmgr.pushDecision(trail.size());
		uncheckedEnqueue(mkLit(dec_var, polarity[dec_var]));
	}

	mpz_mul_2exp(npmodels.get_mpz_t (), cmpmgr.topDecision().totalModels().get_mpz_t (), nIsoPVars());
}


bool Counter::analyzeMC(CRef confl, vec<Lit>& out_learnt, vec<Lit>&selectors,
		int& out_btlevel, unsigned int &lbd, unsigned int &szWithoutSelectors) {
	int pathC = 0;
	Lit p = lit_Undef;

	// Generate conflict clause:
	//
	out_learnt.push();      // (leave room for the asserting literal)
	int index = trail.size() - 1;

	do {
		assert(confl != CRef_Undef); // (otherwise should be UIP)
		Clause& c = ca[confl];

		// Special case for binary clauses
		// The first one has to be SAT
		if (p != lit_Undef && c.size() == 2 && value(c[0]) == l_False) {

			assert(value(c[1])==l_True);
			Lit tmp = c[0];
			c[0] = c[1], c[1] = tmp;
		}

		if (c.learnt())
			claBumpActivity(c);

#ifdef DYNAMICNBLEVEL
		// DYNAMIC NBLEVEL trick (see competition'09 companion paper)
		if (c.learnt() && c.lbd() > 2) {
			unsigned int nblevels = computeLBDMC(c);
			if (nblevels + 1 < c.lbd()) { // improve the LBD
				if (c.lbd() <= lbLBDFrozenClause) {
					c.setCanBeDel(false);
				}
				// seems to be interesting : keep it for the next round
				c.setLBD(nblevels); // Update it
			}
		}
#endif

		for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++) {
			Lit q = c[j];

			if (!seen[var(q)] && level(var(q)) > 0) {
				if (!isSelector(var(q)))
					varBumpActivity(var(q));
				seen[var(q)] = 1;
				if (level(var(q)) >= decisionLevel()) {
					pathC++;
#ifdef UPDATEVARACTIVITY
					// UPDATEVARACTIVITY trick (see competition'09 companion paper)
					if (!isSelector(var(q)) && (reason(var(q)) != CRef_Undef)
							&& ca[reason(var(q))].learnt())
						lastDecisionLevel.push(q);
#endif

				} else {
					if (isSelector(var(q))) {
						assert(value(q) == l_False);
						selectors.push(q);
					} else
						out_learnt.push(q);
				}
			}
		}

		// added by k-hasimt
		if (pathC <= 0) {
			// backjump conservatively to restart
			out_btlevel = 0;
			for (int i = 1; i < out_learnt.size(); i++) {
				seen[var(out_learnt[i])] = 0;
				if (out_btlevel < level(var(out_learnt[i])))
					out_btlevel = level(var(out_learnt[i]));
			}
			if (out_btlevel > 0)
				out_btlevel--;
			for (int j = 0; j < selectors.size(); j++)
				seen[var(selectors[j])] = 0;
			// ToDo: make an appropriate learnt clause and do backjumping.
			return false;
		}
		//---
		/*
		 // Select next clause to look at:
		 while (!seen[var(trail[index--])]);
		 p     = trail[index+1];
		 confl = reason(var(p));
		 seen[var(p)] = 0;
		 pathC--;
		 */
		do {
			// Select next clause to look at:
			while (!seen[var(trail[index--])])
				;
			p = trail[index + 1];
			confl = reason(var(p));
			seen[var(p)] = 0;
			pathC--;
			if (confl == CRef_Undef && pathC > 0) {
				out_learnt.push(~p);
				assert(level(var(p)) <= decisionLevel());
			}
		} while (confl == CRef_Undef && pathC > 0);

	} while (pathC > 0);
	out_learnt[0] = ~p;

	// Simplify conflict clause:
	//
	int i, j;

	for (int i = 0; i < selectors.size(); i++)
		out_learnt.push(selectors[i]);

	out_learnt.copyTo(analyze_toclear);
	if (ccmin_mode == 2) {
		uint32_t abstract_level = 0;
		for (i = 1; i < out_learnt.size(); i++)
			abstract_level |= abstractLevel(var(out_learnt[i])); // (maintain an abstraction of levels involved in conflict)

		for (i = j = 1; i < out_learnt.size(); i++)
			if (reason(var(out_learnt[i])) == CRef_Undef
					|| !litRedundant(out_learnt[i], abstract_level))
				out_learnt[j++] = out_learnt[i];

	} else if (ccmin_mode == 1) {
		for (i = j = 1; i < out_learnt.size(); i++) {
			Var x = var(out_learnt[i]);

			if (reason(x) == CRef_Undef)
				out_learnt[j++] = out_learnt[i];
			else {
				Clause& c = ca[reason(var(out_learnt[i]))];
				// Thanks to Siert Wieringa for this bug fix!
				for (int k = ((c.size() == 2) ? 0 : 1); k < c.size(); k++)
					if (!seen[var(c[k])] && level(var(c[k])) > 0) {
						out_learnt[j++] = out_learnt[i];
						break;
					}
			}
		}
	} else
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
	if (!incremental && out_learnt.size() <= lbSizeMinimizingClause) {
		minimisationWithBinaryResolution(out_learnt);
	}
	// Find correct backtrack level:
	//
	if (out_learnt.size() == 1)
		out_btlevel = 0;
	else {
		int max_i = 1;
		// Find the first literal assigned at the next-highest level:
		for (int i = 2; i < out_learnt.size(); i++)
			if (level(var(out_learnt[i])) > level(var(out_learnt[max_i])))
				max_i = i;
		// Swap-in this literal at index 1:
		Lit p = out_learnt[max_i];
		out_learnt[max_i] = out_learnt[1];
		out_learnt[1] = p;
		out_btlevel = level(var(p));
	}

	// Compute the size of the clause without selectors (incremental mode)
	if (incremental) {
		szWithoutSelectors = 0;
		for (int i = 0; i < out_learnt.size(); i++) {
			if (!isSelector(var((out_learnt[i]))))
				szWithoutSelectors++;
			else if (i > 0)
				break;
		}
	} else
		szWithoutSelectors = out_learnt.size();

	// Compute LBD
	lbd = computeLBDMC(out_learnt, out_learnt.size() - selectors.size());

#ifdef UPDATEVARACTIVITY
	// UPDATEVARACTIVITY trick (see competition'09 companion paper)
	if (lastDecisionLevel.size() > 0) {
		for (int i = 0; i < lastDecisionLevel.size(); i++) {
			if (ca[reason(var(lastDecisionLevel[i]))].lbd() < lbd)
				varBumpActivity(var(lastDecisionLevel[i]));
		}
		lastDecisionLevel.clear();
	}
#endif

	for (int j = 0; j < analyze_toclear.size(); j++)
		seen[var(analyze_toclear[j])] = 0;    // ('seen[]' is now cleared)
	for (int j = 0; j < selectors.size(); j++)
		seen[var(selectors[j])] = 0;

	return true;
}

Counter::btStateT Counter::backtrack(int backtrack_level, Lit lit, CRef cr) {
	if (decisionLevel() == 0)
		return EXIT;
	for (;;) {
		assert(decisionLevel() > 0);
		assert(!cmpmgr.topDecision().hasUnprocessedSplitComp());

		if (cmpmgr.topDecision().isFirstBranch()
				&& (!hasThreshold || !cmpmgr.topDecision().satisfyNorma())
				&& !asynch_interrupt) {
			// FirstBranch

			Lit dlit = trail[trail_lim.last()];
			cancelCurDL();
			uncheckedEnqueue(~dlit);
			// Note: we do not try to assign an extra lit by the learnt clause when conflict occurs, but it may be an option.

			limlevel = decisionLevel();
			cmpmgr.topDecision().changeBranch();
			return RESOLVED;
		} else {
			// SecondBranch
			cmpmgr.prevDecision().increaseModels(
					cmpmgr.topDecision().totalModels());
			if (!asynch_interrupt)
				cmpmgr.cacheModelCountOf(cmpmgr.topComponent().id(),
						cmpmgr.topDecision().totalModels());
			cmpmgr.eraseComponentStackID();

			cancelCurDL();
			trail_lim.pop();
			cmpmgr.popDecision();
			cmpmgr.popComponent();

			if (cmpmgr.topDecision().isUnSAT()) {
				while (cmpmgr.topDecision().hasUnprocessedSplitComp())
					cmpmgr.popComponent();
				cmpmgr.removeCachePollutions();
			} else if (cmpmgr.topDecision().hasUnprocessedSplitComp()) {
				limlevel = decisionLevel() + 1;
				return GO_TO_NEXT_COMP;
			}

			if (decisionLevel() == 0)
				return EXIT;
		}
	}

	assert(false);
	return EXIT;
}

void Counter::cancelCurDL() {
	for (int c = trail.size() - 1; c >= trail_lim.last(); c--) {
		Var x = var(trail[c]);
		assigns[x] = l_Undef;
		polarity[x] = sign(trail[c]);
	}
	qhead = trail_lim.last();
	trail.shrink(trail.size() - trail_lim.last());
}

lbool Counter::solveSAT(void) {
	newDecisionLevel();
	int sat_start_dl = decisionLevel();

	vec<Var> vs;
	Var v;
	Component & c = cmpmgr.topComponent();
	for (auto i = 0; (v = c[i]) != var_Undef; i++) {
		decision[v] = true;
		assert(value(v) == l_Undef);
		if (value(v) == l_Undef)
			vs.push(v);
	}
	order_heap.build(vs);

	solves++;

	trailQueue.fastclear();
	lbdQueue.fastclear();
	sumLBD = 0;
	conflictsRestarts = 1;

	uint64_t old_conflicts = conflicts;
	uint64_t old_decisions = decisions;
	uint64_t old_propagations = propagations;

	lbool status = l_Undef;
	while (status == l_Undef) {
		status = searchBelow(sat_start_dl);
	}

	conflicts_sg += conflicts - old_conflicts;
	decisions_sg += decisions - old_decisions;
	propagations_sg += propagations - old_propagations;

	for (auto i = 0; (v = c[i]) != var_Undef; i++)
		decision[v] = false;

	cancelUntil(sat_start_dl - 1);
	return status;
}

lbool Counter::searchBelow(int start_dl) {
	int backtrack_level;
	int conflictC = 0;
	vec<Lit> learnt_clause, selectors;
	unsigned int nblevels, szWoutSelectors;
	bool blocked = false;
	starts++;

	for (;;) {
		CRef confl = propagate();
		if (confl != CRef_Undef) {
			// CONFLICT
			conflicts++;
			conflictC++;
			conflictsRestarts++;
			if (conflicts % 5000 == 0 && var_decay < 0.95)
				var_decay += 0.01;

			if (decisionLevel() <= start_dl) {
				// LAST CONFLICT
				assert(decisionLevel() != 0);

				learnt_clause.clear();
				selectors.clear();
				last_suc = analyzeMC(confl, learnt_clause, selectors,
						last_bklevel, nblevels, szWoutSelectors);
				if (last_suc) {
					last_lit = learnt_clause[0];
					last_cr = CRef_Undef;
					if (learnt_clause.size() == 1) {
						unitcls.push(learnt_clause[0]);
						nbUn++;
					} else {
						last_cr = ca.alloc(learnt_clause, true);
						learnts.push(last_cr);
						ca[last_cr].setLBD(nblevels);
						ca[last_cr].setSizeWithoutSelectors(szWoutSelectors);
						if (nblevels <= 2)
							nbDL2++;
						if (ca[last_cr].size() == 2)
							nbBin++;

						attachClause(last_cr);
						claBumpActivity(ca[last_cr]);
					}
					varDecayActivity();
					claDecayActivity();
				}

				return l_False;
			}

			trailQueue.push(trail.size());
			// BLOCK RESTART (CP 2012 paper)
			if (conflictsRestarts > LOWER_BOUND_FOR_BLOCKING_RESTART
					&& lbdQueue.isvalid()
					&& trail.size() > R * trailQueue.getavg()) {
				lbdQueue.fastclear();
				nbstopsrestarts++;
				if (!blocked) {
					lastblockatrestart = starts;
					nbstopsrestartssame++;
					blocked = true;
				}
			}

			learnt_clause.clear();
			selectors.clear();
			analyze(confl, learnt_clause, selectors, backtrack_level, nblevels,
					szWoutSelectors);

			lbdQueue.push(nblevels);
			sumLBD += nblevels;

			cancelUntil(
					backtrack_level <= start_dl ? start_dl : backtrack_level);

			if (learnt_clause.size() == 1) {
				uncheckedEnqueue(learnt_clause[0]);
				vardata[var(learnt_clause[0])].level = 0;
				unitcls.push(learnt_clause[0]);
				nbUn++;
			} else {
				CRef cr = ca.alloc(learnt_clause, true);
				ca[cr].setLBD(nblevels);
				ca[cr].setSizeWithoutSelectors(szWoutSelectors);
				if (nblevels <= 2)
					nbDL2++;
				if (ca[cr].size() == 2)
					nbBin++;
				learnts.push(cr);
				attachClause(cr);

				claBumpActivity(ca[cr]);
				uncheckedEnqueue(learnt_clause[0], cr);
			}
			varDecayActivity();
			claDecayActivity();

		} else {
			// Our dynamic restart, see the SAT09 competition compagnion paper
			if ((lbdQueue.isvalid()
					&& ((lbdQueue.getavg() * K) > (sumLBD / conflictsRestarts)))) {
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
			if (conflicts >= (uint64_t) curRestart * nbclausesbeforereduce) {
				assert(learnts.size() > 0);
				curRestart = (conflicts / nbclausesbeforereduce) + 1;
				reduceDB();
				nbclausesbeforereduce += incReduceDB;
			}

			decisions++;
			Lit next = pickBranchLit();
			if (next == lit_Undef)
				return l_True;

			// Increase decision level and enqueue 'next'
			newDecisionLevel();
			uncheckedEnqueue(next);
		}
	}
	return l_Undef;
}

//=================================================================================================
// Print methods:
void Counter::printStats() const
{
	if(verbosity_c) {
		double cpu_time = cpuTime();
		double mem_used = memUsedPeak();

		printf("c o [Statistics]\n");
		printf("c o conflicts             = %-11"PRIu64" (presat %"PRIu64", count %"PRIu64", sat %"PRIu64")\n", conflicts, conflicts_pre, conflicts-conflicts_pre-conflicts_sg, conflicts_sg);
		printf("c o decisions             = %-11"PRIu64" (presat %"PRIu64", count %"PRIu64", sat %"PRIu64")\n", decisions, decisions_pre, decisions-decisions_pre-decisions_sg, decisions_sg);
		printf("c o propagations          = %-11"PRIu64" (presat %"PRIu64", count %"PRIu64", sat %"PRIu64")\n", propagations, propagations_pre, propagations-propagations_pre-propagations_sg, propagations_sg);
		printf("c o simp dbs              = %-11"PRIu64" (%.3f s)\n", simp_dbs, simplify_time);
		printf("c o reduce dbs            = %-11"PRIu64" (presat %"PRIu64", pmc %"PRIu64")\n", nbReduceDB, reduce_dbs_pre, nbReduceDB-reduce_dbs_pre);
		printf("c o learnts (uni/bin/lbd2)= %"PRIu64"/%"PRIu64"/%"PRIu64"\n", nbUn, nbBin, nbDL2);
		printf("c o last learnts          = %-11d (%"PRIu64" learnts removed, %4.2f%%)\n", learnts.size(), nbRemovedClauses, nbRemovedClauses * 100 / (double)conflicts);

		printStatsOfCM();
		printf("c o isolated_pvars        = %"PRIu64"\n", nIsoPVars());
		printf("c o SAT calls             = %-11"PRIu64" (SAT %"PRIu64", UNSAT %"PRIu64")\n", solves, sats, solves-sats);
		printf("c o SAT starts            = %"PRIu64"\n", starts);
		printf("c o backjumps             = %-11"PRIu64" (sp %"PRIu64") [init %s / final %s]\n", nbackjumps+nbackjumps_sp, nbackjumps_sp, opt_bj ? "on" : "off", on_bj ? "on" : "off");
		printf("c o postprocess           = %s\n", postprocessing ? "on" : "off");
		printf("c o hasThereshold         = %s\n", hasThreshold   ? "on" : "off");
		if (mem_used != 0)
			printf("c o Memory used           = %.2f MB\n", mem_used);
		printf("c o CPU time              = %.3f s\n", cpu_time);
		printf("c o Real time             = %.3f s\n", realTime() - real_stime);
		printf("c o\n");

		printf("c o [Result]\n");
	}
	if(!asynch_interrupt) {
		if(hasThreshold) {
			printf("c o #pmc >= ");
			mpz_out_str(stdout, 10, norma.get_mpz_t());
			printf(" ?\n");
			printf("c s %s\n", npmodels>=norma ? "YES" : "NO");
		} else {
			if(npmodels == 0 ) {
				printf("s UNSATISFIABLE\n");
				printf("c s type %s\n", mc ? "mc" : "pmc");
				printf("c s log10-estimate %.15g\n", log10(0));
				printf("c s exact arb int 0\n");
			} else {
				printf("s SATISFIABLE\n");
				// adopt an easy way for log10-estimate. not sure about the precision...
				printf("c s type %s\n", mc ? "mc" : "pmc");
				printf("c s log10-estimate %.15g\n", log10(mpz_get_d(npmodels.get_mpz_t())));
				printf("c s exact arb int ");
				mpz_out_str(stdout, 10, npmodels.get_mpz_t());
				printf("\n");
			}
		}
	} else {
		if(postprocessing) {
			printf("s %s\n", npmodels>0 ? "SATISFIABLE" : "UNSATISFIABLE");
			printf("c s type %s\n", mc ? "mc" : "pmc");
			printf("c s lower bound arb int ");
			mpz_out_str(stdout, 10, npmodels.get_mpz_t());
			printf("\n");
		}
		else
			printf("s UNKNOWN\n");
	}
	fflush(stdout);
}

//=================================================================================================
// Methods for Debug

void Counter::toDimacsRaw(const char *file)
{
	FILE* f = fopen(file, "wr");
	if (f == NULL)
		fprintf(stderr, "could not open file %s\n", file), exit(1);

	fprintf(f, "p cnf %d %d\n", nVars(), nClauses());

	fprintf(f, "cr ");
	for(int i=0; i<nPVars(); i++)
		fprintf(f, "%d ", i+1);
	fprintf(f, "0\n");

	for(int i=0; i<trail.size();i++){
		fprintf(f, "%s%d 0\n", sign(trail[i]) ? "-" : "", var(trail[i])+1);
	}
	for(int i=0; i<clauses.size(); i++) {
		Clause& c = ca[clauses[i]];
		for(int j=0; j<c.size(); j++) {
			fprintf(f, "%s%d ", sign(c[j]) ? "-":"", var(c[j])+1);
		}
		fprintf(f, "0\n");
	}

	fclose(f);
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
