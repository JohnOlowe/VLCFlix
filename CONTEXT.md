# Project context

## Project

VLCFlix Studio is a C++17 desktop media-library application. Sources, tests,
packaging, and build files live at the repository root. The application uses
libVLC for in-window playback and FFmpeg/ffprobe for media processing. Settings,
progress, artwork and logs are kept in the user's application-data directories.

## Development

- Use the `dev` branch for this work. Read `AGENTS.md`, this file and `TURN_LOG.md`
  before continuing.
- Build with `make` or CMake. `make verify` runs static checks, C++ tests and the
  end-to-end journey; see `docs/TESTING.md` for prerequisites and manual checks.
- The Windows setup recipe is `scripts/build-win-native.sh --setup`. It writes the
  gitignored `releases/VLCFlixStudio-Setup.exe`, keeping only the latest handout.
  Do not commit executables or credentials, and never put a token in a remote URL.
- Compare UI changes against actual observed screenshots, including content below
  the fold. Generated imagery is not evidence of application behaviour.

## Items requiring user confirmation

The latest Windows build has not been confirmed by the user. In particular, do not
claim that video colour or smoothness, playback-mode switching, hardware-setting
persistence, first-hover responsiveness, the detail preview, in-window VLC playback,
or preview looping work on their machine without a new test.

The user confirmed two-finger scrolling on an earlier build. Scroll speed was still
reported too fast, including at 1%; the later 0.2% minimum has not been accepted.
Keep the visible Home scrollbar and the in-app sensitivity control. Do not treat a
mouse-wheel-only result as proof of two-finger touchpad behaviour.

Keep user library data safe during installation and uninstall. Preserve the installer
folder-protection guard and do not create or distribute a new installer unless needed.
