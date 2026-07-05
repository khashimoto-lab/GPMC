#include "include/gpmc/CNF.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "include/gpmc/semirings/Rational.h"

namespace gpmc {

// --- construction ---

void CNF::addClause(std::vector<Lit> lits) {
    std::sort(lits.begin(), lits.end());
    int j = 0;
    for (int i = 0; i < (int)lits.size(); i++) {
        if (j > 0 && lits[i] == ~lits[j-1]) return;
        if (j > 0 && lits[i] == lits[j-1]) continue;
        lits[j++] = lits[i];
    }
    lits.resize(j);
    clauses_.push_back(std::move(lits));
}

// --- I/O ---

bool CNF::readDimacs(std::istream& in,
                     const Semiring* weight_proto) {
    std::string line;
    bool header_seen = false;

    if (weight_proto && !weight_proto_)
        weight_proto_ = weight_proto->one();

    auto ensure_var = [&](int v0) {
        if ((int)is_proj_.size() <= v0)
            is_proj_.resize(v0 + 1, false);
        if (weight_proto && (int)weights_.size() < 2*(v0 + 1))
            weights_.resize(2*(v0 + 1));
    };

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "c") {
            std::string sub;
            if (!(ss >> sub)) continue;
            if (sub == "p") {
                std::string kw;
                if (!(ss >> kw)) continue;
                if (kw == "show") {

                    int v;
                    while (ss >> v && v != 0) {
                        if (v <= 0) continue;
                        ensure_var(v - 1);
                        if (!is_proj_[v - 1]) {
                            is_proj_[v - 1] = true;
                            num_proj_vars_++;
                        }
                    }
                } else if (kw == "weight" && weight_proto) {

                    int lit_int;
                    if (ss >> lit_int) {
                        // Everything up to the trailing "0" line terminator is
                        // the weight payload, verbatim (space-separated tokens
                        // included) — the semiring's parse() decides how to
                        // read it, so we must not stop early just because one
                        // of its tokens happens to be the literal string "0".
                        std::string rest, tok;
                        std::vector<std::string> toks;
                        while (ss >> tok) toks.push_back(tok);
                        if (!toks.empty() && toks.back() == "0") toks.pop_back();
                        for (size_t i = 0; i < toks.size(); i++) {
                            if (i) rest += ' ';
                            rest += toks[i];
                        }
                        if (rest.empty()) rest = "1";
                        int v = std::abs(lit_int) - 1;
                        ensure_var(v);
                        Lit l = mkLit(v, lit_int < 0);
                        setWeight(l, weight_proto->parse(rest));
                    }
                }
            } else if (sub == "MUST") {
                // Mirror of writeDimacs: a previous preprocessing pass folded
                // variables away, recording either a weighted multiplier
                // ("MUST MULTIPLY BY <value>") or an unweighted free-variable
                // count ("MUST SHIFT BY <n>"). Round-trips a preprocessed CNF.
                std::string kw1, kw2;
                ss >> kw1 >> kw2;
                if (kw1 == "MULTIPLY" && kw2 == "BY" && weight_proto) {
                    std::string m_str, tok;
                    std::vector<std::string> toks;
                    while (ss >> tok) toks.push_back(tok);
                    if (!toks.empty() && toks.back() == "0") toks.pop_back();
                    for (size_t i = 0; i < toks.size(); i++) {
                        if (i) m_str += ' ';
                        m_str += toks[i];
                    }
                    if (!m_str.empty())
                        multiplier_ = weight_proto->parse(m_str);
                } else if (kw1 == "SHIFT" && kw2 == "BY") {
                    int n;
                    if (ss >> n) isolated_vars_ = n;
                }
            }
            continue;
        }

        if (token == "p") {
            std::string fmt;
            int nv, nc;
            if (!(ss >> fmt >> nv >> nc)) continue;
            num_vars_ = nv;
            ensure_var(nv - 1);
            header_seen = true;
            continue;
        }

        if (!header_seen) continue;
        std::vector<Lit> lits;
        int lit_int;

        auto check_var = [&](int v) -> bool {
            if (v < num_vars_) return true;
            std::fprintf(stderr,
                "Error: literal with var %d exceeds header var count %d\n",
                v + 1, num_vars_);
            return false;
        };

