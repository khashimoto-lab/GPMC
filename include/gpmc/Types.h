#pragma once

namespace gpmc {

using Var = int;

// Literal in 2*var + sign encoding. Self-contained on purpose: public headers
// must not pull in backend-solver types. The encoding matches the backend
// solver's, so 2v+s-indexed tables (CNF::weights_, lit_weight_) agree across
// the boundary.
struct Lit {
    int x;
    bool operator==(Lit o) const { return x == o.x; }
    bool operator!=(Lit o) const { return x != o.x; }
    bool operator< (Lit o) const { return x <  o.x; }
};

inline Lit  mkLit(Var v, bool neg = false) { return Lit{ v + v + (int)neg }; }
inline Lit  operator~(Lit p) { return Lit{ p.x ^ 1 }; }
inline bool sign(Lit p)  { return p.x & 1; }
inline Var  var(Lit p)   { return p.x >> 1; }
inline int  toInt(Lit p) { return p.x; }

constexpr Var VAR_UNDEF = -1;
constexpr int CL_UNDEF  = -2;

}
