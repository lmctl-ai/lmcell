# Libraries and licenses

The first backend check used the [PhysiCell 1.14.2 source](https://github.com/MathCancer/PhysiCell)
at commit
`dbd3499250141b27600e91e501c54c46f68f2763`. Its `licenses/PhysiCell.txt`
and BioFVM notice are BSD 3-Clause; the bundled pugixml 1.7 source carries
the MIT license. The optional MaBoSS/PhysiBoSS addon is also BSD 3-Clause,
but it is excluded from lmcell and is not fetched as a dependency.

PhysiCell's Makefile requires `-fopenmp`, uses OpenMP pragmas throughout the
core and BioFVM, and has a process-wide simulation state. A serial build
probe compiled all 35 upstream core, module, BioFVM, and pugixml translation
translation units (29 available in the pinned tree; 28 linked, excluding the
duplicate digital-cell-line implementation) with C++17 and no OpenMP runtime using `cpp/serial/omp.h`. That header
implements the only runtime queries observed (`omp_get_thread_num` and
`omp_get_num_threads`); disabled OpenMP pragmas are ignored by the compiler.
This keeps the release statically linkable on Linux, macOS, and MinGW while
making single-thread deterministic runs explicit. The source tree is fetched
at build time by `scripts/sync-deps.sh` and is never committed.

The host itself is MIT licensed. The pinned header-only lmcode-cpp is MIT.
`THIRD_PARTY_NOTICES` embeds the required redistribution notices in the
binary and `lmcell --notices` prints them.

No GPL or LGPL code is linked. Optional intracellular addons are deliberately
outside the build so no additional runtime libraries or model data are
required.

## Build-only initialization repair

ASan/LSan with all upstream sources instrumented exposed 104 bytes in 13 allocations from Microenvironment's global constructor writing vectors in default_microenvironment_options before its constructor runs. CMake removes exactly those 13 premature assign statements in a generated build copy; an exact match count guards the pin. The later options constructor initializes these fields. The pinned source checkout remains clean. The wrapper also explicitly initializes Cell_Definition::is_movable because the upstream copy constructor omits it; UBSan exposed the uninitialized bool read when creating cells. Neither repair changes the numerical models. Full upstream notices remain embedded.

The upstream engine is practical to compile as a serial static library. The selected scope uses real PhysiCell phenotype/motility components and BioFVM, with our limited point-agent coordinator. It does not pretend to expose the entire upstream mechanical/rules framework; that scope limitation is documented in README and numerical notes. The reduced dependency-free fallback is unnecessary.

## Platform-independent substrate binding

The first matrix passed Linux and macOS on both architectures; Windows compiled but the single-voxel uptake oracle returned 1 instead of approximately .991657. The upstream global default phenotype can construct Secretion before the default microenvironment exists. Its sync_to_current_microenvironment checks a stored pointer rather than rebinding it, leaving the pointer null and vectors empty; the later advance resizes them, discarding configured rates. The wrapper now calls sync_to_microenvironment explicitly before setting each definition's exchange vectors. A regression forces that null-pointer startup state on every platform: it reproduces the exact failure with the old wrapper and passes with the explicit binding. Temporary diagnostics were removed. No upstream patch beyond the build-copy initialization repair is needed.
