# GPMC-mc2020

GPMC is a projected model counter for CNF formulas. 
The source codes of this software are based on those of
glucose 3.0 and SharpSAT 12.08.1. 
This is a variant of GPMC v1.0.0 obtained by detaching some functions and 
fitting to the format for Model Counting 2020 competition, PMC track.

## Installation
See `INSTALL.md`.

## Usage
```
$ ./gpmc [options]
```
Options:
-  -presat / -no-presat  
    turn on/off SAT solving as preprocessing (default: on)

-  -ibcp 	/ -no-ibcp  
    turn on/off IBCP (default: off)

-  -bj=<0..2>
    - 0: chronological backtracking only (default)
    - 1: limited backjumping
    - 2: (non-limited) backjumping

-  -cs=<1..intmax>  
    set maximum component cache size (MB) (default: 4000)
    NOTE: This bound is only for component cache. GPMC may use much more memory.
    
This version of GPMC gets cnf only from stdin.
    
## Author
[Kenji Hashimoto](https://www.trs.cm.is.nagoya-u.ac.jp/~k-hasimt/index-e.html), 
Nagoya University, Japan. 
