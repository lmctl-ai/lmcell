# Examples and checks

examples/tissue_decay.lms asserts survival of a nonmotile cell and uniform exponential substrate decay, then writes counts and profiles. examples/oxygen_rules.lms asserts the exponential survival expectation when oxygen is exactly at a Hill rule's half-max, then writes population counts and final cells. All four files under build/examples/ are byte-identical on rerun with the same seed on the same platform.

Six DSL tests separately exercise the quiescent/decay baseline, exponential growth, apoptosis, independent multiple substrates, persistent-walk MSD and normalized uptake. All eight programs compile and run in test.sh. Example system calls use dsl_cli(p) so execution uses the same host that supplied the schema; bytecode and CSV stay under build/.

## Story examples (2026-10-23)

Sixteen examples. The eight verification examples above plus eight stories, each asserting a known answer before drawing:

- spheroid_necrosis: 2500-cell disc, nutrient edges fixed at 1 (D=3000, uptake 0.5); rule_toward drives cycle up and apoptosis down with nutrient. Asserts core (r<80) live fraction < 0.45 and rim (150..200) > 0.8. 3-panel lmplot dashboard + 25-frame GIF. The nutrient is generic and slower than oxygen so uptake*dt stays small at dt=0.1.
- tumour_render: same model, lmplot paints the nutrient field as a ground texture, lmscript writes an OBJ/MTL of icospheres (80 faces per cell), lmblender (Cycles, 24 spp, OIDN) renders; ffmpeg palettises to 256 colours (585 KB -> 193 KB). About 30 s.
- drug_penetration: vessel edge (`edge` xmin=1), D=2000, k=0.2 -> L=100 um, diffusion_dt 0.01; asserts every voxel against cosh((600-x)/L)/cosh(585/L) within 0.015 and far-bin survival against exp(-T death(c(550))) within 0.08.
- drug_resistance: two processes (drug on/off); per-type counts vs exp((b-d)t) and logistic resistant fraction; asserts resistant clone 100 e^(0.0006*2400) within 190 (4 sd).
- contact_inhibition: four processes (K=3,5,8, none); time-mean density over the last 300 min within 12 % of 1+2K.
- wound_closure: two processes (migration only, + division with neighbors rule); asserts gap fill > 0.75 vs < 0.6; 25-frame GIF.
- chemotaxis_drift: three bias populations in a linear gradient; asserts drift speeds 0, 2v/pi, v within 0.06-0.08 um/min over t=60..240; 41-frame GIF.
- hill_rules: 18 types reading 6 uniform substrates in one run; measured ln(N/N0)/T within 0.012/min of the three formulas (worst 0.0041); drawn entirely with lmdraw (rule diagram + chart).

Sweeps use one `_dsl_` block inside an lmscript `fn` with `${}` parameters (one host process per case: PhysiCell's globals allow one simulation per process). `${}` inserts whole numbers as ints, so pass ints and convert with `float()` in the DSL, or nudge lmdraw coordinates off whole numbers. The DSL has no arrays or ternary. lmscript `system` defaults to a 60 s timeout; `tumour_render` passes an explicit timeout to the lmblender call. Plain lmscript has no float() or ln(): use num() and a series ln.
