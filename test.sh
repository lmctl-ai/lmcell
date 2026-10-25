#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
BUILD_DIR="${BUILD_DIR:-build}"
HOST="$BUILD_DIR/lmcell"
UNAME_S="$(uname -s)"
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) HOST="$HOST.exe";; esac
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmcell-test-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
# Native Windows tools (node, the host) cannot address git-bash /tmp or /d/a paths.
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) WORK="$(cd "$WORK" && pwd -W | tr '\134' '/')";; esac
pass=0
ok(){ pass=$((pass+1)); echo "ok $pass - $1"; }
fail(){ echo "FAIL: $*" >&2; exit 1; }
sha(){ if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }

# lmscript resolves cli="lmcell" on PATH first, so programs bind to the binary under test.
PATH="$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$HERE/$BUILD_DIR"):$PATH"
export PATH
LMSCRIPT="$(scripts/select-compiler.sh)"
scripts/sync-sdk.sh --verify >/dev/null || fail "SDK header pin mismatch"
ok "SDK header matches sdk.lock"
scripts/sync-deps.sh --verify >/dev/null || fail "third_party pin mismatch"
ok "third_party matches deps.lock"

GEN=""
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then GEN="-G Ninja"; fi
# shellcheck disable=SC2086
cmake -S . -B "$BUILD_DIR" $GEN -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" --parallel "${BUILD_JOBS:-4}" >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; fail "build"; }
ok "build"
ctest --test-dir "$BUILD_DIR" --output-on-failure || fail "native cell tests"
ok "native cell regression tests"
python3 scripts/check-version.py "$HOST" || fail "package version agreement"
ok "VERSION agrees with CMake, schema, notices, binary and release tag"
want_version="lmcell $(sed -n 's/^project(lmcell VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ "$("$HOST" --version)" = "$want_version" ] || fail "--version is not exactly '$want_version'"
ok "--version prints exactly '$want_version'"
# Compare bytes directly: git worktrees (.git is a file) and tarballs must not skip this.
cp schema/lmcell.dsl.json "$WORK/schema.before"
python3 scripts/gen-schema.py >/dev/null || fail "scripts/gen-schema.py"
cmp -s "$WORK/schema.before" schema/lmcell.dsl.json || fail "schema/lmcell.dsl.json was stale (now regenerated): rebuild and commit it"
ok "schema matches the registrations in cpp/main.cpp"
"$HOST" --dsl-schema --json >"$WORK/schema.json" || fail "embedded schema print"
cmp -s "$WORK/schema.json" schema/lmcell.dsl.json || fail "embedded schema differs from schema/lmcell.dsl.json"
ok "--dsl-schema --json prints byte-identical embedded schema bytes"
WITHOUT_STD=""
"$LMSCRIPT" --help 2>&1 | grep -q -- '--without-std' && WITHOUT_STD="--without-std"
diff -u <("$LMSCRIPT" --dsl-signatures schema/lmcell.dsl.json $WITHOUT_STD) <("$HOST" --dsl-signatures) || fail "host/compiler signatures differ"
ok "host registry matches compiler signatures"
"$HOST" --notices >"$WORK/notices.txt" || fail "--notices"
cmp -s "$WORK/notices.txt" THIRD_PARTY_NOTICES || fail "--notices differs from THIRD_PARTY_NOTICES"
for want in PhysiCell BioFVM pugixml lmcode-cpp; do grep -q "$want" "$WORK/notices.txt" || fail "--notices lacks $want"; done
ok "--notices prints THIRD_PARTY_NOTICES byte for byte"

case "$UNAME_S" in
  Linux)
    if command -v readelf >/dev/null 2>&1; then
      needed="$(readelf -d "$HOST" | sed -n 's/.*(NEEDED).*Shared library: \[\(.*\)\]/\1/p')"
    else
      needed="$(ldd "$HOST" | awk '/=>/ {print $1} !/=>/ && /ld-linux/ {sub(/^[[:space:]]*/, ""); sub(/[[:space:]].*/, ""); print $0}' | xargs -n1 basename)"
    fi
    [ -n "$needed" ] || fail "expected dynamic glibc linkage on Linux, found no NEEDED entries"
    bad="$(printf '%s\n' "$needed" | grep -v -E '^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|ld-linux.*)' || true)"
    [ -z "$bad" ] || fail "disallowed Linux dynamic libraries: $bad"
    ok "Linux binary links only dynamic glibc (${needed//$'\n'/ })";;
  Darwin) bad="$(otool -L "$HOST" | tail -n +2 | grep -v -E '/usr/lib/(libSystem|libc\+\+|libresolv)' || true)"
          [ -z "$bad" ] || fail "non-system dylib dependencies: $bad"; ok "only system libraries linked";;
  MINGW*|MSYS*|CYGWIN*)
    if command -v objdump >/dev/null 2>&1; then
      bad="$(objdump -p "$HOST" | grep 'DLL Name' | grep -i -E 'libstdc|libgcc|winpthread|libgomp' || true)"
      [ -z "$bad" ] || fail "MinGW runtime DLLs are imported: $bad"
      ok "no MinGW runtime DLL imports"
    else ok "static-link check skipped (no objdump)"; fi;;
  *) ok "static-link check skipped on $UNAME_S";;
