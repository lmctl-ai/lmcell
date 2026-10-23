# Schema, results and units

Context cell:Cell creates Sim builders. Sim stores a Config; CellType handles retain a stable Sim and an index, so vector reallocation cannot invalidate handles. run freezes builders and creates an immutable Result. Matrix snapshots are independent of results. All host arenas have object limits. Schema generation reads literal checked registrations in cpp/main.cpp, and test.sh compares generated JSON, embedded bytes, registry signatures and compiler signatures.

The README API table defines each method and units. Domain lengths must be integer multiples of the spacing. Names are unique, seed coordinates lie in the domain, and all numerics are finite and validated before integer conversions. Limits are 32 types/substrates, 1000 rules, 100000 cells, 1000000 voxels/steps and 200 million grid-step products. A second native simulation fails because of process-global caches. Invalid native initialization consumes the process, while configuration validation occurs before global mutation.

Matrix CSV contains no header: counts time/live; cells x/y/type/alive; profiles x/y/substrates. Outputs are binary-mode files with LF, classic locale and 17 digits. Indexes are zero-based. Counts include t=0 and the exact requested duration. Dead cells can remain in cell output until upstream apoptosis clearance. No-flux boundary is -1; otherwise fixed concentrations apply to edge voxel centers. Single-row domains have only x-edge constraints.

DSL has no lists/maps/callbacks. Scalar helper expressions accept arithmetic, powers and elementary functions in strings; DSL literals use ordinary decimal spelling. User conditions use cell.assert_true; scalar comparisons use cell.assert_close with an absolute tolerance. Dry run never invokes run or file writers.
