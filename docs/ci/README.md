# Running the Windows build on GitHub Actions

`windows.yml` in this folder is a complete workflow that builds **and runs** the
Windows application on a real `windows-latest` machine, then publishes the exe and the
evidence to a GitHub release.

It is not in `.github/workflows/` yet for one boring reason: the fine-grained token
used in this session has *Contents: read/write* but not *Workflows: write*, so GitHub
refuses the push that adds a workflow file:

```
refusing to allow a Personal Access Token to create or update workflow
`.github/workflows/windows.yml` without `workflow` scope
```

Two ways to turn it on, both one step:

**A. Give the token the scope, then let the agent push it.**

Fine-grained PAT → *Repository permissions* → **Workflows: Read and write** → save.
Classic PAT → tick **workflow**. Send the token; the agent copies this file into
`.github/workflows/` and pushes. From then on every push to `cpp` builds on Windows.

**B. Do it in the browser in two minutes.**

1. Open <https://github.com/JohnOlowe/VLCFlix/new/cpp/.github/workflows> (create file).
2. Name it `windows.yml`.
3. Paste the contents of this folder's `windows.yml`, commit.

Either way the first run produces a release called **Windows build N** with:

* `VLCFlixStudio-win64.zip` — the application, built by MSYS2's own mingw-w64 GCC
  (a different toolchain from the cross-build in `scripts/build-win-native.sh`, which
  is a feature: two independent builds of the same source agreeing is evidence);
* `windows-evidence.zip` — the screenshots the app drew on Windows, the scan output,
  the `status --json` document, the test-suite log, and the app's own log file.

The release only appears if the build succeeds, and the smoke step's outcome is
written into the release notes, so an empty release list is itself a signal.

## Why bother when a build already exists

A cross-compiled exe proves the source compiles for Windows. Only running it proves
the app opens a window, loads libVLC's plugins and decodes a frame on Windows. That is
the difference this workflow closes, and it is why the release notes on
`win64-cross-2` say "not verified: never executed".
