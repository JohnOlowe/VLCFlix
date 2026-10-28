# VLCFlix Studio

VLCFlix Studio is a C++17 desktop application for browsing a local video library. It provides a graphical library, search, details and settings.

## Build

Install a C++17 compiler, `make`, `pkg-config`, SDL2, FreeType, libpng and libjpeg development packages.

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

## Use

```bash
./build/vlcflix doctor
./build/vlcflix scan --folders /path/to/videos
./build/vlcflix status --json
./build/vlcflix ui
./build/vlcflix render --all out/ --size 1600x900
```

Library settings, progress and generated artwork are kept in the application data directory. `--data` or `VLCFLIX_STUDIO_DATA` can select a different data directory. The `samples/` folder contains small media fixtures used by the tests.

## License

The project is distributed under the terms in [LICENSE.txt](LICENSE.txt). Third-party components have their own licenses.
