#!/usr/bin/env bash
# local-ci.sh — mirrors the three cheapest CI checks exactly.
# Run before every push to a feature branch.
#
# Usage:  bash scripts/local-ci.sh [--fix]
#   --fix   auto-apply clang-format (instead of dry-run)

set -euo pipefail

FIX=0
for arg in "$@"; do
  [[ "$arg" == "--fix" ]] && FIX=1
done

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS=0; FAIL=0

step() { echo -e "\n${YELLOW}▶ $*${NC}"; }
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; PASS=$((PASS + 1)); }
fail() { echo -e "${RED}  ✗ $*${NC}"; FAIL=$((FAIL + 1)); }

# ── 1. clang-format ───────────────────────────────────────────────────────────
step "clang-format (clang-format-18)"
if ! command -v clang-format-18 &>/dev/null; then
    fail "clang-format-18 not found — install: sudo apt-get install clang-format-18"
else
    CPP_FILES=$(find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp')
    if [[ "$FIX" == "1" ]]; then
        echo "$CPP_FILES" | xargs clang-format-18 -i
        ok "clang-format applied (--fix mode)"
    else
        if echo "$CPP_FILES" | xargs clang-format-18 --dry-run --Werror 2>&1; then
            ok "clang-format clean"
        else
            fail "clang-format violations — run: bash scripts/local-ci.sh --fix"
        fi
    fi
fi

# ── 2. clang-tidy (changed files only, mirrors CI) ───────────────────────────
step "clang-tidy (changed files vs origin/main)"
if ! command -v clang-tidy-18 &>/dev/null; then
    fail "clang-tidy-18 not found — install: sudo apt-get install clang-tidy-18"
else
    # Prefer build_tidy; fall back to any build dir that has compile_commands.json.
    # Must be a fully-built tree (FetchContent deps downloaded) for correct results.
    TIDY_BUILD=""
    for d in build_tidy build build_san; do
        if [[ -f "$d/compile_commands.json" ]]; then
            TIDY_BUILD="$d"; break
        fi
    done
    if [[ -z "$TIDY_BUILD" ]]; then
        fail "clang-tidy: no compile_commands.json found — run: cmake -B build_tidy -DOMEGA_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build_tidy"
    else
        # Pass only .cpp/.hpp files: headers analysed standalone give false positives
        # (no include-path context without a TU); real header violations surface
        # when their including TUs are processed.
        CHANGED=$(git diff --name-only origin/main...HEAD 2>/dev/null \
          | grep -E '\.(cpp|hpp)$' || true)
        if [[ -z "$CHANGED" ]]; then
            ok "clang-tidy: no C++ files changed vs origin/main"
        elif echo "$CHANGED" | xargs clang-tidy-18 -p "$TIDY_BUILD" --warnings-as-errors='*' 2>&1; then
            ok "clang-tidy clean (using $TIDY_BUILD)"
        else
            fail "clang-tidy violations (see above)"
        fi
    fi
fi

# ── 3. CHANGELOG lint ─────────────────────────────────────────────────────────
step "CHANGELOG lint"
# Get the current branch's likely PR title from the most recent commit subject
LAST_SUBJECT=$(git log -1 --pretty=%s 2>/dev/null || echo "")
# Check if branch has any CHANGELOG modification vs main
CHANGELOG_CHANGED=$(git diff --name-only origin/main...HEAD 2>/dev/null \
  | grep "CHANGELOG.md" || true)

if echo "$LAST_SUBJECT" | grep -qE "^(ci|chore|docs):"; then
    ok "CHANGELOG: skipped (last commit is ci:/chore:/docs:)"
elif [[ -n "$CHANGELOG_CHANGED" ]]; then
    ok "CHANGELOG.md updated in this branch"
else
    fail "CHANGELOG.md not updated — add an entry under [Unreleased], or use ci:/chore:/docs: commit prefix to skip"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
if [[ "$FAIL" -eq 0 ]]; then
    echo -e "${GREEN}All $PASS checks passed — safe to push.${NC}"
else
    echo -e "${RED}$FAIL check(s) failed, $PASS passed — fix before pushing.${NC}"
    exit 1
fi
