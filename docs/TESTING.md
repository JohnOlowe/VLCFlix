# Testing

The project has complementary checks; a successful compilation alone does not prove
that playback, input devices or installation work on a particular machine.

- `make test` builds and runs the C++ tests, including UI, image and fault checks.
- `make preflight` checks source/build parity, packaging and tracked-file hygiene.
- `make verify` runs preflight, the C++ tests and the end-to-end journey.
- `make asan` runs the tests with AddressSanitizer.
- `cmake -S . -B build && cmake --build build && ctest --test-dir build` exercises
  the CMake build and its registered tests.

The journey uses `ffmpeg` to create temporary media. Playback tests need libVLC and
its decoder plugins, and the image comparisons use the committed `tests/golden/`
baselines. Tests can write temporary logs and screenshots outside the repository.
Do not update golden files without reviewing the rendered screens.

For Windows, also run the installer and application on an actual target machine and
check the window, input, playback, audio, hardware decoding and uninstall behaviour.
Wine and headless tests are useful checks, not substitutes for that confirmation.
