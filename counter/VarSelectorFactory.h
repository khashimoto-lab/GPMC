#pragma once
#include <memory>
#include <stdexcept>

#include "counter/VSADSSelector.h"
#include "counter/VarSelector.h"
#include "include/gpmc/config/VarSelectorConfig.h"

namespace gpmc {

// Builds the selector for a given mode. The result still needs its two-phase
// initialization (VarSelector::prepareStatic at load time, then bind() at
// count time) — the factory only wires the static config from VarSelectorConfig.
struct VarSelectorFactory {
    static std::unique_ptr<VarSelector> create(const VarSelectorConfig& config)
    {
        switch (config.mode) {
        case VarSelectMode::VSADS:
            return std::make_unique<VSADSSelector>(
                       config.params.w_freq, config.params.w_act);

        case VarSelectMode::VSADS_Lex:
            return std::make_unique<VSADSLexSelector>();

        case VarSelectMode::VSADS_TD:
            return std::make_unique<VSADSTDSelector>(
                       config.td,
                       config.params.w_freq, config.params.w_act, config.params.w_td);

        case VarSelectMode::VSADS_TD_Lex:
            return std::make_unique<VSADSTDLexSelector>(config.td);

        case VarSelectMode::VSIDS:
            return std::make_unique<VSADSSelector>(0.0, config.params.w_act);

        case VarSelectMode::DLCS:
            return std::make_unique<VSADSSelector>(config.params.w_freq, 0.0);

        default:
            throw std::runtime_error("VarSelectMode not yet implemented");
        }
    }
};

}
