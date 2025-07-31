#include "gpmc_pywrapper.h"

using namespace Glucose;

CounterWrapper::CounterWrapper()
{
    res = 0;
    config.pp.verb = 0;
}

void CounterWrapper::add_clause(std::vector<int> c)
{
    if (c.empty()) {
        throw std::invalid_argument("add_clase : clause is empty.");
    }

    ps.clear();
    for (auto l : c)
    {
        if (l == 0) {
            throw std::invalid_argument("add_clause: clause contains 0 (invalid literal)");
        }
        int v = abs(l) - 1;
        if (v >= ins.assigns.size())
            ins.assigns.resize(v + 1, l_Undef);
        Lit lit = (l > 0) ? mkLit(v) : ~mkLit(v);
        ps.push_back(lit);
    }
    ins.addClause(ps);
}

mpz_class CounterWrapper::count()
{
    Counter<mpz_class> S(config);
    S.verbosity_c = 1;
    S.loadInstance(ins);

    bool done = S.preprocess();
    if(done) {
        return S.getMC();
    }
    S.setExtraVarScore();
    bool suc = S.countModels();
    if(suc)
        return S.getMC();
    else
        return 0;
}

void  CounterWrapper::set_nvars(int n)
{
    ins.vars = n;
    ins.ispvars.resize(ins.vars, false);
    ins.assigns.resize(ins.vars, l_Undef);
}

void CounterWrapper::set_projvars(std::vector<int> pvars)
{
    for (auto i : pvars)
    {
        int v = i-1;
        if(v >= ins.ispvars.size())
            ins.ispvars.resize(v+1, false);
        if(!ins.ispvars[v]) {
            ins.ispvars[v] = true;
            ins.npvars++;
        }
    }
}

void CounterWrapper::set_mode(int mode) 
{
    std::array<Mode, 4> mode_table = {Mode::MC, Mode::WMC, Mode::PMC, Mode::WPMC};
    config.cntr.mode = (mode >= 0 && mode < 4) ? mode_table[mode] : Mode::MC;

    ins.weighted = false;
    ins.projected = (mode >= 2 && mode < 4);
    ins.keepVarMap = false;
}
