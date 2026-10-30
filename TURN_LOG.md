# Development log

## 2026-10-30 — root layout verification

- Verified the root C++ layout, executable script permissions, ignored releases and
  credential paths, and the protected installer destination.
- Added the touchpad implementation to the CMake source list. Corrected golden-image
  discovery when tests run from a build directory, and made missing baselines fail
  instead of being silently generated.
- Linux `make verify`: 33 static checks, 707 C++ checks and all 15 journey steps
  passed. CMake built successfully and both CTest tests passed; AddressSanitizer
  passed 707 checks. A missing-baseline negative control failed as expected without
  creating images.
- The Windows installer source passed a cross-compiler syntax check; no installer was
  built in this verification. Current Windows behaviour remains to be confirmed on the
  user's machine.
