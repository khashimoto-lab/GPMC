# Glucose patches

Patches applied to the vendored Glucose (`extern/glucose/`, version 3.0) at
**build time only**. The vendored sources under `extern/glucose/` are a pristine
Glucose 3.0 snapshot and must never be edited in place.

## How it works

`CMakeLists.txt` (Glucose section) does the following at configure time:

1. Copy `extern/glucose/` to `${CMAKE_BINARY_DIR}/glucose-patched/`.
2. Apply every `extern/glucose-patches/*.patch` (sorted by name) to that copy
   via `git apply`.
3. Build the patched copy instead of `extern/glucose/`.

So `extern/glucose/` is a read-only upstream snapshot, and **all of our
modifications live here as `.patch` files**. This keeps the repository clean,
makes our changes reviewable in isolation, and makes upgrading Glucose a matter
of re-vendoring upstream and re-checking whether each patch still applies.

## The patches

- **`01-suppress-restart-log.patch`** — behavioural. Comments out the per-restart
  `printf("c last restart ...")` in `core/Solver.cc` so model-counting runs stay
  quiet. This is the only behavioural change to Glucose.
- **`02-cxx-modern-compat.patch`** — compile compatibility, required to build
  under a modern C++ standard (GPMC builds at C++20). Three hard errors, not
  warnings:
  - `core/SolverTypes.h`: moves the `= false` default on `mkLit`'s `sign`
    parameter from the `friend` declaration to the inline definition. A default
    argument on a non-defining friend declaration is rejected by GCC
    (`-fpermissive` would be needed otherwise).
  - `utils/Options.h`: adds a space between the format-string literal and the
    `PRIi64` macro (`"%4"PRIi64` → `"%4" PRIi64`, three occurrences). C++11
    onward parses a macro adjacent to a string literal as a user-defined literal.
  - `utils/System.cc`: fixes `MiniSat::memUsedPeak` → `Glucose::memUsedPeak` in
    the `__FreeBSD__` branch, an upstream rename omission left over from
    Glucose's MiniSat ancestry. That branch is never compiled on Linux, so it
    built fine here, but fails with "use of undeclared identifier 'MiniSat'"
    on FreeBSD.

## Naming convention

`NN-short-description.patch`, e.g. `01-suppress-restart-log.patch`. The numeric
prefix fixes the application order.

## Regenerating a patch

To edit a patch, change the file in `build/glucose-patched/`, then from the
repo root:

```sh
diff -u extern/glucose/core/FILE build/glucose-patched/core/FILE
```

The patch paths must be relative to the Glucose root (`a/core/...`,
`b/core/...`) so that `git apply -p1` works from inside the copied tree. See
`CMakeLists.txt` for the exact `git apply` invocation.
