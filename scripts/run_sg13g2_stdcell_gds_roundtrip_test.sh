#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
INPUT_GDS="${2:-$ROOT/examples/gds_to_core/data/sg13g2_stdcell.gds}"
WORK_DIR="$BUILD/tests/sg13g2_stdcell"
ROUND_GDS="$WORK_DIR/roundtrip.gds"

mkdir -p "$WORK_DIR"

if [[ ! -f "$INPUT_GDS" ]]; then
  echo "Input GDS not found: $INPUT_GDS" >&2
  exit 1
fi

ROUNDTRIP="$BUILD/sg13g2_stdcell_gds_roundtrip"
if [[ -x "$BUILD/sg13g2_stdcell_gds_roundtrip.exe" ]]; then
  ROUNDTRIP="$BUILD/sg13g2_stdcell_gds_roundtrip.exe"
fi

if [[ ! -x "$ROUNDTRIP" ]]; then
  echo "Missing $ROUNDTRIP (cmake --build build --target sg13g2_stdcell_gds_roundtrip)" >&2
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
  local win_paths=(
    "$LOCALAPPDATA/KLayout/klayout_app.exe"
    "$APPDATA/KLayout/klayout_app.exe"
    "/c/Program Files/KLayout/klayout_app.exe"
    "/c/Program Files (x86)/KLayout/klayout_app.exe"
  )
  for candidate in "${win_paths[@]}"; do
    if [[ -f "$candidate" ]]; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

KLAYOUT="$(find_klayout || true)"
if [[ -z "$KLAYOUT" ]]; then
  echo "KLayout not found; set KLAYOUT_EXE or add klayout to PATH" >&2
  exit 1
fi

echo "=== sg13g2_stdcell.gds: GDS -> CORE -> GDS ==="
"$ROUNDTRIP" "$INPUT_GDS" "$ROUND_GDS"

echo
echo "=== KLayout DRC XOR compare ==="
"$KLAYOUT" -b \
  -rd "gds1=$INPUT_GDS" \
  -rd "gds2=$ROUND_GDS" \
  -r "$ROOT/scripts/klayout_compare_gds.drc"
