/*****************************************************************************************[Main.cc]
 *
 * GPMC -- Copyright (c) 2017-2021, Kenji Hashimoto (Nagoya University, Japan)
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

#include "utils/System.h"
#include "utils/ParseUtils.h"
#include "utils/Options.h"
#include "core/Dimacs.h"
// #include "core/Solver.h"
#include "core/Counter.h"

using namespace Glucose;
using namespace GPMC;

//=================================================================================================

static Counter* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int signum) {
	printf("c o\n"); printf("c o *** INTERRUPTED by signal %d ***\n", signum);fflush(stdout);
	solver->interrupt();

	if(solver->stopping || !solver->postprocessing) {
		// solver->printStats();
		_exit(1);
	} else {
		solver->stopping = true;
	}
}

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int signum) {
	printf("c o\n"); printf("c o *** INTERRUPTED by signal %d ***\n", signum);fflush(stdout);
	solver->interrupt();
	// solver->printStats();
	_exit(1); }

//=================================================================================================
// Main:


int main(int argc, char** argv)
{

	try {
		setUsageHelp("c USAGE: %s [options] <input-file>\n");
		// setUsageHelp("c USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
		// printf("This is MiniSat 2.0 beta\n");

#if defined(__linux__)
		fpu_control_t oldcw, newcw;
		_FPU_GETCW(oldcw); newcw = (oldcw & ~_FPU_EXTENDED) | _FPU_DOUBLE; _FPU_SETCW(newcw);
		printf("c o WARNING: for repeatability, setting FPU to use double precision\n");
#endif
		// Extra options:
		//
		IntOption    verb("MAIN", "verb",   "Verbosity level (0=silent, 1=some).", 1, IntRange(0, 1));
		IntOption    cpu_lim("MAIN", "cpu-lim","Limit on CPU time allowed in seconds.\n", INT32_MAX, IntRange(0, INT32_MAX));
		IntOption    mem_lim("MAIN", "mem-lim","Limit on memory usage in megabytes.\n", INT32_MAX, IntRange(0, INT32_MAX));
		StringOption opt_threshold ("GPMC -- MAIN", "upto", "Stop when it finds #models >= threshold. An input threshold should be a natural number.");

		printf("c o "GPMC_VERSION"\n");
		printf("c o Command: ");
		for (int i=0; i < argc; i++)
			printf("%s ", argv[i]);
		printf("\n");
		fflush(stdout);

		parseOptions(argc, argv, true);

		Counter S;
		double initial_time = cpuTime();

		S.verbosity_c = verb;
		S.verbosity = 0;

		S.hasThreshold = (opt_threshold != NULL);
		if(S.hasThreshold) {
			S.norma = S.norma_orig = opt_threshold;
			if(S.norma <= 0){
				fprintf(stderr, "c o Invalid argument: -upto=<num>: num should be a positive natural number > 0.\n");
				exit(1);
			}
		}
		else
			S.norma = S.norma_orig = 0;

		solver = &S;
		// Use signal handlers that forcibly quit until the solver will be able to respond to
		// interrupts:
		signal(SIGINT, SIGINT_exit);
		signal(SIGXCPU,SIGINT_exit);

		// Set limit on CPU-time:
		if (cpu_lim != INT32_MAX){
			rlimit rl;
			getrlimit(RLIMIT_CPU, &rl);
			if (rl.rlim_max == RLIM_INFINITY || (rlim_t)cpu_lim < rl.rlim_max){
				rl.rlim_cur = cpu_lim;
				if (setrlimit(RLIMIT_CPU, &rl) == -1)
					printf("WARNING! Could not set resource limit: CPU-time.\n");
			} }

		// Set limit on virtual memory:
		if (mem_lim != INT32_MAX){
			rlim_t new_mem_lim = (rlim_t)mem_lim * 1024*1024;
			rlimit rl;
			getrlimit(RLIMIT_AS, &rl);
			if (rl.rlim_max == RLIM_INFINITY || new_mem_lim < rl.rlim_max){
				rl.rlim_cur = new_mem_lim;
				if (setrlimit(RLIMIT_AS, &rl) == -1)
					printf("c o WARNING! Could not set resource limit: Virtual memory.\n");
			} }

		if (argc == 1)
			printf("c o Reading from standard input... Use '--help' for help.\n");

		gzFile in = (argc == 1) ? gzdopen(0, "rb") : gzopen(argv[1], "rb");
		if (in == NULL)
			fprintf(stderr, "c o ERROR! Could not open file: %s\n", argc == 1 ? "<stdin>" : argv[1]), exit(1);

		parse_DIMACS(in, S);
		gzclose(in);

		double parsed_time = cpuTime();
		if (S.verbosity_c) S.printProblemStats(parsed_time - initial_time, "parsing");

		// Change to signal-handlers that will only notify the solver and allow it to terminate
		// voluntarily:
		signal(SIGINT,  SIGINT_interrupt);  //  2, SIGINT
		signal(SIGABRT, SIGINT_exit);       //  6, SIGABRT
		signal(SIGSEGV, SIGINT_exit);       // 11, SIGSEGV
		signal(SIGTERM, SIGINT_interrupt);  // 15, SIGTERM
		signal(SIGXCPU, SIGINT_interrupt);

		bool simp = S.presimplify();
		if (S.verbosity_c) {
			double simp_time = cpuTime();
			if (S.verbosity_c) { S.printProblemStats(simp_time - parsed_time, "simplifying"); }
		}
		if (!simp) {
			if (S.verbosity_c) { printf("c o solved by preprocessing.\n"); }
			S.npmodels = 0;
		} else if (S.nVars() == 0) {
			if (S.verbosity_c) { printf("c o solved by preprocessing.\n"); }
			S.npmodels = 1;
			mpz_mul_2exp(S.npmodels.get_mpz_t (), S.npmodels.get_mpz_t (), S.nIsoPVars());
		} else {
			if(S.nIsoPVars() > 0 && S.hasThreshold)
				mpz_cdiv_q_2exp(S.norma.get_mpz_t (), S.norma.get_mpz_t (), S.nIsoPVars());

			S.countModels();
		}

		S.printStats();
		exit(0);

	} catch (const std::invalid_argument&) {
		printf("c o Invalid argument\n");
		exit(1);
	} catch (OutOfMemoryException&){
		printf("c o INDETERMINATE (out of memory)\n");
		solver->interrupt();
		solver->printStats();
		exit(1);
	}
}
