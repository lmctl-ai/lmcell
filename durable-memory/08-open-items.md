# Beyond v0.1

- Three-dimensional tissues and arbitrary domain/geometry builders.
- Intracellular SBML and MaBoSS integration; these optional backends are not built.
- Checkpoints, MultiCellDS import/export, visualizers and distributed execution.
- Full mechanical interactions, continuous volume growth, more cell phenotypes, phase reporting and adhesion/repulsion controls.
- Independent in-process simulations require replacing or resetting upstream global caches safely.

This is a research modeling host with explicitly documented units, timestep effects and finite statistical accuracy. It does not claim a calibrated biological tissue model from unparameterized defaults. Results and builders remain alive until process exit; bound ensemble sizes per host process.
- Review 2026-10-23: upstream std::cout chatter now goes to stderr; host stdout is only the JSON result. Open: rules only support max response 2x (increases) or 0 (decreases) with no explicit max; increases on a zero base rate has no effect. CellType.exchange binds one substrate per type and a second call silently replaces the first. Same-seed runs are byte-identical per platform only; mt19937_64 + uniform_real_distribution and cos(long double) are not guaranteed identical across libstdc++/libc++/MinGW. Static glibc (LGPL-2.1) is noticed as in sibling hosts; relink-object availability is a family-wide policy question.
