# N1 validation

## Current validated WSL baseline

Environment reported by the WSL validation run:

- Qt 6.11.2
- GCC 16.2.1
- CMake/Ninja
- branch: `native/n1-foundation`

Observed domain benchmark results before CI packaging work:

- build 500 physical instances: ~0.094 ms/iteration
- move one physical instance in a 500-copy order: ~0.045 ms/iteration

These numbers are development-machine observations, not release guarantees.

## Required local WSL gate

```bash
git pull --ff-only
cmake --preset wsl-debug
cmake --build --preset wsl-debug --parallel
ctest --preset wsl-debug
./build/wsl-debug/tcgprint_benchmarks
./build/wsl-debug/tcgprint
```

Expected:

- all tests pass;
- benchmark executable completes;
- QML window opens;
- startup log reports the persistent log path and `Startup QML ready in ... ms`.

## Automated CI gate

The `native-ci` workflow builds/tests:

- Linux / GCC / Qt 6.12;
- Windows / MSVC 2022 / Qt 6.12.

The Windows job additionally runs `windeployqt` and uploads a portable `TCGPrint-Native-win64` artifact.

## N1 still not complete until

- the Windows CI job is green;
- the uploaded portable artifact launches on Windows without the development environment;
- cold/warm startup and idle-memory measurements are recorded;
- an empty compositor scene/frame-pacing baseline is recorded.

N2 implementation may be prepared in parallel, but N1 should not be marked complete until these release-path checks are evidenced.
