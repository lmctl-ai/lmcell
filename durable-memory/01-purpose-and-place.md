# Purpose and place

lmcell is a scripting-only multicellular simulation host following simulation/PROCEDURE.md. It uses serial PhysiCell 1.14.2 and BioFVM in one static executable, with builder objects and numeric CSV exchange for lmplot. Configuration is built in memory; no XML, kernels, or other runtime data is required. There is one simulation per process because upstream caches and cell definitions are global. Launch another host process for parameter ensembles.

The reduced own-model fallback is reserved for a demonstrated inability to ship upstream cleanly. The permissive upstream license and a serial OpenMP compatibility header allow the real backend to be evaluated on all five platforms first.
