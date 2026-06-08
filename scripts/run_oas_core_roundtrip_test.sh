#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export CORE_SOURCE_DIR="$ROOT"
cd "$ROOT"
BUILD="${1:-$ROOT/build}"
INPUT_OAS="${2:-}"
INPUT_GDS="${3:-$ROOT/examples/gds_to_core/data/sg13g2_stdcell.gds}"
WORK_DIR="$BUILD/tests/oas_core"
ROUND_OAS="$WORK_DIR/roundtrip.oas"
SOURCE_OAS="$WORK_DIR/source.oas"

mkdir -p "$WORK_DIR"

ROUNDTRIP="$BUILD/oas_core_roundtrip"
if [[ -x "$BUILD/oas_core_roundtrip.exe" ]]; then
  ROUNDTRIP="$BUILD/oas_core_roundtrip.exe"
fi
if [[ ! -x "$ROUNDTRIP" ]]; then
  echo "Missing $ROUNDTRIP (cmake --build build --target oas_core_roundtrip)" >&2
  exit 1
fi

find_klayout() {
  if [[ -n "${KLAYOUT_EXE:-}" && -x "$KLAYOUT_EXE" ]]; then
    echo "$KLAYOUT_EXE"
    return 0
  fi
  local win_paths=(
    "${APPDATA:-}/KLayout/klayout_app.exe"
    "${LOCALAPPDATA:-}/KLayout/klayout_app.exe"
    "/c/Program Files/KLayout/klayout_app.exe"
    "/c/Program Files (x86)/KLayout/klayout_app.exe"
  )
  local path
  for path in "${win_paths[@]}"; do
    if [[ -n "$path" && -f "$path" ]]; then
      echo "$path"
      return 0
    fi
  done
  for candidate in klayout_app klayout_app.exe klayout klayout.exe; do
    if command -v "$candidate" >/dev/null 2>&1; then
      command -v "$candidate"
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

if [[ -n "$INPUT_OAS" ]]; then
  SOURCE_OAS="$INPUT_OAS"
elif [[ -f "$INPUT_GDS" ]]; then
  echo "=== Prepare source OAS from GDS ==="
  "$KLAYOUT" -b \
    -rd "in=$INPUT_GDS" \
    -rd "out=$SOURCE_OAS" \
    -r "$ROOT/scripts/klayout_convert_layout.drc"
  echo
else
  echo "No input OAS or GDS found." >&2
  exit 1
fi

if [[ ! -f "$SOURCE_OAS" ]]; then
  echo "Input OAS not found: $SOURCE_OAS" >&2
  exit 1
fi

echo "=== OAS -> CORE -> OAS ==="
"$ROUNDTRIP" "$SOURCE_OAS" "$ROUND_OAS"

echo
echo "=== KLayout DRC XOR compare ==="
"$KLAYOUT" -b \
  -rd "oas1=$SOURCE_OAS" \
  -rd "oas2=$ROUND_OAS" \
  -r "$ROOT/scripts/klayout_compare_oas.drc"
