# CaDiCaL patches

Patches applied to the vendored CaDiCaL (`extern/cadical/`, version 3.0.0) at
**build time only**. The vendored sources under `extern/cadical/` are kept
pristine and must never be edited in place.

## How it works

`CMakeLists.txt` (CaDiCaL section) does the following at configure time:

1. Copy `extern/cadical/` to `${CMAKE_BINARY_DIR}/cadical-patched/`.
2. Apply every `extern/cadical-patches/*.patch` (sorted by name) to that copy
   via `git apply`.
3. Build the patched copy instead of `extern/cadical/`.

So `extern/cadical/` is a read-only upstream snapshot, and **all of our
modifications live here as `.patch` files**. This keeps the repository clean,
makes our changes reviewable in isolation, and makes upgrading CaDiCaL a matter
of re-vendoring upstream and re-checking whether each patch still applies.

## Naming convention

`NN-short-description.patch`, e.g. `01-elite-learnt-traverse.patch`. The numeric
prefix fixes the application order.

## Regenerating a patch

To edit a patch, change the file in `build/cadical-patched/`, then from the
repo root:

```sh
diff -u extern/cadical/src/FILE build/cadical-patched/src/FILE
```

or, if you keep a git checkout of the patched tree, `git diff`. The patch paths
must be relative to the CaDiCaL root (`a/src/...`, `b/src/...`) so that
`git apply -p1` works from inside the copied tree. See `CMakeLists.txt` for the
exact `git apply` invocation.
