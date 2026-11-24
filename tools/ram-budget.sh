#!/usr/bin/env bash
# Static RAM + flash budget gate for every firmware target (AGENTS.md R9). Run after a build.
#
#   tools/ram-budget.sh [build-dir]                     # default: build
#   RAM_BUDGET_CONF=/tmp/low.conf tools/ram-budget.sh   # control runs only
#
# For each line of tools/ram-budget.conf, measures build/<elf>.elf with arm-none-eabi-size:
#   RAM   = data + bss   (static RAM incl. the linker's reserved heap/stack block)
#   FLASH = text + data
# Exit 1 = a target is over budget. Exit 2 = could not measure (missing ELF / tool) — that
# is NOT a pass. The linker only fails at 100% of RAM; on the F051 (8 KB) the stacks of
# main and every ISR live in what is left, so the budget stops growth well before that.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-build}"
[ -d "$BUILD" ] || BUILD="$ROOT/$BUILD"
CONF="${RAM_BUDGET_CONF:-$ROOT/tools/ram-budget.conf}"
SIZE="${SIZE:-arm-none-eabi-size}"

die() { echo "ram-budget: $* — NOT measured (this is NOT a pass)" >&2; exit 2; }
command -v "$SIZE" >/dev/null || die "$SIZE not found (run inside the Dockerfile.ci image)"
[ -f "$CONF" ] || die "$CONF missing"

RC=0; N=0
while read -r ELF MAXRAM MAXFLASH _; do
    case "$ELF" in ''|\#*) continue ;; esac
    F="$BUILD/$ELF.elf"
    [ -f "$F" ] || die "$F missing; build first"
    read -r TEXT DATA BSS _ < <("$SIZE" -B "$F" | awk 'NR==2 {print $1, $2, $3}')
    RAM=$((DATA + BSS)); FLASH=$((TEXT + DATA)); N=$((N + 1))
    echo "ram-budget: $ELF RAM ${RAM}/${MAXRAM} B ($((RAM * 100 / MAXRAM))%)  FLASH ${FLASH}/${MAXFLASH} B ($((FLASH * 100 / MAXFLASH))%)"
    if [ "$RAM" -gt "$MAXRAM" ]; then
        echo "ram-budget: FAIL — $ELF static RAM ${RAM} B > ${MAXRAM} B. Shrink buffers / task stacks / queue depths; do not raise the budget to get a pass (AGENTS.md R9)" >&2
        RC=1
    fi
    if [ "$FLASH" -gt "$MAXFLASH" ]; then
        echo "ram-budget: FAIL — $ELF flash ${FLASH} B > ${MAXFLASH} B (AGENTS.md R9)" >&2
        RC=1
    fi
done < "$CONF"
[ "$N" -gt 0 ] || die "no targets in $CONF"
[ "$RC" -eq 0 ] && echo "ram-budget: PASS"
exit "$RC"
