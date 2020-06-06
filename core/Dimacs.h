/****************************************************************************************[Dimacs.h]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2010, Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/
// Modified so that our counter can accept dimacs cnf files with a specification of projection variables
// by Kenji Hashimoto


#ifndef Glucose_Dimacs_h
#define Glucose_Dimacs_h

#include <stdio.h>

#include "utils/ParseUtils.h"
#include "core/SolverTypes.h"

using namespace Glucose;

namespace GPMC {

//=================================================================================================
// DIMACS Parser:

template<class B, class Solver>
static void readClause(B& in, Solver& S, vec<Lit>& lits) {
    int     parsed_lit, var;
    lits.clear();
    for (;;){
        parsed_lit = parseInt(in);
        if (parsed_lit == 0) break;
        var = abs(parsed_lit)-1;
        while (var >= S.nVars()) S.newVar();
        lits.push( (parsed_lit > 0) ? mkLit(var) : ~mkLit(var) );
    }
}

// --- Added by k-hasimt --- BEGIN
// Read projection vars  :  e.g., "vp 1 2 3 ... 0"
template<class B, class Solver>
static void readProjVars(B& in, Solver&S) {
	int     parsed_var;
	for (;;){
		parsed_var = parseInt(in);
		if (parsed_var == 0) break;
		while (parsed_var-1 >= S.nVars()) S.newVar();
		S.registerAsPVar(parsed_var-1, true);
	}
}
// --- Added by k-hasimt --- END

template<class B, class Solver>
static void parse_DIMACS_main(B& in, Solver& S) {
    vec<Lit> lits;
    int vars    = 0;
    int clauses = 0;
    int pvars   = 0;
    int cnt     = 0;
    for (;;){
        skipWhitespace(in);
        if (*in == EOF) break;
        else if (*in == 'p'){
            if (eagerMatch(in, "p pcnf")){
                vars    = parseInt(in);
                clauses = parseInt(in);
                pvars   = parseInt(in);
                // SATRACE'06 hack
                // if (clauses > 4000000)
                //     S.eliminate(true);
            }else{
                printf("c PARSE ERROR! Unexpected char: %c\n", *in), exit(3);
            }
        }        
	// --- Added by k-hasimt --- BEGIN
        else if (*in == 'v'){
            if (eagerMatch(in, "vp")){
                readProjVars(in, S);
            }
            else
            	skipLine(in);
        // } else if (*in == 'c' || *in == 'p')
        // --- Added by k-hasimt --- END
        } else if (*in == 'c')
            skipLine(in);
        else{
            cnt++;
            readClause(in, S, lits);
            S.addClause_(lits); }
    }
    if (S.verbosity_c > 0) {
    	if (vars < S.nVars())
    		printf("c WARNING! DIMACS header mismatch: wrong number of variables.\n");
    	if (cnt  != clauses)
    		printf("c WARNING! DIMACS header mismatch: wrong number of clauses.\n");
    	if (pvars != S.nPVars())
    		printf("c WARNING! DIMACS header mismatch: wrong number of important variables.\n");
    }
    S.registerAsPVar(S.nVars(), false); // Added by k-hasimt  (Assertion:  S.ispvar.size() == nVars()"+1")

}

// Inserts problem into solver.
//
template<class Solver>
static void parse_DIMACS(gzFile input_stream, Solver& S) {
    StreamBuffer in(input_stream);
    parse_DIMACS_main(in, S); }

//=================================================================================================
}

#endif
