# lmcell durable memory

`lmcell` is a static, dependency-free lmscript `_dsl_` host for cellular automata, tissue simulation, reaction-diffusion, and spatial biology.
Repository: https://github.com/lmctl-ai/lmcell.

## 60-second takeover

```sh
cmake -S . -B build                   # Configure CMake build
cmake --build build --parallel 4      # Build binary at build/lmcell
./test.sh                             # Run verification test suite
```

## Document map

| File | Contents |
| --- | --- |
| [01-purpose-and-place.md](01-purpose-and-place.md) | Architectural role, simulation capabilities, and host domain |
| [02-libraries-and-licenses.md](02-libraries-and-licenses.md) | Embedded libraries, third-party licenses, and clean-room implementation |
| [03-schema-v0.md](03-schema-v0.md) | Cellular models, grid types, and DSL schema definitions |
| [04-numerics.md](04-numerics.md) | Discretization schemes, stability, grid updates, and numerical invariants |
| [05-build-run-test.md](05-build-run-test.md) | Build commands, test suites, and determinism benchmarks |
| [06-ci.md](06-ci.md) | CI platform matrix (5 platforms) and build workflows |
| [07-examples.md](07-examples.md) | Cellular automata examples, Gray-Scott reaction-diffusion models |
| [08-open-items.md](08-open-items.md) | Open items, future extensions, and roadmap |
| [09-visual-gallery.md](09-visual-gallery.md) | Visual outputs, PNG rendering, and simulation gallery |
| [10-resource-limits.md](10-resource-limits.md) | Grid size boundaries, memory budget, and compute limits |
| [next-release-version.md](next-release-version.md) | Version alignment protocol (VERSION, CMake, THIRD_PARTY_NOTICES) |

## SDK pin and sync

The vendored SDK header is pinned via `sdk.lock` pointing to `https://github.com/lmctl-ai/lmcode-cpp`:
```sh
scripts/sync-sdk.sh                   # Refetches pinned cpp/dslvm.hpp and verifies sha256
```

## Adding an example or function

### Adding an example
1. Create `examples/<name>.lms` using `_dsl_ cli="lmcell" install="github:lmctl-ai/lmcell@latest"`.
2. Run script: `build/lmcell run examples/<name>.lms` or via `lmscript`.
3. Add the example to `./test.sh` test run list.
4. Save visual output to `docs/images/<name>.png` and update `README.md` demo table with `lmctl autorun` command.

### Adding a host function
1. Declare function/method in `schema/lmcell.dsl.json`.
2. Register and implement the function in C++ source under `src/` using `dslvm::Registry`.
3. Rebuild and verify schema synchronization with `cmake --build build` and `./test.sh`.

## Releases

Releases are triggered automatically on push of a `v*` tag. The release workflow builds across Linux x64/arm64, macOS x64/arm64, and Windows x64, publishing platform binaries with `SHA256SUMS`, `LICENSE`, and `THIRD_PARTY_NOTICES`. See `next-release-version.md` for version bump guidelines before tagging.
