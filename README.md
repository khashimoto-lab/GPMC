# GPMC

GPMC is an exact model counter for CNF formulas supporting:

- Model Counting (MC)
- Weighted Model Counting (WMC)
- Projected Model Counting (PMC)
- Projected Weighted Model Counting (PWMC)

## Installation

See [INSTALL.md](INSTALL.md).

## Usage

```
$ ./bin/gpmc [OPTIONS] [input.cnf]
```

Input is read from stdin if no file is given.

### Counting mode

| Option | Description |
|---|---|
| `--mode mc` | Model counting (default) |
| `--mode pmc` | Projected model counting |
| `--mode wmc` | Weighted model counting |
| `--mode pwmc` | Projected weighted model counting |

### General options

| Option | Description |
|---|---|
| `--no-preprocess` | Disable SAT-based preprocessing (on by default) |
| `--cache-mb N` | Component cache size in MB (default: 4000) |

### Output options

| Option | Description |
|---|---|
| `--frac` / `--no-frac` | Emit the exact fraction; can be very long for rational results (default: off) |
| `--digits N` | Significant digits for prec-sci output (default: 15) |
| `--prec-sci` / `--no-prec-sci` | Also emit decimal sci-notation for rational results (default: on) |

Additional options for experiments (variable selection heuristics, cache
tuning, tree decomposition control) are listed by `--help`.

## Input format

DIMACS CNF format with MCC2021/2024 extensions:

```
p cnf <nvars> <nclauses>
c p show <v1> <v2> ... 0       (projection variables)
c p weight <lit> <w> 0         (literal weight for WMC)
```

### Weights in weighted modes

In `--mode wmc` / `--mode pwmc` every projection variable is expected to carry a
weight on both polarities. Missing weights in the input are completed before
counting per the MCC rule: if neither polarity is given, both default to 1; if
one side `w` is given (with `0 < w < 1`), the other is set to `1 - w`. A
one-sided weight outside `(0, 1)` is an error. After completion the solver
relies on every projection variable being fully weighted.

## Output format

```
s SATISFIABLE
c s type wmc
c s exact arb frac 29/50       (with --frac; off by default)
c s exact arb prec-sci 0.58    (with --prec-sci, on by default)
c s log10-estimate -2.3657200643706278e-01
```

## Lineage and acknowledgments

GPMC builds on a line of component-caching model counters:

- [sharpSAT](https://github.com/marcthurley/sharpSAT) by Marc Thurley —
  the component-caching counter core that GPMC descends from
- [Glucose](https://www.labri.fr/perso/lsimon/glucose/) 3.0 by Gilles Audemard
  and Laurent Simon (based on MiniSat) — the underlying CDCL engine
- [sharpSAT-TD](https://github.com/Laakeri/sharpsat-td) by Tuukka Korhonen and
  Matti Järvisalo — the tree-decomposition-guided variable selection and
  FlowCutter integration
- [CaDiCaL](https://github.com/arminbiere/cadical) by Armin Biere et al. —
  SAT-based preprocessing
- [FlowCutter](https://github.com/kit-algo/flow-cutter-pace17) by Ben Strasser —
  tree decomposition

## License

GPMC is distributed under the MIT License (see [LICENSE.md](LICENSE.md)).
Licenses of the third-party components above are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
