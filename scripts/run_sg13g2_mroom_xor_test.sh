#!/usr/bin/env bash
# XOR sg13g2 stdcell through a prebuilt KLayout+mroom bundle (not apt klayout).
#
# Flow: GDS -> .layout.room (mroom) -> GDS -> KLayout DRC XOR vs source.
# Input is always examples/gds_to_room/data/sg13g2_stdcell.gds (repo fixture).
# Do NOT point this at wafer or any design that must stay off GitHub.
#
# Usage:
#   ./scripts/run_sg13g2_mroom_xor_test.sh [build-dir]
# Env:
#   KLAYOUT_MROOM_ROOT  existing unpacked bundle (skips download)
#   KLAYOUT_ROOMDB_REPO / KLAYOUT_BUNDLE_NAME  passed to fetch script
#   GH_TOKEN            for artifact download
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
INPUT_GDS="${SG13G2_GDS:-$ROOT/examples/gds_to_room/data/sg13g2_stdcell.gds}"
WORK_DIR="$BUILD/tests/sg13g2_mroom_xor"
ROOM_OUT="$WORK_DIR/sg13g2_stdcell.layout.room"
ROUND_GDS="$WORK_DIR/roundtrip.gds"
BUNDLE_DIR="$BUILD/klayout-mroom-bundle"

mkdir -p "$WORK_DIR"

if [[ ! -f "$INPUT_GDS" ]]; then
  echo "Input GDS not found: $INPUT_GDS" >&2
  exit 1
fi

# Hard guard: refuse wafer-scale fixtures if someone overrides the path.
case "$(basename "$INPUT_GDS")" in
  wafer.gds|wafer.oas|wafer.layout.room|wafer*.gds|wafer*.oas)
    echo "Refusing wafer fixture '$INPUT_GDS' — must not run on CI/GitHub paths." >&2
    exit 1
    ;;
esac

resolve_klayout() {
  if [[ -n "${KLAYOUT_MROOM_ROOT:-}" ]]; then
    if [[ -x "$KLAYOUT_MROOM_ROOT/klayout-run.sh" ]]; then
      echo "$KLAYOUT_MROOM_ROOT/klayout-run.sh"
      return 0
    fi
    if [[ -x "$KLAYOUT_MROOM_ROOT/klayout" ]]; then
      echo "$KLAYOUT_MROOM_ROOT/klayout"
      return 0
    fi
  fi
  if [[ -x "$BUNDLE_DIR/current/klayout-run.sh" ]]; then
    echo "$BUNDLE_DIR/current/klayout-run.sh"
    return 0
  fi
  if [[ -x "$BUNDLE_DIR/dist-bundle/klayout-run.sh" ]]; then
    echo "$BUNDLE_DIR/dist-bundle/klayout-run.sh"
    return 0
  fi
  return 1
}

KLAYOUT="$(resolve_klayout || true)"
if [[ -z "$KLAYOUT" ]]; then
  echo "=== Fetch KLayout+mroom bundle from KLayout-roomdb CI ==="
  chmod +x "$ROOT/scripts/fetch_klayout_mroom_bundle.sh"
  "$ROOT/scripts/fetch_klayout_mroom_bundle.sh" "$BUNDLE_DIR"
  KLAYOUT="$(resolve_klayout)"
fi

if [[ -z "$KLAYOUT" || ! -x "$KLAYOUT" ]]; then
  echo "Prebuilt KLayout+mroom not found under $BUNDLE_DIR" >&2
  exit 1
fi

echo "Using KLayout: $KLAYOUT"
if [[ -f "$BUNDLE_DIR/BUNDLE_SOURCE.txt" ]]; then
  echo "--- bundle source ---"
  cat "$BUNDLE_DIR/BUNDLE_SOURCE.txt"
  echo "---------------------"
fi

# Confirm mroom is present next to the binary.
KLAYOUT_DIR="$(cd "$(dirname "$KLAYOUT")" && pwd)"
if [[ ! -e "$KLAYOUT_DIR/db_plugins/libmroom.so" && ! -e "$KLAYOUT_DIR/db_plugins/libmroom.so.0" ]]; then
  echo "WARNING: libmroom.so not found under $KLAYOUT_DIR/db_plugins" >&2
fi

CONVERT="$ROOT/scripts/klayout_convert_layout.drc"
COMPARE="$ROOT/scripts/klayout_compare_gds.drc"

echo "=== mroom: GDS -> ROOM ($INPUT_GDS) ==="
"$KLAYOUT" -b -rd "in=$INPUT_GDS" -rd "out=$ROOM_OUT" -r "$CONVERT"

echo "=== mroom: ROOM -> GDS ==="
"$KLAYOUT" -b -rd "in=$ROOM_OUT" -rd "out=$ROUND_GDS" -r "$CONVERT"

echo "=== KLayout DRC XOR (source GDS vs round-trip GDS) ==="
"$KLAYOUT" -b \
  -rd "gds1=$INPUT_GDS" \
  -rd "gds2=$ROUND_GDS" \
  -r "$COMPARE"

echo "PASSED: sg13g2 stdcell XOR via prebuilt mroom KLayout"
