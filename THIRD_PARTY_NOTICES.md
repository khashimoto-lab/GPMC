# Third-Party Notices

GPMC is distributed under the MIT License (see [LICENSE.md](LICENSE.md)).
It incorporates or derives from the following third-party software.

## Components vendored in `extern/`

| Component | Role | License | Full text |
|---|---|---|---|
| [CaDiCaL](https://github.com/arminbiere/cadical) | SAT-based preprocessing | MIT | [extern/cadical/LICENSE](extern/cadical/LICENSE) |
| [Glucose](https://www.labri.fr/perso/lsimon/glucose/) 3.0 (core/mtl/utils) | CDCL engine the counter is built on | MIT (MiniSat terms) | headers of each file under [extern/glucose/](extern/glucose/) |
| [FlowCutter](https://github.com/kit-algo/flow-cutter-pace17) (PACE 2017) | Tree decomposition | BSD 2-Clause | [extern/flow-cutter-pace17/LICENSE](extern/flow-cutter-pace17/LICENSE) |
| [CLI11](https://github.com/CLIUtils/CLI11) | Command-line parsing | BSD 3-Clause | header of [extern/CLI11/CLI11.hpp](extern/CLI11/CLI11.hpp) |
| [rapidhash](https://github.com/Nicoshev/rapidhash) | Component cache hashing | MIT | header of [extern/rapidhash.h](extern/rapidhash.h) |

## Linked libraries (not vendored)

| Library | Role | License |
|---|---|---|
| [GMP](https://gmplib.org/) | Arbitrary-precision integer/rational arithmetic | LGPL v3+ (linked only; obtain from your system) |

## Derived code

### sharpSAT

The component-caching counter core (component analysis, packed component
cache, decision stack organization) descends from sharpSAT by Marc Thurley,
via GPMC. sharpSAT is distributed under the MIT License:

```
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
```

### sharpSAT-TD

The tree-decomposition-guided variable selection (TDScorer) and the
FlowCutter integration (`td/IFlowCutter.*`) are based on
sharpSAT-TD by Tuukka Korhonen and Matti Järvisalo, which is itself a
modification of sharpSAT (MIT License, 2019 Marc Thurley). See
https://github.com/Laakeri/sharpsat-td for details.
