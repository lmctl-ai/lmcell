# Schema, results and units

Context cell:Cell creates Sim builders. Sim stores a Config; CellType handles retain a stable Sim and an index, so vector reallocation cannot invalidate handles. run freezes builders and creates an immutable Result. Matrix snapshots are independent of results. All host arenas have object limits. Schema generation reads literal checked registrations in cpp/main.cpp, and test.sh compares generated JSON, embedded bytes, registry signatures and compiler signatures.

The README API table defines each method and units. Domain lengths must be integer multiples of the spacing. Names are unique, seed coordinates lie in the domain, and all numerics are finite and validated before integer conversions. Limits are 32 types/substrates, 1000 rules, 100000 cells, 1000000 voxels/steps and 200 million grid-step products. A second native simulation fails because of process-global caches. Invalid native initialization consumes the process, while configuration validation occurs before global mutation.

Matrix CSV contains no header: counts time/live; cells x/y/type/alive; profiles x/y/substrates. Outputs are binary-mode files with LF, classic locale and 17 digits. Indexes are zero-based. Counts include t=0 and the exact requested duration. Dead cells can remain in cell output until upstream apoptosis clearance. No-flux boundary is -1; otherwise fixed concentrations apply to edge voxel centers. Single-row domains have only x-edge constraints.

DSL has no lists/maps/callbacks. Scalar helper expressions accept arithmetic, powers and elementary functions in strings; DSL literals use ordinary decimal spelling. User conditions use cell.assert_true; scalar comparisons use cell.assert_close with an absolute tolerance. Dry run never invokes run or file writers.

## Additions (2026-10-23, v0.1.2)

- `Sim.edge(substrate, side, value)`: per-side Dirichlet override (`xmin|xmax|ymin|ymax`, -1 = no flux). Global `boundary` applies first, then edges in call order (last wins at corners). Single-row domains reject y sides.
- `Sim.diffusion_dt(dt)`: exchange + diffusion substeps of `step/ceil(step/dt)` inside each cell step (PhysiCell's diffusion_dt). Uptake constants are recomputed with the substep. The grid-step budget counts substeps.
- `Sim.record(interval)` with `Result.history()` (time,x,y,type,alive) and `Result.profile_history()` (time,x,y,substrates); records at t=0 and the first step at or after each multiple of the interval; capped at 20M history values and 20M profile values.
- `Sim.scatter(type,n,xmin,xmax,ymin,ymax)` and `Sim.seed_disk(type,n,x,y,r)`: splitmix64 stream seeded from the sim seed, so placements are identical on every platform (only `cos`/`sin`/`sqrt` of the disk map can differ in the last bit). The region must already lie inside the domain.
- `CellType.exchange` now accumulates one entry per substrate; repeating a substrate is an error (was: silently replaced the first). `CellType.chemotaxis(substrate, bias)`: bias in [0,1], direction = central-difference gradient of the cell's voxel (one-sided at edges, 0 along a 1-voxel axis); a zero gradient falls back to the unbiased walk.
- `CellType.rule_toward(signal,target,saturation,half_max,hill)`: rate += (saturation - rate) * H, effect stored as "toward". Rule signal may be the reserved name `neighbors` (other live cells in the cell's voxel, counted once at step start, so order independent); a substrate may not be named `neighbors`.
- `Result.type_counts()` (time + live per type, every step) and `Result.live_of(type)`.
