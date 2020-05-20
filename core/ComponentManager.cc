#include "core/Component.h"
#include "mtl/Sort.h"
#include <utility>

using namespace GPMC;

#define CACHE

void ComponentManager::init(int nvars, int npvars, const vec<CRef>& sclauses, const ClauseAllocator& sca, int cachesize, bool hasThreshold, mpz_class norma){
	vec< vec<Var> > binlinks(nvars);
	vec< vec<ClID> > occ(nvars);

	occlists_.growTo(nvars);
	var_frequency_.growTo(nvars);

	for(int i = 0; i < sclauses.size(); i++) {
		Clause& sc = (Clause &)sca[sclauses[i]];
		if(sc.size() == 2) {
			binlinks[var(sc[0])].push(var(sc[1]));
			binlinks[var(sc[1])].push(var(sc[0]));
			var_frequency_[var(sc[0])]++;
			var_frequency_[var(sc[1])]++;
		}
		else {	// sc.size() > 2 (because bcp was already done and satisfied clauses were removed)
			clauses_.push(ca_.alloc(sc));
			for(int j = 0; j < sc.size(); j++){
				occ[var(sc[j])].push(clauses_.size()-1);
				var_frequency_[var(sc[j])]++;
			}
		}
	}

	occ_pool_.clear();
	for(Var v = 0; v < nvars; v++) {
		occlists_[v] = occ_pool_.size();

		vec<Var>& bl = binlinks[v];
		sort(bl);
		Var prev = var_Undef;
		for(int i = 0; i < bl.size(); i++)
			if(bl[i] != prev) {
				occ_pool_.push(bl[i]);
				prev = bl[i];
			}
		occ_pool_.push(var_Undef);

		vec<ClID>& ocl = occ[v];
		for(int i = 0; i < ocl.size(); i++)
			occ_pool_.push(ocl[i]);
		occ_pool_.push(clid_Undef);
	}

	varseen_.growTo(nvars, SC_CANDIDATE);
	clseen_.growTo(clauses_.size(), SC_CANDIDATE);
	npvars_ = npvars;

#ifdef CACHE
	cache_.init(cachesize);
	CachedComponent::adjustPackSize(nvars, clauses_.size());
#endif

	hasThreshold_ = hasThreshold;
	norma_ = norma;
	initComponentStack(nvars, clauses_.size());
	initDecisionStack();

	components = 1;
	num_try_split = 0;
}

void ComponentManager::initComponentStack(int nvars, int nlongcls) {
	comp_stack_.clear();
	comp_stack_.reserve(nvars + 1);
	comp_stack_.push_back(new Component(nvars, nlongcls, true));

	Component & orig = *(comp_stack_[0]);
	for(Var  v = 0; v < nvars; v++)    orig.pushVar(v);
	orig.closeVar();
	for(ClID c = 0; c < nlongcls; c++) orig.pushClID(c);
	orig.closeClID();

#ifdef CACHE
	CacheEntryID id = cache_.createEntryFor(*comp_stack_.back(), comp_stack_.size() - 1);
	comp_stack_.back()->set_id(id);
#endif
}

void ComponentManager::initDecisionStack()
{
	dl_.clear();
	dl_.push_back(Decision(0,0,hasThreshold_,norma_));
	dl_.back().changeBranch();
}

int ComponentManager::splitComponent(const vec<lbool>& assigns){
	int p; Var v; ClID c;
	mpz_class tmp_model_count;

	unsigned oldtop = comp_stack_.size();
	unsigned boundary = oldtop;
	Component & targetcomp =  *(comp_stack_.back());

	for(p = 0; (v = targetcomp[p]) != var_Undef; p++)
		if(value(v, assigns) == l_Undef){
			varseen_[v] = SC_CANDIDATE;
			var_frequency_[v] = 0;
		}
		else varseen_[v] = SC_NOT_CANDIDATE;

	for(++p; (c = targetcomp[p]) != clid_Undef; p++)
		clseen_[c] = SC_CANDIDATE;


	// Start splitting
	int nvars_in_comp, ncls_in_comp;
	for (p = 0; (v = targetcomp[p]) != var_Undef; p++) {
		if (varseen_[v] == SC_CANDIDATE) {
			searchComponent(v, assigns, nvars_in_comp, ncls_in_comp);
			if (nvars_in_comp == 1) {
				if(isPVar(v)) 	dl_.back().increaseModels(2);
				else 			dl_.back().increaseModels(1);
				varseen_[v] = SC_DONE;
			}else{
				comp_stack_.push_back(new Component(nvars_in_comp, ncls_in_comp, isPVar(v)));

				Component & newcomp = *(comp_stack_.back());

				int pp; Var vv; ClID cc;
				for (pp = 0; (vv = targetcomp[pp]) != var_Undef; pp++)
					if (varseen_[vv] == SC_SEEN) {
						newcomp.pushVar(vv);
						varseen_[vv] = SC_DONE;
					}
				newcomp.closeVar();
				for (++pp; (cc = targetcomp[pp]) != clid_Undef; pp++)
					if (clseen_[cc] == SC_SEEN) {
						newcomp.pushClID(cc);
						clseen_[cc] = SC_DONE;
					}
				newcomp.closeClID();
#ifndef CACHE
				if(isPVar(v)) boundary++;
#endif
#ifdef CACHE
				// caching
				CacheEntryID id = cache_.createEntryFor(*comp_stack_.back(), comp_stack_.size() - 1);
				if (id != 0) {
					comp_stack_.back()->set_id(id);
					assert(cache_.hasEntry(id));
					assert(cache_.hasEntry(targetcomp.id()));
					if (cache_.requestValueOf(*comp_stack_.back(), tmp_model_count)) {
						dl_.back().increaseModels(tmp_model_count);
						cache_.eraseEntry(id);
						delete comp_stack_.back();
						comp_stack_.pop_back();
					} else {
						cache_.entry(id).set_father(targetcomp.id());
						cache_.add_descendant(targetcomp.id(), id);
						if(isPVar(v)) boundary++;
					}
				}
#endif
			}
		}
	}

	Decision& curdl = dl_.back();
	curdl.setSplitCompsEnd(comp_stack_.size());

	for (auto i = oldtop; i < boundary; i++)
		for (auto j = i + 1; j < boundary; j++)
			if (comp_stack_[i]->nVars()< comp_stack_[j]->nVars())
				std::swap(comp_stack_[i], comp_stack_[j]);

	components += comp_stack_.size() - oldtop;
	if(comp_stack_.size() != oldtop) num_try_split++;
	return comp_stack_.size() - oldtop;
}

