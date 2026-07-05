#pragma once
#include <vector>

#include "counter/Component.h"
#include "include/gpmc/Semiring.h"

namespace gpmc {

class Decision {
    // child components of this decision live in comp_stack[comp_base_, comp_top_),
    // consumed from the top down (nextComp lowers comp_top_)
    int  comp_base_ = 0;
    int  comp_top_  = 0;

    int  num_comps_ = 0;   // how many components this branch split into (fixed once set)
    bool cur_branch_     = false;

    // per-branch (false/true) model count; null == unset (tracked by hasmodel_)
    SS   models_[2];
    bool hasmodel_[2] = {false, false};
    SS   branch_weight_;

    std::vector<unsigned> dvar_in_comp_;
    unsigned stamp_ = 0;

public:
    explicit Decision(int comp_stack_pos, int ndvars = 0)
        : comp_base_(comp_stack_pos), comp_top_(comp_stack_pos),
          dvar_in_comp_(ndvars, 0) {}

    bool isFirstBranch() const { return !cur_branch_; }
    void changeBranch()        { cur_branch_ = true; num_comps_ = 0; }
    int  curBranch()    const  { return static_cast<int>(cur_branch_); }

    int  compTop()             const { return comp_top_; }
    void setCompTop(int e)           { comp_top_ = e; num_comps_ = comp_top_ - comp_base_; }
    
    // if true, top of comp_stack is an unprocessed split component of this decision
    bool hasUnprocessedComp()  const { return comp_base_ < comp_top_; }
    int  numUnprocessedComp()  const { return comp_top_ - comp_base_; }
    int  numComps()            const { return num_comps_; }
    void nextComp()                  { comp_top_--; }

    void increaseModels(const Semiring& val) {
        if (!hasmodel_[curBranch()]) {
            models_[curBranch()] = val.dup();
        } else {
            models_[curBranch()]->mul_inplace(val);
        }
        hasmodel_[curBranch()] = true;
    }

    void markBranchUnsat() {
        models_[curBranch()] = nullptr;
        hasmodel_[curBranch()] = false;
    }

    void setBranchWeight(SS w)              { branch_weight_ = std::move(w); }
    void mulBranchWeight(const Semiring& w) { branch_weight_->mul_inplace(w); }

    void mulLitsWeights() {
        if (hasmodel_[curBranch()] && branch_weight_) {
            models_[curBranch()]->mul_inplace(*branch_weight_);
        }
    }

    void setDVarInComp(const Component& comp) {
        stamp_++;
        for (int i = 0; i < comp.nDVarsInComp(); i++)
            dvar_in_comp_[comp[i]] = stamp_;
    }
    bool isDVarInComp(Var v) const { return dvar_in_comp_[v] == stamp_; }

    bool hasModel()  const { return hasmodel_[0] || hasmodel_[1]; }
    bool isUnsat()   const { return !hasmodel_[curBranch()]; }

    SS totalModels() const {
        if (!hasmodel_[0] && !hasmodel_[1]) return nullptr;
        if (!hasmodel_[0]) return models_[1]->dup();
        if (!hasmodel_[1]) return models_[0]->dup();
        return models_[0]->add(*models_[1]);
    }
};

}
