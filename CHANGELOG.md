# Changelog

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
