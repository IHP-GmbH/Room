#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
INPUT_GDS="${2:-$ROOT/testdata/sample.gds}"
WORK_DIR="$BUILD/tests/sample_gds"
ROUND_GDS="$WORK_DIR/roundtrip.gds"

mkdir -p "$WORK_DIR"

SAMPLE_GEN="$BUILD/make_sample_gds"
if [[ -x "$BUILD/make_sample_gds.exe" ]]; then
  SAMPLE_GEN="$BUILD/make_sample_gds.exe"
fi
if [[ -x "$SAMPLE_GEN" ]]; then
  echo "=== Generate testdata/sample.gds ==="
  "$SAMPLE_GEN" "$INPUT_GDS"
  echo
fi

if [[ ! -f "$INPUT_GDS" ]]; then
  echo "Input GDS not found: $INPUT_GDS" >&2
  exit 1
fi

ROUNDTRIP="$BUILD/gds_room_roundtrip"
if [[ -x "$BUILD/gds_room_roundtrip.exe" ]]; then
  ROUNDTRIP="$BUILD/gds_room_roundtrip.exe"
fi

if [[ ! -x "$ROUNDTRIP" ]]; then
  echo "Missing $ROUNDTRIP (cmake --build build --target gds_room_roundtrip)" >&2
  exit 1
fi

echo "=== sample.gds: GDS -> ROOM -> GDS ==="
"$ROUNDTRIP" "$INPUT_GDS" "$ROUND_GDS"
