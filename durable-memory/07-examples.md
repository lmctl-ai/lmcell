# Examples and checks

examples/tissue_decay.lms asserts survival of a nonmotile cell and uniform exponential substrate decay, then writes counts and profiles. examples/oxygen_rules.lms asserts the exponential survival expectation when oxygen is exactly at a Hill rule's half-max, then writes population counts and final cells. All four files under build/examples/ are byte-identical on rerun with the same seed on the same platform.

Six DSL tests separately exercise the quiescent/decay baseline, exponential growth, apoptosis, independent multiple substrates, persistent-walk MSD and normalized uptake. All eight programs compile and run in test.sh. Example system calls use dsl_cli(p) so execution uses the same host that supplied the schema; bytecode and CSV stay under build/.
