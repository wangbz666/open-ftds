# Changelog

## 0.2.1

- Keep the configured output directory in collector-owned storage after init.
- Use `pthread_atfork` to make the common instrumentation path free of `getpid()` calls.
- Make the initialized-state fast path atomic and avoid its lifecycle mutex.
- Preserve the PID-based recovery path when `pthread_atfork` is unavailable.

## 0.2.0

- Detect PID changes and lazily rebuild process-local state after fork.
- Add runtime enable/disable, minimum-level control and 1/N sampling.
- Add slow-operation counters and report sorting.
- Add a process diagnostic status API.
- Add an explicit C++20 header/link test.
- Add YRCache torch `BuildExtension` and CMake integration helpers.
- Add fork, runtime-control, sampling and slow-I/O tests.

## 0.1.0

- Initial C11/C++ latency aggregation library and FTDS-style source layer.
