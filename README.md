# GPMC

GPMC is a projected model counter for CNF formulas.
The source codes of this software are based on those of
MiniSat-based Sat solver and SharpSAT 12.08.1.

Note:  
This branch is an exprimental one that tries to incorporate the techniques 
(preprocessing and variable selecting heuristic by tree decomposition) of 
[SharpSAT-TD](https://zenodo.org/record/4880703) into gpmc, and we focus only on the "reptile" problem as a benchmark 
problem here. 


## Installation
See `INSTALL.md`.

## Usage
```
$ ./gpmc [options] [file]
```
Options:

-  -mc  
    model counting, where all the variables are regarded as projection vars without declaring in an input file (c p show ...)

-  -cs=\<1..intmax>  
    set maximum component cache size (MB) (default: 4000)  
    NOTE: This bound is only for component cache. GPMC may use much more memory.

-  -bj / -no-bj  
    turn on startup limited backjumping (default: on)

-  -upto=\<num>  
    stop when the solver finds #projected models >= num.

-  -pp / -no-pp  
    do post-processing after the counting procedure is interrupted (default: off).  
    This option is not active when -upto option is specified.  
    If it is interrupted once, do post-processing in order to output the
    incomplete count result at the time.
    NOTE: The post-processing may spend much time for large instances.

## Input file format
Input boolean formulas should be in DIMACS CNF format, together with
a sequence of projection varible IDs before the line "p cnf ...".

Example:

```
p cnf 3 4
c p show 1 2 0
-1 2 0
-2 3 0
2 -3 0
1 -2 3 0
```
The set of projection variables is { 1, 2 }.

## Author
[Kenji Hashimoto](https://www.trs.cm.is.nagoya-u.ac.jp/~k-hasimt/index-e.html),
Nagoya University, Japan.
