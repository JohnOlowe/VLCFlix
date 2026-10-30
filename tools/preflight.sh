#!/usr/bin/env bash
# Static checks: the rules that a compiler on this machine would never notice.
#
# Everything here is a claim that can be checked without a display, a sound card or a
# Windows toolchain - which is exactly why it belongs in the repository rather than in
# someone's head. It is not a substitute for the manual checklist in docs/TESTING.md
# section 9; it is the part of that checklist that a machine can hold to.
#
#   tools/preflight.sh        # exit 0 = all clear, 1 = at least one violation
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

fails=0
checks=0
ok()   { checks=$((checks + 1)); printf '  ok    %s\n' "$1"; }
bad()  { checks=$((checks + 1)); fails=$((fails + 1)); printf '  FAIL  %s\n' "$1"; }
info() { printf '\n== %s\n' "$1"; }

SRC="src"
LIBSRC_DIRS="$SRC/core $SRC/library $SRC/platform $SRC/playback $SRC/render $SRC/ui"

# ---------------------------------------------------------------- build parity
# A source file that is in one build system but not the other is the classic way to
# get "it builds on my machine": the object is silently missing and the link fails
# later, or worse, a stale binary is tested.
info "every source is in both build systems"
missing_cmake=0
missing_make=0
for file in $(find $LIBSRC_DIRS -name '*.cpp' | sort); do
  # CMake and find both use paths relative to the repository root.
  rel="${file}"
  grep -q "$rel" CMakeLists.txt || { bad "CMakeLists.txt is missing $file"; missing_cmake=1; }
  # The Makefile globs the directories, so a file is covered unless it sits outside them.
  case " $LIBSRC_DIRS " in
    *" $(dirname "$file") "*) ;;
    *) bad "Makefile does not compile $file (outside the globbed directories)"; missing_make=1 ;;
  esac
