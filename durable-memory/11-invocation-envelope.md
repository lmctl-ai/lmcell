# Invocation envelope (0.1.4)

SDK origin/main 93bddf1, staging compiler 0.2.460. Main loop based on lmfluid.
Validate metadata, load every input before any execution, select/run/complete
parts, stop on first failure, emit one terminal SDK frame. Exit codes: 0 success,
2 CLI/metadata, 3 load/run, 4 application/output failure. Dry metadata and standalone
--dry-run execute neither simulations nor file calls; reject contradictory real
metadata. Release a serialization reserve when memory budgets fail.

cell.print is stderr; simulation outputs use CSV. Preserve the one-PhysiCell-run
per-process restriction. Gallery sweeps still spawn a separate host per case;
tests run complete calculation prefixes with CSV postprocessing and determinism.
Renderer tests require compatible local lmplot/lmdraw/lmblender. No network flag
can download them. Blender uses run_lmcode (no timeout option is provided by that
API; the former system call had a 600-second limit).

Recording uses a compiled C++ executable on every platform (spawnv on Windows,
fork/execv on Unix), records host status and verifies raw terminal framing.
Python starts Windows npm lmscript through its .cmd shim. The compiler capability
probe is the shared lmdraw version, with no executable shell-script stub.
