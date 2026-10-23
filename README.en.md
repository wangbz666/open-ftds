# OpenFTDS

OpenFTDS is a small, dependency-free latency instrumentation library for C and
C++ storage and performance-sensitive services. It offers begin/end probes,
stable event IDs, interval aggregation, rotating `.ios` files, and an offline
reporter for count, errors, average latency, min/max, and approximate
P50/P95/P99 latency.

Version 0.2 adds lazy post-fork recovery for worker processes, runtime enable
and level controls, 1/N sampling, slow-operation counters, and a C++20 build
check for torch-extension integration. Version 0.2.1 keeps the configured path
in collector-owned storage and removes `getpid()` plus the lifecycle mutex from
the common instrumentation path.

Its compatibility layer supplies the public `ftds.h`, `initFtdsClient`,
`FTDS_DELAY_START/END`, and `-lftdsclient` usage pattern. Both precomputed
nanosecond durations and library-timed tokens are supported.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/openftds-example
./tools/openftds-report ./demo-output
```

With only a system compiler and Make:

```bash
make test
make example
```

See [README.md](README.md) for the complete API, output format, and design notes.
For air-gapped delivery, start with [OFFLINE_DELIVERY.md](OFFLINE_DELIVERY.md)
and [AI_INTEGRATION_GUIDE.md](AI_INTEGRATION_GUIDE.md).