done
[ "$missing_cmake" = 0 ] && ok "CMakeLists.txt lists all $(find $LIBSRC_DIRS -name '*.cpp' | wc -l) library sources"
[ "$missing_make" = 0 ] && ok "the Makefile covers every library source"
test_missing=0
for testfile in $(ls tests/*.cpp | sort); do
  grep -q "${testfile}" CMakeLists.txt || { bad "CMakeLists.txt is missing $testfile"; test_missing=1; }
  grep -q "$(basename "$testfile")" Makefile || { bad "Makefile is missing $testfile"; test_missing=1; }
done
[ "$test_missing" = 0 ] && ok "both build systems compile every test file"

# ------------------------------------------------------- balanced conditionals
# An #if without its #endif compiles fine right up to the platform that is not being
# built here. This counts them.
info "preprocessor conditionals balance"
unbalanced=0
for file in $(find $SRC -name '*.cpp' -o -name '*.h' | sort); do
  opens=$(grep -cE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef)\b' "$file")
  closes=$(grep -cE '^[[:space:]]*#[[:space:]]*endif\b' "$file")
  if [ "$opens" != "$closes" ]; then
    bad "$file: $opens opening directive(s) vs $closes #endif"
    unbalanced=1
  fi
done
[ "$unbalanced" = 0 ] && ok "every #if/#ifdef has its #endif in $(find $SRC -name '*.h' -o -name '*.cpp' | wc -l) files"

# ------------------------------------------------------- the Windows identity
# The application identity is defined in several files and must agree.
info "the Windows application identity"
slug=$(sed -n 's/.*kProductSlug = "\([^"]*\)".*/\1/p' $SRC/core/brand.h | head -1)
product=$(sed -n 's/.*kProductName = "\([^"]*\)".*/\1/p' $SRC/core/brand.h | head -1)
data=$(sed -n 's/.*kDataDirName = "\([^"]*\)".*/\1/p' $SRC/core/brand.h | head -1)
if [ "$slug" = "VLCFlix" ] || [ "$data" = "VLCFlix" ] || [ "$product" = "VLCFlix" ]; then
  bad "the product identity uses a reserved name ($product / $slug / $data)"
else
  ok "product '$product', slug '$slug', data dir '$data' - set for this application"
fi
# The installer is src/setup/setup.cpp: one self-contained .exe that unpacks the
# payload, makes the shortcuts and writes the uninstall entry. It replaced an NSIS
# draft, so the checks moved with it - same invariants, checked in the real thing.
SETUP=src/setup/setup.cpp
if [ ! -f $SETUP ]; then
  bad "$SETUP is missing"
else
  grep -qF "constexpr const wchar_t* kExeName = L\"VLCFlixStudio.exe\";" $SETUP \
    && ok "installer executable is VLCFlixStudio.exe" \
    || bad "installer executable name is not the product's"
  grep -qF 'return home_dir() + L"\\Programs\\VLCFlixStudio";' "$SETUP" \
    && ok "install directory is %LOCALAPPDATA%\\Programs\\VLCFlixStudio" \
    || bad "the default install directory is not VLCFlixStudio"
  grep -qF 'Uninstall\\VLCFlixStudio"' $SETUP \
    && ok "uninstall key is the application's own (…\\Uninstall\\VLCFlixStudio)" \
    || bad "uninstall key is not the application's"
  grep -qF 'constexpr const wchar_t* kProtectedDirName = L"VLCFlix";' $SETUP \
    && ok "the installer names a protected directory" \
    || bad "the installer does not protect the reserved directory"
  grep -qF 'Refusing to install into' $SETUP \
    && ok "installing into the protected folder is refused" \
    || bad "the installer would use the protected folder"
  grep -qF 'Uninstall\\VLCFlixStudio"' $SETUP \
    && ok "the application uses its own uninstall key" || true
  if [ -f packaging/app.manifest ]; then
    grep -q 'VLCFlixStudio' packaging/app.manifest \
      && ok "the manifest's assembly identity is the product's" \
      || bad "the manifest assembly identity does not match"
  else
    bad "packaging/app.manifest is missing"
  fi
  grep -q 'vlcflix.ico' scripts/build-win-native.sh \
    && ok "the build gives the app and the installer the product icon" \
    || bad "the build does not attach the product icon"
fi

# A manifest that Windows' side-by-side parser rejects stops the process before main(),
# so it cannot be caught by running the app - and Wine does not validate manifests at
# all. This is the check that does, plus a negative control proving it can fail.
if python3 scripts/check-win-manifest.py packaging/app.manifest >/dev/null 2>&1; then
  ok "the application manifest is schema-clean and has no dependent assemblies"
else
  bad "packaging/app.manifest is not valid (Windows would refuse to start the exe)"
fi
negative_manifest=$(mktemp)
sed 's|name="VLCFlixStudio.Desktop"|name="VLCFlixStudio.Desktop" description="not a schema attribute"|' \
  packaging/app.manifest > "$negative_manifest"
if python3 scripts/check-win-manifest.py "$negative_manifest" >/dev/null 2>&1; then
  bad "the manifest check accepted a description attribute on assemblyIdentity"
else
  ok "the manifest check rejects an out-of-schema attribute (negative control)"
fi
rm -f "$negative_manifest"
grep -q 'check-win-manifest.py' scripts/build-win-native.sh \
  && ok "the Windows build refuses to ship a manifest that fails that check" \
  || bad "scripts/build-win-native.sh does not run the manifest check"

# --------------------------------------------------------------- house rules
# From AGENTS.md: main.cpp must not include SDL headers (the window layer owns SDL),
# and credentials must never be committed.
info "the rules in AGENTS.md"
if grep -qE '#include *<SDL' $SRC/main.cpp; then
  bad "main.cpp includes SDL headers directly"
else
  ok "main.cpp does not include SDL headers"
fi
tracked_secrets=$(git ls-files | grep -E '(^|/)(\.secrets|\.netrc|\.git-credentials|id_rsa|id_ed25519)' || true)
[ -z "$tracked_secrets" ] && ok "no credential files are tracked" \
                          || bad "credential files are tracked: $tracked_secrets"
big=$(git ls-files -z | xargs -0 -r du -k 2>/dev/null | awk '$1 > 5120 {print $2}')
[ -z "$big" ] && ok "no tracked file is larger than 5 MB" \
              || bad "tracked files over 5 MB: $(echo "$big" | tr '\n' ' ')"
# A realistic token, not the word "ghp_" in prose: these prefixes appear in this
# repository's own documentation about scrubbing them, which is not a leak.
leak=$(git grep -nE '(ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{25,}|AKIA[0-9A-Z]{16})' -- \
        '*.cpp' '*.h' '*.sh' '*.md' '*.json' '*.nsi' '*.txt' 2>/dev/null || true)
[ -z "$leak" ] && ok "no token-shaped strings in the tracked tree" \
               || bad "a token-shaped string is tracked (value suppressed)"

# ------------------------------------------------------------ documentation
# docs/TESTING.md promises specific commands; a promise that names a target which does
# not exist is worse than no promise.
info "the documentation's promises exist"
for target in verify test asan; do
  grep -qE "^$target:" Makefile && ok "make $target exists" || bad "make $target is promised but missing"
done
[ -x tests/journey.sh ] && ok "tests/journey.sh is executable" || bad "tests/journey.sh is not executable"
for script in scripts/build-win-native.sh scripts/check-win-payload.sh; do
  [ -x "$script" ] && ok "$script is executable" || bad "$script is not executable"
done
for file in scripts/windows-selfcheck.ps1 src/setup/setup.cpp \
            packaging/vlcflix.ico packaging/app.manifest docs/ci/windows.yml; do
  [ -f "$file" ] && ok "$file exists" || bad "$file is missing"
done
# The workflow is kept out of .github/workflows only because this session's token has
# no `workflow` scope; if it was moved there, it must not still be advertised as
# something the user has to copy.
if [ -f .github/workflows/windows.yml ]; then
  ok "the Windows workflow is live in .github/workflows"
elif [ -f docs/ci/windows.yml ]; then
  ok "the Windows workflow is staged in docs/ci (needs the workflow scope to go live)"
fi
for screen in home browse mylist search detail settings empty; do
  [ -f "tests/golden/$screen.png" ] || bad "golden baseline for '$screen' is missing"
done
ok "7 golden baselines present"

printf '\n%d check(s), %d failure(s)\n' "$checks" "$fails"
[ "$fails" = 0 ] || exit 1
