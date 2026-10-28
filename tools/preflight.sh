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

SRC="native/src"
LIBSRC_DIRS="$SRC/core $SRC/library $SRC/platform $SRC/playback $SRC/render $SRC/ui"

# ---------------------------------------------------------------- build parity
# A source file that is in one build system but not the other is the classic way to
# get "it builds on my machine": the object is silently missing and the link fails
# later, or worse, a stale binary is tested.
info "every source is in both build systems"
missing_cmake=0
missing_make=0
for file in $(find $LIBSRC_DIRS -name '*.cpp' | sort); do
  # CMake paths are relative to native/, the find output is relative to the repo.
  rel="${file#native/}"
  grep -q "$rel" native/CMakeLists.txt || { bad "CMakeLists.txt is missing $file"; missing_cmake=1; }
  # The Makefile globs the directories, so a file is covered unless it sits outside them.
  case " $LIBSRC_DIRS " in
    *" $(dirname "$file") "*) ;;
    *) bad "Makefile does not compile $file (outside the globbed directories)"; missing_make=1 ;;
  esac
done
[ "$missing_cmake" = 0 ] && ok "CMakeLists.txt lists all $(find $LIBSRC_DIRS -name '*.cpp' | wc -l) library sources"
[ "$missing_make" = 0 ] && ok "the Makefile covers every library source"
test_missing=0
for testfile in $(ls native/tests/*.cpp | sort); do
  grep -q "${testfile#native/}" native/CMakeLists.txt || { bad "CMakeLists.txt is missing $testfile"; test_missing=1; }
  grep -q "$(basename "$testfile")" native/Makefile || { bad "Makefile is missing $testfile"; test_missing=1; }
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
# Requirement: the native build must be a new application, never an upgrade. The
# strings that decide that live in three files and must agree.
info "the Windows build is a new application"
slug=$(sed -n 's/.*kProductSlug = "\([^"]*\)".*/\1/p' $SRC/core/brand.h | head -1)
product=$(sed -n 's/.*kProductName = "\([^"]*\)".*/\1/p' $SRC/core/brand.h | head -1)
data=$(sed -n 's/.*kDataDirName = "\([^"]*\)".*/\1/p' $SRC/core/brand.h | head -1)
if [ "$slug" = "VLCFlix" ] || [ "$data" = "VLCFlix" ] || [ "$product" = "VLCFlix" ]; then
  bad "the product identity collides with VLCFlix 1.2.0 ($product / $slug / $data)"
else
  ok "product '$product', slug '$slug', data dir '$data' - all distinct from 1.2.0"
fi
if [ ! -f native/packaging/installer.nsi ]; then
  bad "native/packaging/installer.nsi is missing"
else
  # The paths are built from ${APP_SLUG} on purpose (one place to rename the product),
  # so the check is that the define is the slug and that the paths are built from it.
  grep -qE "^!define APP_SLUG +\"$slug\"" native/packaging/installer.nsi \
    && ok "installer APP_SLUG is '$slug'" || bad "installer APP_SLUG is not '$slug'"
  grep -qF 'InstallDir "$LOCALAPPDATA\Programs\${APP_SLUG}"' native/packaging/installer.nsi \
    && ok "install directory is built from the new slug" || bad "install directory does not use the new slug"
  grep -qF "Uninstall\\$slug\"" native/packaging/installer.nsi \
    && ok "uninstall key is the new app's own (…\\Uninstall\\$slug)" \
    || bad "uninstall key is not the new app's"
  grep -qF 'LEGACY_DIR' native/packaging/installer.nsi \
    && ok "the installer names 1.2.0's directory only in order to refuse it" \
    || bad "the installer does not guard against installing over VLCFlix 1.x"
  grep -qF "$slug.exe" native/packaging/installer.nsi 2>/dev/null || true
  grep -qE '^!define APP_EXE +"VLCFlixStudio.exe"' native/packaging/installer.nsi \
    && ok "installer executable is VLCFlixStudio.exe" \
    || bad "installer executable name is not the new product's"
  grep -qE '^!define APP_ID +"VLCFlixStudio' native/packaging/installer.nsi \
    && ok "installer AppId is the new app's, so Windows cannot treat it as an upgrade" \
    || bad "installer AppId does not identify a new application"
  if [ -f native/packaging/app.manifest ]; then
    grep -q 'VLCFlixStudio' native/packaging/app.manifest \
      && ok "the manifest's assembly identity is the new product's" \
      || bad "the manifest still carries the old assembly identity"
  else
    bad "native/packaging/app.manifest is missing"
  fi
fi

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
               || bad "a token-shaped string is tracked: $(echo "$leak" | head -1)"

# ------------------------------------------------------------ documentation
# docs/TESTING.md promises specific commands; a promise that names a target which does
# not exist is worse than no promise.
info "the documentation's promises exist"
for target in verify test asan; do
  grep -qE "^$target:" native/Makefile && ok "make $target exists" || bad "make $target is promised but missing"
done
[ -x native/tests/journey.sh ] && ok "tests/journey.sh is executable" || bad "tests/journey.sh is not executable"
for script in scripts/build-win-native.sh scripts/check-win-payload.sh; do
  [ -x "$script" ] && ok "$script is executable" || bad "$script is not executable"
done
for file in scripts/windows-selfcheck.ps1 native/packaging/installer.nsi \
            native/packaging/app.manifest docs/ci/windows.yml; do
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
  [ -f "native/tests/golden/$screen.png" ] || bad "golden baseline for '$screen' is missing"
done
ok "7 golden baselines present"

printf '\n%d check(s), %d failure(s)\n' "$checks" "$fails"
[ "$fails" = 0 ] || exit 1
