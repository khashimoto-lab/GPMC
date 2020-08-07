GPMC v1.0.1 
		Kenji Hashimoto <k-hasimt@i.nagoya-u.ac.jp>, 
		Nagoya University, Japan.
		Oct, 2018.

GPMC is a projected model counter for CNF formulas. 
The source codes of this software are based on those of
glucose 3.0 and SharpSAT 12.08.1. 

[Input file format]
Input boolean formulas should be in DIMACS CNF format, together with 
a sequence of projection varible IDs before the line "p cnf ...".

Example:

cr 1 2 0
p cnf 3 4
-1 2 0
-2 3 0
2 -3 0
1 -2 3 0

The set of projection variables is { 1, 2 }.

[How to Compile]
g++ 4.7 or later and the GMP bignum package are necessary.
sysinfo is used to get free RAM size.

$ tar xvzf gpmc-1.0.0.tar.gz
$ cd gpmc-1.0.0/core
$ make rs

[Usage]
$ ./gpmc_static [options] [CNF_File]
Options: 
-presat / -no-presat
    turn on/off SAT solving as preprocessing (default: on)

-ibcp 	/ -no-ibcp
    turn on/off IBCP (default: off)

-bj=<0..2>
    0: chronological backtracking only
    1: limited backjumping (default)
    2: (non-limited) backjumping

-cs=<1..intmax>
    set maximum component cache size (MB) (default: 4000)
    NOTE: This bound is only for component cache. GPMC may use much more memory.

-upto=<num>
    stop when the solver finds #projected models >= num. 

-post / -no-post
    do post-processing after the counting procedure is interrupted (default: off).
    This option is not active when -upto option is specified.
    
    If it is interrupted once, do post-processing in order to output the 
    incomplete count result at the time.
    NOTE: The post-processing may spend much time for large instances. 
    
 
