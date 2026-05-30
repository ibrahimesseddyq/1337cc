#!/usr/bin/env bash
# run_tests.sh — Test runner for the 1337cc LLVM IR backend.
#
# For each test/NNN_name.c file:
#   1. Compiles with ./1337cc to produce /tmp/1337cc_test/NNN_name.ll
#   2. Links the .ll with clang to produce /tmp/1337cc_test/NNN_name
#   3. Runs the binary and captures stdout
#   4. Compares output to tests/NNN_name.expected
#
# Usage: bash test/run_tests.sh [pattern]
#   pattern — optional glob to select tests (e.g. "01_*")

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
COMPILER="$REPO_DIR/1337cc"
TESTS_DIR="$SCRIPT_DIR"
TMP_DIR="/tmp/1337cc_test"

mkdir -p "$TMP_DIR"

# Colours (suppress if not a tty)
if [ -t 1 ]; then
    GREEN='\033[0;32m'
    RED='\033[0;31m'
    YELLOW='\033[1;33m'
    RESET='\033[0m'
else
    GREEN='' RED='' YELLOW='' RESET=''
fi

PASS=0
FAIL=0
SKIP=0

PATTERN="${1:-*.c}"

for c_file in "$TESTS_DIR"/$PATTERN; do
    [ -f "$c_file" ] || continue

    base="$(basename "$c_file" .c)"
    expected_file="$TESTS_DIR/${base}.expected"

    if [ ! -f "$expected_file" ]; then
        echo -e "${YELLOW}SKIP${RESET}  $base  (no .expected file)"
        SKIP=$((SKIP + 1))
        continue
    fi

    ll_file="$TMP_DIR/${base}.ll"
    bin_file="$TMP_DIR/${base}"
    actual_file="$TMP_DIR/${base}.actual"

    # Step 1: compile C → LLVM IR
    if ! "$COMPILER" "$c_file" "$ll_file" 2>"$TMP_DIR/${base}.compile_err"; then
        echo -e "${RED}FAIL${RESET}  $base  (compiler error)"
        cat "$TMP_DIR/${base}.compile_err" >&2
        FAIL=$((FAIL + 1))
        continue
    fi

    # Step 2: link LLVM IR → native binary (llc → .o, then gcc links)
    s_file="$TMP_DIR/${base}.s"
    o_file="$TMP_DIR/${base}.o"
    if ! llc --relocation-model=pic "$ll_file" -o "$s_file" 2>"$TMP_DIR/${base}.link_err" ||
       ! gcc "$s_file" -o "$bin_file" 2>>"$TMP_DIR/${base}.link_err"; then
        echo -e "${RED}FAIL${RESET}  $base  (link error)"
        cat "$TMP_DIR/${base}.link_err" >&2
        FAIL=$((FAIL + 1))
        continue
    fi

    # Step 3: run and capture output
    if ! "$bin_file" >"$actual_file" 2>&1; then
        echo -e "${RED}FAIL${RESET}  $base  (runtime error, exit code $?)"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Step 4: compare output
    if diff -q "$expected_file" "$actual_file" >/dev/null 2>&1; then
        echo -e "$actual_file"
        echo -e "${GREEN}PASS${RESET}  $base"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${RESET}  $base  (output mismatch)"
        echo "  Expected:"
        sed 's/^/    /' "$expected_file"
        echo "  Got:"
        sed 's/^/    /' "$actual_file"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed, ${SKIP} skipped"

[ "$FAIL" -eq 0 ]
