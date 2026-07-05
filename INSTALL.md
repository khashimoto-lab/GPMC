# GPMC Installation

## Requirements

- g++ 10 or later, or clang 10 or later (C++20)
- cmake 3.15 or later
- GMP (GNU Multiple Precision Arithmetic Library)

On Ubuntu/Debian:
```
$ sudo apt install build-essential cmake libgmp-dev
```

## Build

```
$ git clone https://github.com/khashimoto-lab/GPMC.git
$ cd GPMC
$ ./build.sh r
```

The binary is placed at `bin/gpmc`.

## Build options

| Command | Description |
|---|---|
| `./build.sh r` | Release build |
| `./build.sh d` | Debug build |
| `./build.sh rs` | Release static build |
| `./build.sh clean` | Remove build/, bin/, and lib/ |

`rs` needs static GMP libraries (`libgmp.a`/`libgmpxx.a`), which `apt install
libgmp-dev` provides on Debian/Ubuntu. Homebrew's `gmp` on macOS ships only
the dynamic library, so `rs` isn't available there unless you build GMP from
source yourself.

## Using GPMC as a library

Every build also produces `lib/libgpmc.a`, a merge of GPMC's own code with
the vendored Glucose/CaDiCaL/FlowCutter archives. Combined with the public
headers under `include/`, this is all you need to build against GPMC:

```
$ g++ -std=c++20 -I include your_program.cc -L lib -lgpmc -lgmpxx -lgmp -o your_program
```

See the headers under `include/gpmc/` (starting with `CNF.h`, `Counter.h`,
and `Preprocessor.h`) for the public API.

`samples/pmc.cc` and `samples/wmc.cc` are minimal, self-contained programs
(no CLI, no DIMACS parsing) that build a tiny CNF directly through the API
and count it — projected and weighted respectively. Each file's header
comment has the exact build command.
