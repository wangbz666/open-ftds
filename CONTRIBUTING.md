# Contributing

Build with warnings enabled and run all tests before submitting a change:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Keep the core C11 library dependency-free. Format changes must be documented in
`docs/format.md` and remain readable by older tools whenever practical.