        try {
            int first = std::stoi(token);
            if (first != 0) {
                int v = std::abs(first) - 1;
                if (!check_var(v)) return false;
                lits.push_back(mkLit(v, first < 0));
            } else { addClause(std::move(lits)); continue; }
        } catch (...) { continue; }

        while (ss >> lit_int) {
            if (lit_int == 0) break;
            int v = std::abs(lit_int) - 1;
            if (!check_var(v)) return false;
            lits.push_back(mkLit(v, lit_int < 0));
        }
        addClause(std::move(lits));
    }

    if ((int)is_proj_.size() > num_vars_) {
        std::fprintf(stderr,
            "Error: show/weight line var exceeds header var count %d\n",
            num_vars_);
        return false;
    }

    return true;
}

static std::string exact_weight_str(const Semiring& w) {
    if (auto r = dynamic_cast<const Rational*>(&w)) return r->to_string_frac();
    return w.to_string();
}

void CNF::writeDimacs(std::ostream& out,
                      const std::string& header_comment) const {
    if (!header_comment.empty())
        out << "c " << header_comment << "\n";

    if (isWeighted()) {
        if (multiplier_)
            out << "c MUST MULTIPLY BY " << exact_weight_str(*multiplier_) << " 0\n";
    } else if (isolated_vars_ > 0) {
        out << "c MUST SHIFT BY " << isolated_vars_ << " 0\n";
    }

    out << "p cnf " << num_vars_ << " " << clauses_.size() << "\n";

    if (num_proj_vars_ > 0) {
        out << "c p show";
        for (int v = 0; v < num_vars_; v++)
            if (isProj(v)) out << " " << (v + 1);
        out << " 0\n";
    }

    if (isWeighted()) {
        for (int v = 0; v < num_vars_; v++) {
            if (!isProj(v)) continue;
            for (bool neg : {false, true}) {
                Lit l = mkLit(v, neg);
                if (!hasWeight(l)) continue;
                int lit_int = neg ? -(v + 1) : (v + 1);
                out << "c p weight " << lit_int << " "
                    << exact_weight_str(*weight(l)) << " 0\n";
            }
        }
    }

    for (const auto& cl : clauses_) {
        for (Lit l : cl)
            out << (sign(l) ? -(var(l) + 1) : (var(l) + 1)) << " ";
        out << "0\n";
    }
}

// Fill in the literal weights MCC's weighted tracks leave implicit. Supplying
// both polarities is really the user's job; we only complete them here because
// the competition format permits one-sided (or omitted) weights and we are
// obliged to interpret them. Specifically, for every projection variable:
//   - both polarities given  -> leave as-is
//   - neither given          -> default both to one
//   - exactly one given w    -> set the other to its complement 1 - w
// Non-projection variables carry no counting weight, so any stray weight on
// them is cleared (this is what keeps an isolated nonproj survivor from folding
// its own weight into the count; see tests/cnf/t33_pwmc_nonproj_weight.cnf).
bool CNF::completeMccWeights() {
    if (!hasWeightProto()) return false;
    for (int v = 0; v < numVars(); v++) {
        if (!isProj(v)) {
            clearWeight(mkLit(v, false));
            clearWeight(mkLit(v, true));
            continue;
        }
        Lit pos = mkLit(v, false), neg = mkLit(v, true);
        bool hp = hasWeight(pos), hn = hasWeight(neg);
        if (hp && hn) continue;
        if (!hp && !hn) {
            setWeight(pos, weightOne());
            setWeight(neg, weightOne());
            continue;
        }
        Lit given = hp ? pos : neg, missing = hp ? neg : pos;
        const mpq_class& wq = static_cast<Rational*>(weight(given).get())->value();
        if (wq > 0 && wq < 1) {
            mpq_class comp = mpq_class(1) - wq;
            comp.canonicalize();
            setWeight(missing, std::make_unique<Rational>(comp));
        } else {
            return false;
        }
    }
    return true;
}

// --- queries ---

const SS& CNF::weight(Lit l) const {
    int i = toInt(l);
    assert(i < (int)weights_.size() && weights_[i] && "weight() called but literal has no weight");
    return weights_[i];
}

SS CNF::weightOne() const {
    assert(weight_proto_ && "weightOne() called in unweighted mode (no semiring set)");
    return weight_proto_->one();
}

}