void ComponentManager::searchComponent(Var seed_var, const vec<lbool>& assigns, int& nvars_in_comp, int& ncls_in_comp){
	vec<Var> vars_in_comp;

	ncls_in_comp = 0;
	vars_in_comp.push(seed_var);
	varseen_[seed_var] = SC_SEEN;

	int p; Var v1, v2; ClID c;
	for(p = 0; p < vars_in_comp.size(); p++){
		v1 = vars_in_comp[p];

		uint32_t q;
		for(q = occlists_[v1]; (v2 = occ_pool_[q]) != var_Undef; q++){
			if(varseen_[v2] == SC_CANDIDATE) {
				assert(value(v2,assigns)==l_Undef);
				vars_in_comp.push(v2);
				varseen_[v2] = SC_SEEN;
				var_frequency_[v1]++;
				var_frequency_[v2]++;
			}
		}

		int nlits;
		for(++q; (c = occ_pool_[q]) != clid_Undef; q++) {
			if(clseen_[c] == SC_CANDIDATE) {
				Clause& cl = ca_[clauses_[c]];
				int before = vars_in_comp.size();
				nlits = 0;
				for(int i = 0; i < cl.size(); i++) {
					if(value(cl[i],assigns) == l_False) continue;
					else if(value(cl[i],assigns) == l_True){
						while(before < vars_in_comp.size()){
							varseen_[vars_in_comp.last()] = SC_CANDIDATE;
							vars_in_comp.pop();
						}
						clseen_[c] = SC_NOT_CANDIDATE;
						for(int j = 0; j < i; j++) {
							if(var_frequency_[var(cl[j])] > 0)
								var_frequency_[var(cl[j])]--;
						}
						goto NEXT;
					}
					else {
						v2 = var(cl[i]);
						assert(value(v2,assigns)==l_Undef);
						nlits++;
						var_frequency_[v2]++;
						if(varseen_[v2] == SC_CANDIDATE){
							varseen_[v2] = SC_SEEN;
							vars_in_comp.push(v2);
						}
					}
				}
				clseen_[c] = SC_SEEN;
				ncls_in_comp++;
			}
			NEXT:;
		}
	}

	nvars_in_comp = vars_in_comp.size();

}

Var ComponentManager::pickBranchVar(const vec<double>& activity)
{
	// GPMC uses the lexicographical order of var_frequency and activity for choosing a decision var.

	Component& c = topComponent();
	assert(dl_.back().SplitCompsEnd() == comp_stack_.size());

	assert(c.nVars() > 0);
	Var maxv = var_Undef;
	double max_score_a = -1;
	double max_score_f = -1;

	int p;
	for(p=0; isPVar(c[p]) && c[p] != var_Undef; p++) {
		double score_f = var_frequency_[c[p]];
		double score_a = activity[c[p]];
		if( score_f > max_score_f) {
			max_score_f = score_f;
			max_score_a = score_a;
			maxv = c[p];
		} else if ( score_f == max_score_f && score_a > max_score_a) {
			max_score_a = score_a;
			maxv = c[p];
		}
	}

	assert(maxv != var_Undef);
	return maxv;
}

void ComponentManager::removeCachePollutions() {
	assert(topDecision().baseComp() == comp_stack_.size()-1);
	cache_.cleanAllDescendantsOf(comp_stack_.back()->id());
	// cache_.cleanPollutionsInvolving(comp_stack_.back()->id());
}
