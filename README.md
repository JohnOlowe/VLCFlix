# VLCFlix Studio

VLCFlix Studio is a C++17 desktop application for browsing a local video library. It provides a graphical library, search, details and settings. Playback uses libVLC in the application window; FFmpeg tools are used for media probing and artwork.

## Build

Install a C++17 compiler, `make`, `pkg-config`, SDL2, FreeType, libpng, libjpeg and libVLC development packages. Install FFmpeg and ffprobe for media probing, preview generation and the end-to-end tests. libVLC decoder plugins are needed for playback.

On Debian-based systems, for example:

```bash
sudo apt install build-essential pkg-config cmake libsdl2-dev libfreetype-dev libpng-dev libjpeg-dev libvlc-dev vlc-plugin-base ffmpeg
```

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
./build/vlcflix play samples/ghostlight.mp4 --wait 5
```

Library settings, progress and generated artwork are kept in the application data directory. `--data` or `VLCFLIX_STUDIO_DATA` can select a different data directory. The `samples/` folder contains small media fixtures used by the tests.

## License

The project is distributed under the terms in [LICENSE.txt](LICENSE.txt). Third-party components have their own licenses.
