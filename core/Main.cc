/*****************************************************************************************[Main.cc]
 *
 * GPMC -- Copyright (c) 2017-2020, Kenji Hashimoto (Nagoya University, Japan)
 *

GPMC sources are based on Glucose 3.0 and SharpSAT 12.08.1.
We will follow the permissions and copyrights of them.

-----------------------------------------------------------

 * SharpSAT https://sites.google.com/site/marcthurley/sharpsat

MIT License

Copyright (c) 2019 marcthurley

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-----------------------------------------------------------

 Glucose -- Copyright (c) 2009, Gilles Audemard, Laurent Simon
				CRIL - Univ. Artois, France
				LRI  - Univ. Paris Sud, France

Glucose sources are based on MiniSat (see below
 copyrights). Permissions and copyrights of
Glucose are exactly the same as Minisat on which it is based on. (see below).

---------------

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

#include <errno.h>

#include <signal.h>
#include <zlib.h>
#include <sys/sysinfo.h>

#include "utils/System.h"
#include "utils/ParseUtils.h"
#include "utils/Options.h"
#include "core/Dimacs.h"
// #include "core/Solver.h"
#include "core/Counter.h"

using namespace Glucose;
using namespace GPMC;

//=================================================================================================


void printStats(Counter& solver)
{
	double cpu_time = cpuTime();
	double mem_used = memUsedPeak(); // 0;       changed by k-hasimt
	// printf("restarts              : %"PRIu64" (%"PRIu64" conflicts in avg)\n", solver.starts,(solver.starts>0 ?solver.conflicts/solver.starts : 0));
	// printf("blocked restarts      : %"PRIu64" (multiple: %"PRIu64") \n", solver.nbstopsrestarts,solver.nbstopsrestartssame);
	// printf("last block at restart : %"PRIu64"\n",solver.lastblockatrestart);
	printf("c nb ReduceDB           : %lld\n", solver.nbReduceDB);
	printf("c nb removed Clauses    : %lld\n", solver.nbRemovedClauses);
	printf("c nb learnts DL2        : %lld\n", solver.nbDL2);
	printf("c nb learnts size 2     : %lld\n", solver.nbBin);
	printf("c nb learnts size 1     : %lld\n", solver.nbUn);

	printf("c conflicts             : %-12"PRIu64"   (%.0f /sec)\n", solver.conflicts   , solver.conflicts   /cpu_time);
	printf("c decisions             : %-12"PRIu64"   (%4.2f %% random) (%.0f /sec)\n", solver.decisions, (float)solver.rnd_decisions*100 / (float)solver.decisions, solver.decisions   /cpu_time);
	printf("c propagations          : %-12"PRIu64"   (%.0f /sec)\n", solver.propagations, solver.propagations/cpu_time);
	printf("c conflict literals     : %-12"PRIu64"   (%4.2f %% deleted)\n", solver.tot_literals, (solver.max_literals - solver.tot_literals)*100 / (double)solver.max_literals);
	// printf("nb reduced Clauses    : %lld\n",solver.nbReducedClauses);

	// --- Added by k-hasimt --- BEGIN
	printf("c SAT solves (component): %"PRIu64"\n", solver.solves);
	// printf("avg. SATsolve restarts: %4.2f\n", (float)solver.starts / (float)solver.solves);
	printf("c backjump mode         : %d\n", solver.backjumping);
	printf("c backjumps(total)      : %d\n", solver.nbackjumps);
	printf("c          (limited)    : %d\n", solver.nbackjumpstolim);
	printf("c presat                : %s\n", solver.presat?"on":"off");
	printf("c ibcp                  : %s\n", solver.ibcp?"on":"off");

	solver.printStatsOfCM();
	// --- Added by k-hasimt --- END

	if (mem_used != 0) printf("c Memory used           : %.2f MB\n", mem_used);
	// printf("c CPU time              : %g s\n", cpu_time);
}


static Counter* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
//static void SIGINT_interrupt(int signum) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int signum) {
	if (solver->verbosity_c > 0){
		printf("\n"); printf("c *** INTERRUPTED *** by signal %d\n", signum);  // "signal" added by k-hasimt
		printStats(*solver);
		printf("c CPU time              : %g s\n", cpuTime());
		printf("c\n"); printf("c *** INTERRUPTED ***\n");
		fflush(stdout);
	}
	_exit(1);
}

int availableRAMSize(int cachesize){
	struct sysinfo info;
	sysinfo(&info);

	uint64_t free_ram_bytes = info.freeram *(uint64_t) info.mem_unit;
	int free_ram = free_ram_bytes / 1048576;

	int maximum_cache_size = cachesize;

	if (cachesize <= 0 || free_ram <= 0) {
		printf("c Not enough memory to run.\n");
		exit(0);
	}

	if (cachesize > free_ram) {
		maximum_cache_size = 7 * free_ram / 10;
		if (solver->verbosity_c > 0) {
			printf("c WARNING: Maximum cache size larger than free RAM available\n");
			printf("c Free RAM %d MB\n", free_ram);
			printf("c Maximum cache size : %d MB -> %d MB\n", cachesize, maximum_cache_size);
		}
	}
	return maximum_cache_size;
}

//=================================================================================================
// Main:


int main(int argc, char** argv)
{

	try {
		setUsageHelp("c USAGE: %s [options] <input-file>\n\n"
				"where input is in plain DIMACS with specification of projection varIDs.\n"
				"See only \"GPMC -- COUNTER\" and \"GPMC -- MAIN\". The other options are for glucose. \n"
				"See also README.txt.\n\n");
		// setUsageHelp("c USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
		// printf("This is MiniSat 2.0 beta\n");

#if defined(__linux__)
		fpu_control_t oldcw, newcw;
		_FPU_GETCW(oldcw); newcw = (oldcw & ~_FPU_EXTENDED) | _FPU_DOUBLE; _FPU_SETCW(newcw);
		// printf("c WARNING: for repeatability, setting FPU to use double precision\n");
#endif
		// Extra options:
		//
		IntOption    verb("GPMC -- MAIN", "verb",   "Verbosity level (0=silent, 1=some).", 0, IntRange(0, 1));
		IntOption    vv("MAIN", "vv",   "Verbosity every vv conflicts", 10000, IntRange(1,INT32_MAX));
		IntOption    cpu_lim("MAIN", "cpu-lim","Limit on CPU time allowed in seconds.\n", INT32_MAX, IntRange(0, INT32_MAX));
		IntOption    mem_lim("MAIN", "mem-lim","Limit on memory usage in megabytes.\n", INT32_MAX, IntRange(0, INT32_MAX));

		IntOption  opt_cachesize ("GPMC -- MAIN", "cs", "Maximum component cache size (MB) (not strict)", 4000, IntRange(1, INT32_MAX));

		parseOptions(argc, argv, true);

		Counter S;
		double initial_time = cpuTime();

		S.verbosity_c = verb;
		S.verbosity = 0;
		S.verbEveryConflicts = vv;
		S.showModel = false; // mod;

		solver = &S;
		S.cachesize = availableRAMSize(opt_cachesize);

		// Use signal handlers that forcibly quit until the solver will be able to respond to
		// interrupts:
		signal(SIGINT,  SIGINT_exit);  //  2, SIGINT
		signal(SIGABRT, SIGINT_exit);  //  6, SIGABRT
		signal(SIGSEGV, SIGINT_exit);  // 11, SIGSEGV
		signal(SIGTERM, SIGINT_exit);  // 15, SIGTERM

		// Set limit on CPU-time:
		if (cpu_lim != INT32_MAX){
			rlimit rl;
			getrlimit(RLIMIT_CPU, &rl);
			if (rl.rlim_max == RLIM_INFINITY || (rlim_t)cpu_lim < rl.rlim_max){
				rl.rlim_cur = cpu_lim;
				if (setrlimit(RLIMIT_CPU, &rl) == -1)
					printf("c WARNING! Could not set resource limit: CPU-time.\n");
			} }

		// Set limit on virtual memory:
		if (mem_lim != INT32_MAX){
			rlim_t new_mem_lim = (rlim_t)mem_lim * 1024*1024;
			rlimit rl;
			getrlimit(RLIMIT_AS, &rl);
			if (rl.rlim_max == RLIM_INFINITY || new_mem_lim < rl.rlim_max){
				rl.rlim_cur = new_mem_lim;
				if (setrlimit(RLIMIT_AS, &rl) == -1)
					printf("c WARNING! Could not set resource limit: Virtual memory.\n");
			} }

		/*
		if (argc == 1)
			printf("c Reading from standard input... Use '--help' for help.\n");

		gzFile in = (argc == 1) ? gzdopen(0, "rb") : gzopen(argv[1], "rb");
		if (in == NULL)
			fprintf(stderr, "c ERROR! Could not open file: %s\n", argc == 1 ? "<stdin>" : argv[1]), exit(1);
		 */
		// This version does not allow users to specify an input file as an argument.
		// It accepts cnf only from stdin.
		gzFile in = gzdopen(0, "rb");
		if (in == NULL)
			fprintf(stderr, "c ERROR! Could not open file: <stdin>\n"), exit(1);
		parse_DIMACS(in, S);
		gzclose(in);

		double parsed_time = cpuTime();
		if (S.verbosity_c > 0){
			printf("c ===========================[ Problem Statistics ]==============================\n");
			printf("c\n");
			printf("c  Number of variables:  %12d\n", S.nVars());
			printf("c  Number of clauses:    %12d\n", S.nClauses());
			printf("c  Number of proj vars:  %12d\n", S.nPVars());
			printf("c  Parse time:           %12.2f s\n", parsed_time - initial_time);
			printf("c  \n");

			printf("c  Compacting Formula...");
			fflush(stdout);
		}
		if(!S.simplifyMC()){
			// if (res != NULL) fprintf(res, "UNSAT\n"), fclose(res);
			if (S.verbosity_c > 0){
				printf("done\nc ===============================================================================\n");
				printf("c Solved by simplification\n");
				printStats(S);
				printf("c\n"); }
			// printf("c UNSATISFIABLE\n");
			printf("c CPU time              : %g s\n", cpuTime());
			printf("s pmc 0\n");fflush(stdout);
			exit(0); // exit(20);
		}
		if (S.verbosity_c > 0){
			printf("done\n");
			printf("c  Number of variables:  %12d\n", S.nVars());
			printf("c  Number of clauses:    %12d\n", S.nClauses());
			printf("c  Number of proj vars:  %12d (total)\n", S.nPVars()+S.nIsoPVars());
			printf("c                        %12d (not isolated)\n", S.nPVars());
			printf("c                        %12d (isolated)\n", S.nIsoPVars());
			printf("c\nc\n");
			printf("c Start counting...");
			fflush(stdout);
		}


		if(S.nVars() != 0){
			S.countModels();
		}
		else{
			S.npmodels = 1;
			mpz_mul_2exp(S.npmodels.get_mpz_t (), S.npmodels.get_mpz_t (), S.nIsoPVars());
		}

		if (S.verbosity_c > 0){
			printf("done\nc\n"); printStats(S); printf("c\n");
		}

		printf("c CPU time              : %g s\n", cpuTime());
		gmp_printf("s pmc %Zd\n", S.npmodels.get_mpz_t());
		fflush(stdout);

	} catch (const std::invalid_argument&) {
		printf("c Invalid argument\n");
		exit(0);
	} catch (OutOfMemoryException&){
		printf("c ===================================================================================================\n");
		printf("c INDETERMINATE\n");
		exit(0);
	}
}