esac

# Programs: compile every test and example, run them (they assert their own numbers).
mkdir -p build/examples images
rm -f images/*.png
n=0
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  [ ! -e "$WORK/compiled-$name" ] || fail "duplicate program basename: $name"
  touch "$WORK/compiled-$name"
  "$LMSCRIPT" "$src" "build/$name.dslb" >"$WORK/$name.log" 2>&1 || { cat "$WORK/$name.log" >&2; fail "$name compile"; }
  n=$((n+1))
done
ok "$n programs compile as bytecode v5"
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >"$WORK/$name.out" 2>"$WORK/$name.err" || { cat "$WORK/$name.out" "$WORK/$name.err" >&2; fail "$name run"; }
  grep -q '"ok": true' "$WORK/$name.out" || { cat "$WORK/$name.out" >&2; fail "$name did not report ok"; }
  # No program prints, so stdout must be exactly the JSON result (no upstream chatter).
  [ "$(tr -d '\r' <"$WORK/$name.out")" = '{"ok": true, "result": null}' ] || { cat "$WORK/$name.out" >&2; fail "$name stdout is not only the JSON result"; }
done
ok "$n programs run, pass their own assertions and keep stdout to the JSON result"

# Determinism: the CSV files the examples write are identical on rerun.
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/before.txt"
for src in examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >/dev/null 2>&1 || fail "$name rerun"
done
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/after.txt"
cmp -s "$WORK/before.txt" "$WORK/after.txt" || fail "result files differ on rerun"
ok "all $(wc -l <"$WORK/after.txt" | tr -d ' ') result files are byte-identical on rerun"

# Error paths: each snippet must fail the run with exit 3 and a message naming the problem.
expect_error() { # name, snippet, expected message fragment
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmcell";
$2
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null 2>"$WORK/$1.cerr" || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  set +e; out="$("$HOST" --dsl "$WORK/$1.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] || fail "$1: exit code $code, expected 3 ($out)"
  echo "$out" | grep -q "$3" || fail "$1: message lacks '$3': $out"
}
expect_error domain 'let s=cell.sim(1).domain(1.0,0.0,0.0,10.0,1.0); s.run(1.0,0.1);' "domain"
expect_error step 'cell.sim(1).run(1.0,0.0);' "dt"
expect_error diffusion 'cell.sim(1).substrate("oxygen",-1.0,0.0,1.0,-1.0).run(0.0,1.0);' "diffusion"
expect_error rate 'cell.sim(1).cell_type("a").cycle(-1.0);' "cycle"
expect_error persistence 'cell.sim(1).cell_type("a").motility(1.0,0.0);' "persistence"
expect_error missing_type 'cell.sim(1).seed("unknown",1,0.0,0.0);' "unknown cell type"
expect_error rule_effect 'let s=cell.sim(1).substrate("o",1.0,0.0,1.0,-1.0); s.cell_type("a").rule("o","bad","cycle entry",1.0,2.0);s.run(1.0,0.1);' "effect"
expect_error rule_signal 'let s=cell.sim(1); s.cell_type("a").rule("unknown","decreases","cycle entry",1.0,2.0);s.run(1.0,0.1);' "substrate"
expect_error repeat 'let s=cell.sim(1);s.run(0.0,1.0);s.run(0.0,1.0);' "already run"
expect_error global_repeat 'cell.sim(1).run(0.0,1.0);cell.sim(2).run(0.0,1.0);' "one PhysiCell"
expect_error index 'let m=cell.sim(1).run(0.0,1.0).counts();m.get(2,0);' "index"
expect_error unknown_expr 'cell.eval("unknown+1");' "unknown"
expect_error assertfail 'cell.assert_close(1.0,2.0,0.1);' "assert_close"
expect_error nan_assert 'cell.assert_close(sqrt(-1.0),0.0,0.1);' "assert_close"
expect_error assert_true 'cell.assert_true(false);' "assert_true"
expect_error exchange_twice 'let s=cell.sim(1).substrate("o",1.0,0.0,1.0,-1.0); s.cell_type("a").exchange("o",1.0,0.0).exchange("o",0.0,1.0);' "already set"
expect_error edge_side 'cell.sim(1).substrate("o",1.0,0.0,1.0,-1.0).edge("o","left",1.0);' "edge side"
expect_error edge_substrate 'cell.sim(1).edge("o","xmin",1.0);' "unknown edge substrate"
expect_error chemotaxis_bias 'cell.sim(1).cell_type("a").chemotaxis("o",1.5);' "bias"
expect_error chemotaxis_substrate 'let s=cell.sim(1); s.cell_type("a").motility(1.0,1.0).chemotaxis("o",0.5); s.run(1.0,0.1);' "chemotaxis substrate"
expect_error scatter_outside 'let s=cell.sim(1); s.cell_type("a"); s.scatter("a",5,0.0,500.0,0.0,10.0);' "inside the domain"
expect_error record_zero 'cell.sim(1).record(0.0);' "record interval"
expect_error live_of 'cell.sim(1).run(0.0,1.0).live_of("ghost");' "unknown cell type"
expect_error reserved 'cell.sim(1).substrate("neighbors",1.0,0.0,1.0,-1.0).run(1.0,0.1);' "reserved"
ok "errors stop with exit 3 and clear messages (24 cases)"

# Dry run loads/verifies bytecode without executing numerical work or writing CSV.
rm -f build/examples/tissue_decay_profiles.csv
"$HOST" --dry-run --dsl build/tissue_decay.dslb >"$WORK/dry.out" || fail "dry run"
[ ! -e build/examples/tissue_decay_profiles.csv ] || fail "dry run created CSV"
"$HOST" --dsl build/tissue_decay.dslb >/dev/null || fail "restore example CSV"
ok "dry run verifies without writing outputs"

for flags in '--unknown' '--version extra' '--dsl-schema --bogus' '--dry-run' '--dsl --bogus'; do
  set +e
  # Deliberate word splitting: each fixed test string is a CLI argument sequence.
  # shellcheck disable=SC2086
  "$HOST" $flags >/dev/null 2>&1
  code=$?
  set -e
  [ "$code" = 2 ] || fail "usage flags '$flags': exit $code"
done
ok "invalid CLI combinations use exit 2"

set +e; "$HOST" --dsl "$WORK/does-not-exist.dslb" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 3 ] || fail "missing bytecode exit code $code, expected 3"
set +e; "$HOST" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 2 ] || fail "usage exit code $code, expected 2"
ok "usage and load errors use exit codes 2 and 3"

# Memory budget: a runaway loop stops with a run error (exit 3, message on stderr and in the JSON
# line) instead of exhausting the machine. A small budget everywhere; on Linux also the default
# 4096 MiB under a 6 GiB address-space limit (ulimit only here, never in the host). One cheap
# 1e6-step run (one voxel, no cells), then every counts() copy keeps a 16 MB Matrix in the arena.
cat >"$WORK/runaway.lms" <<LMS
p = _dsl_ cli="lmcell";
let r = cell.sim(1).domain(0.0, 20.0, 0.0, 20.0, 20.0).run(100000.0, 0.1);
let i = 0;
while (i < 1000000) {
  let m = r.counts();
  i = i + 1;
}
end _dsl_
savedsl(p, "$WORK/runaway.dslb");
LMS
"$LMSCRIPT" "$WORK/runaway.lms" >/dev/null || fail "runaway.lms compile"
set +e; out="$(LMCELL_MAX_MEMORY_MB=64 "$HOST" --dsl "$WORK/runaway.dslb" 2>"$WORK/runaway.err")"; code=$?; set -e
[ "$code" = 3 ] || fail "runaway loop under LMCELL_MAX_MEMORY_MB=64: exit $code, expected 3 ($out)"
echo "$out" | grep -q "memory budget exceeded" || fail "runaway loop: JSON lacks the budget message: $out"
grep -q "LMCELL_MAX_MEMORY_MB" "$WORK/runaway.err" || fail "runaway loop: no budget message on stderr"
set +e; LMCELL_MAX_MEMORY_MB=-5 "$HOST" --dsl "$WORK/runaway.dslb" >/dev/null 2>"$WORK/badenv.err"; code=$?; set -e
[ "$code" = 2 ] && grep -q "positive whole number" "$WORK/badenv.err" || fail "bad LMCELL_MAX_MEMORY_MB: exit $code"
# Exponential division stops at the budget (checked before each new cell, not inside PhysiCell),
# and a large voxel grid is refused before BioFVM builds it.
expect_budget() { # name, MiB, snippet
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmcell";
$3
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null || fail "$1 compile"
  set +e; out="$(LMCELL_MAX_MEMORY_MB=$2 "$HOST" --dsl "$WORK/$1.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] && echo "$out" | grep -q "memory budget exceeded" || fail "$1 under LMCELL_MAX_MEMORY_MB=$2: exit $code ($out)"
}
expect_budget growth 64 'let s = cell.sim(1); s.cell_type("a").cycle(0.1).radius(1.0); let r = s.seed("a", 1, 0.0, 0.0).run(1000.0, 1.0);'
expect_budget grid 64 'let r = cell.sim(1).domain(0.0, 1000.0, 0.0, 1000.0, 1.0).substrate("o2", 1.0, 0.1, 1.0, -1.0).run(1.0, 1.0);'
if [ "$UNAME_S" = Linux ]; then
  set +e; out="$(ulimit -v 6291456; "$HOST" --dsl "$WORK/runaway.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] && echo "$out" | grep -q "limit 4096 MiB" || fail "runaway loop with the default budget under ulimit -v 6 GiB: exit $code ($out)"
  ok "memory budget: runaway loop stops at 64 MiB and at the default 4096 MiB (under ulimit -v 6 GiB); division growth and grids count"
else
  ok "memory budget: runaway loop stops at LMCELL_MAX_MEMORY_MB=64 (default-budget run is Linux-only); division growth and grids count"
fi

python3 scripts/check-gallery.py --generated
ok "gallery links, image signatures, dimensions and byte budgets"

echo "all $pass checks passed"
