# Beyond v0.1

- Three-dimensional tissues and arbitrary domain/geometry builders.
- Intracellular SBML and MaBoSS integration; these optional backends are not built.
- Checkpoints, MultiCellDS import/export, visualizers and distributed execution.
- Full mechanical interactions, continuous volume growth, more cell phenotypes, phase reporting and adhesion/repulsion controls.
- Independent in-process simulations require replacing or resetting upstream global caches safely.

This is a research modeling host with explicitly documented units, timestep effects and finite statistical accuracy. It does not claim a calibrated biological tissue model from unparameterized defaults. Results and builders remain alive until process exit; bound ensemble sizes per host process.
