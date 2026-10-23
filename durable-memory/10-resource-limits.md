# Resource limits

Why: Sim, Result and Matrix objects live in arenas until exit (10000 per kind), and every
`Result.counts()/cells()/profiles()` call copies a Matrix into the arena, so a DSL loop grows
without bound (a counts table alone is up to 2e6 doubles, 16 MB). On 2026-10-23 a runaway lm*
host (lmnum) reached 16 GB RSS and filled the 24 GB machine; every host now has a total budget.

- **Total memory budget**, default 4096 MiB, set with `LMCELL_MAX_MEMORY_MB` (positive whole
  MiB; anything else exits 2 with the usage text). Shown in the usage message. Configured in
  `main` only for `--dsl` runs.
- `cpp/memory_budget.{hpp,cpp}` (identical copy in every lm* host) replaces the global
  `operator new`/`delete` and counts block sizes from the C allocator. It is compiled into
  `cell_core`, so the ctest executable uses it too.
- PhysiCell/BioFVM allocate only through `std::vector`/`new` (the bundled pugixml uses malloc
  but lmcell never loads XML settings), so cells, voxel grids, the cell container, solver
  scratch, the recorded time series and the arenas are all counted.
- Pre-checks so the budget fails before PhysiCell's global state is half built:
  - grid: `require(nx*ny*(768 + 96*max(1, substrates)))` before `resize_space`. Measured tracked
    peak is about 680 + 94 per substrate bytes per voxel (250k voxels: 1 substrate 170 MB + LOD
    scratch, 32 substrates 724 MB tracked / 925 MB RSS). The existing cap stays at 1e6 voxels.
  - cells: `require(16 KiB)` before every `create_cell` (seeds and divisions); a cell is about
    4.6 KiB tracked (100000 seeded cells: 460 MB). Exponential division therefore stops either at
    the 100000-cell cap or at the budget, with a run error, never inside `create_cell`.
- PhysiCell does not catch `std::bad_alloc`; its `exit(-1)` calls are for XML/rules setup that
  lmcell does not use. A budget failure elsewhere inside BioFVM unwinds through ordinary C++
  frames; `Cleanup` then deletes the fully built cells, and the one-simulation-per-process guard
  stays consumed.
- Exceeding the budget throws `membudget::Exceeded` (a `std::bad_alloc`); host methods turn it
  into a `dslvm::Error` with the same text and `main` also catches `std::exception` around
  `Program::load`/`run`. The first failure prints
  `lmcell: memory budget exceeded: ... (set LMCELL_MAX_MEMORY_MB to raise it)` on stderr; the
  JSON line carries the same text; exit 3.
- Not covered: allocations of one diffusion step beyond the estimate (bounded by the voxel cap).
  No `setrlimit` (not portable to macOS static builds).
- Measured (x86_64 Linux, WSL): the test.sh runaway (one 1e6-step run on a single voxel, then a
  loop of `r.counts()` copies, 16 MB each) stops at 64 MiB in 0.24 s (66 MB peak RSS) and at the
  default 4096 MiB after 2.5-5.7 s with 4.19 GB peak RSS. A 1-cell culture with cycle 0.1 stops at
  128 MiB after 2.3 s (146 MB RSS); a 1000x1000 grid is refused at 256 MiB before allocating
  (824 MiB requested). test.sh runs the runaway, growth and grid cases at 64 MiB on every
  platform and the default-budget run only on Linux, under `ulimit -v 6291456`.
