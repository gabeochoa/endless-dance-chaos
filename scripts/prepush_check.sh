#!/bin/bash
# Mandatory pre-push gate (Gabe 2026-10-18): fresh-clone build+e2e, clang-21
# syntax check, submodule hygiene. Run BEFORE every push; report must include
# "fresh-clone ✓ clang21 ✓". Usage: scripts/prepush_check.sh
set -u
REPO=$(basename "$(git rev-parse --show-toplevel)")
export PATH="$HOME/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="$HOME/homebrew/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
FAIL=0
echo "== submodule hygiene =="
git submodule status | grep -v '^ ' && { echo "SUBMODULE DIRTY"; FAIL=1; }
echo "== fresh-clone build + e2e =="
rm -rf "/tmp/$REPO-check"
git clone "https://github.com/gabeochoa/$REPO" "/tmp/$REPO-check" >/dev/null 2>&1 || { echo "CLONE FAIL"; exit 1; }
cd "/tmp/$REPO-check" && git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive >/dev/null 2>&1 || { echo "SUBMODULE INIT FAIL"; exit 1; }
if [ "$REPO" = "endless-dance-chaos" ]; then
  make -j4 >/tmp/prepush_build.log 2>&1 && perl -e 'alarm 1500; exec @ARGV' ./output/dance.exe --test-mode --headless --test-script-dir tests/e2e >/tmp/prepush_e2e.log 2>&1 || FAIL=1
else
  make -j4 >/tmp/prepush_build.log 2>&1 && perl -e 'alarm 300; exec @ARGV' make test-headless >/tmp/prepush_e2e.log 2>&1 || FAIL=1
fi
grep -E "Scripts failed: 0|E2E tests PASSED" /tmp/prepush_e2e.log >/dev/null || FAIL=1
echo "== clang 21 syntax check =="
CLANG21=$(ls ~/homebrew/Cellar/llvm@21/*/bin/clang++ ~/homebrew/opt/llvm/bin/clang++ /opt/homebrew/opt/llvm/bin/clang++ 2>/dev/null | head -n 1)
if [ -n "$CLANG21" ]; then
  RAY=$(pkg-config --cflags raylib 2>/dev/null); for f in src/*.cpp src/*/*.cpp; do [ -f "$f" ] || continue; $CLANG21 -std=c++23 -fsyntax-only -DAFTER_HOURS_ENABLE_E2E_TESTING $RAY -Ivendor/ -Isrc/ "$f" >/dev/null 2>/tmp/prepush_clang.log || { echo "CLANG21 FAIL: $f"; head -n 5 /tmp/prepush_clang.log; FAIL=1; }; done
else echo "no brew llvm21 found — clang21 check SKIPPED"; fi
[ $FAIL -eq 0 ] && echo "fresh-clone ✓ clang21 ✓" || echo "PREPUSH GATE FAILED"
exit $FAIL
