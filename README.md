# VLCFlix Studio

VLCFlix Studio is a C++17 application for organizing a local video library.

## Build

Install a C++17 compiler and `make`; CMake is also supported.

```bash
make -j2
make test
```

Or use CMake:

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

## License

The project is distributed under the terms in [LICENSE.txt](LICENSE.txt). Third-party components have their own licenses.
