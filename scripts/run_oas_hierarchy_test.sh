#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
INPUT_GDS="${2:-$ROOT/examples/gds_to_core/data/sg13g2_stdcell.gds}"
WORK_DIR="$BUILD/tests/oas"
MINIMAL_OAS="$WORK_DIR/minimal.oas"
CONVERTED_OAS="$WORK_DIR/sg13g2_stdcell.oas"
LOG_DIR="$BUILD/logs"

mkdir -p "$WORK_DIR" "$LOG_DIR"

TOOL="$BUILD/oas_hierarchy"
if [[ -x "$BUILD/oas_hierarchy.exe" ]]; then
  TOOL="$BUILD/oas_hierarchy.exe"
fi
if [[ ! -x "$TOOL" ]]; then
  echo "Missing $TOOL (cmake --build build --target oas_hierarchy)" >&2
  exit 1
fi

find_klayout() {
  if [[ -n "${KLAYOUT_EXE:-}" && -x "$KLAYOUT_EXE" ]]; then
    echo "$KLAYOUT_EXE"
    return 0
  fi
  local candidate
  for candidate in klayout klayout.exe klayout_app klayout_app.exe; do
    if command -v "$candidate" >/dev/null 2>&1; then
      command -v "$candidate"
      return 0
    fi
  done
  return 1
}

echo "=== OAS smoke: create + read ==="
"$TOOL" smoke "$MINIMAL_OAS" TOP

if [[ ! -f "$INPUT_GDS" ]]; then
  echo "Input GDS not found: $INPUT_GDS (skipping sg13g2 OAS read)" >&2
  exit 0
fi

KLAYOUT="$(find_klayout || true)"
if [[ -z "$KLAYOUT" ]]; then
  echo "KLayout not found; skipping GDS->OAS conversion and sg13g2 hierarchy read" >&2
  exit 0
fi

echo
echo "=== Convert GDS to OAS (KLayout) ==="
"$KLAYOUT" -b \
  -rd "gds=$INPUT_GDS" \
  -rd "oas=$CONVERTED_OAS" \
  -r "$ROOT/scripts/klayout_gds_to_oas.drc"

echo
echo "=== OAS hierarchy read: sg13g2_stdcell ==="
"$TOOL" read "$CONVERTED_OAS"
