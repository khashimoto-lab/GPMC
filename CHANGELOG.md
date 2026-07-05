# Change Log

## [1.2.0] - 2026/07/05
Major internal rewrite (architecture, ownership, build system); functionally
almost identical to 1.1.1.

### Changed
- Replace mpfr-based arithmetic with GMP's mpq (rational) throughout
- Replace the old single-dash `-flag=value` option parser with CLI11:
  GNU-style `--flag value` options, automatic `--help`, and type validation

### Removed
- Drop d-DNNF construction (may return as a separate tool later)

## [1.1.1] - 2022/08/26
### Added
- Add a function to contruct a d-DNNF with a variable mapping
- Deal with natural number weights of literals (-natw option)

### Changed
- Improve the preprocessor (refactoring)
- change the public methods of Counter class

## [1.1] - 2022/06/21
### Added
- Add a preprocessor for simplying input CNF. 
- Use FlowCutter for tree decomposition. The result is used for variable selection heuristics. 
- Deal with Weighted model counting

### Removed
- Options "-pp" and "-upto"