# Build and measured verification

On x86_64 Linux with GCC 11, Release produces a fully static stripped executable of about 3.6 MB. The selected upstream library has 28 translation units; no OpenMP runtime, XML configuration, model datasets or dynamic numerical libraries are required. CMake builds the dependency and host together. Run scripts/sync-deps.sh first; SDK content and dependency commit/cleanliness are checked by test.sh.

The test harness has 17 grouped checks: pins, build, twelve native scenarios, exact version, generated/embedded/registry/compiler schema parity, notices, static linkage, eight compiled/executed DSL programs (stdout must be exactly the JSON result), four deterministic CSV exports, 15 runtime error snippets, dry run and usage/load/run exit codes. The twelve Release native scenarios take roughly nine seconds locally (two parallel jobs). Statistical oracle measurements are recorded in 04-numerics.md. Native checks remain active with NDEBUG.

All upstream sources and our wrapper/tests were compiled with AddressSanitizer and UndefinedBehaviorSanitizer; all nine scenarios passed with leak detection and halt-on-UB enabled after the documented initialization and explicit substrate-binding repairs. The fully instrumented native suite takes about 22 seconds. Clang static analysis of the wrapper, shellcheck and Python compilation also pass. These local checks complement the required five-platform CI rather than replacing it.

The test harness puts build/ first on PATH before compiling programs and detects duplicate basenames. Native and bytecode scratch/output files stay under build/ or temporary directories. Each stochastic scenario runs in its own process, respecting global-state ownership. All fixtures are original MIT material; no upstream nonpermissive conformance data is shipped.

Successful main CI build-and-test phase times (seconds, including native and DSL tests): Linux x64 22, Linux arm64 56, macOS arm64 30, macOS x64 90, Windows x64 65. These are measured workflow phase times, not local clean-build claims. Released Linux x64 is 3,509,360 bytes; local GCC 11 static output is 3,606,168 bytes.
