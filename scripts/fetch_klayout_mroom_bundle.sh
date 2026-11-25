#!/usr/bin/env bash
# Download the newest non-expired KLayout+mroom CI bundle from adatsuk/KLayout-roomdb.
#
# Default artifact matches Room's ubuntu-latest / ubuntu-24.04 runners.
# Requires: gh (GitHub CLI) authenticated with access to that repo's Actions artifacts
# (in CI: secrets.GH_PAT with actions:read on adatsuk/KLayout-roomdb).
#
# Usage:
#   ./scripts/fetch_klayout_mroom_bundle.sh [out-dir]
# Env:
#   KLAYOUT_ROOMDB_REPO   default adatsuk/KLayout-roomdb
#   KLAYOUT_BUNDLE_NAME   default klayout-linux-ubuntu24
#   GH_TOKEN / GITHUB_TOKEN for gh auth
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-$ROOT/build/klayout-mroom-bundle}"
REPO="${KLAYOUT_ROOMDB_REPO:-adatsuk/KLayout-roomdb}"
ARTIFACT="${KLAYOUT_BUNDLE_NAME:-klayout-linux-ubuntu24}"

if ! command -v gh >/dev/null 2>&1; then
  echo "gh (GitHub CLI) is required to download $REPO artifacts" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
STAGE="$OUT_DIR/.download"
rm -rf "$STAGE"
mkdir -p "$STAGE"

echo "Looking up latest non-expired artifact '$ARTIFACT' on $REPO ..."
ART_JSON="$STAGE/artifacts.json"
if ! gh api "repos/$REPO/actions/artifacts?name=$ARTIFACT&per_page=20" >"$ART_JSON"; then
  echo "Failed to query artifacts on $REPO (need GH_TOKEN/PAT with actions:read)." >&2
  exit 1
fi
META_OUT="$STAGE/meta.env"
python3 - "$ART_JSON" "$META_OUT" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
for a in data.get("artifacts", []):
    if a.get("expired"):
        continue
    art_id = a.get("id")
    if not art_id:
        continue
    run = (a.get("workflow_run") or {}).get("id") or ""
    created = a.get("created_at") or ""
    with open(sys.argv[2], "w", encoding="utf-8") as out:
        out.write(f"ART_ID={art_id}\nCREATED={created}\nRUN_HINT={run}\n")
    sys.exit(0)
sys.exit(2)
PY
# shellcheck disable=SC1090
source "$META_OUT"
if [[ -z "${ART_ID:-}" ]]; then
  echo "No non-expired artifact named '$ARTIFACT' on $REPO" >&2
  exit 1
fi
echo "Using artifact id=$ART_ID created=${CREATED:-?} workflow_run=${RUN_HINT:-?}"

ZIP="$STAGE/bundle.zip"
gh api \
  -H "Accept: application/vnd.github+json" \
  "/repos/$REPO/actions/artifacts/$ART_ID/zip" >"$ZIP"

python3 - <<PY
import zipfile
z=zipfile.ZipFile(r"$ZIP")
z.extractall(r"$STAGE")
print("extracted:", ", ".join(z.namelist()[:8]))
PY

TAR="$(find "$STAGE" -maxdepth 1 -type f \( -name '*.tar.gz' -o -name '*.tgz' \) | head -n1)"
if [[ -z "$TAR" ]]; then
  echo "No .tar.gz inside artifact zip" >&2
  ls -la "$STAGE" >&2
  exit 1
fi

# Fresh unpack of dist-bundle/
find "$OUT_DIR" -mindepth 1 -maxdepth 1 ! -name '.download' -exec rm -rf {} +
tar -xzf "$TAR" -C "$OUT_DIR"

BUNDLE_ROOT=""
_run="$(find "$OUT_DIR" -maxdepth 2 -type f -name klayout-run.sh | head -n1 || true)"
if [[ -n "$_run" ]]; then
  BUNDLE_ROOT="$(dirname "$_run")"
else
  _bin="$(find "$OUT_DIR" -maxdepth 2 -type f -name klayout | head -n1 || true)"
  if [[ -n "$_bin" ]]; then
    BUNDLE_ROOT="$(dirname "$_bin")"
  fi
fi
if [[ -z "$BUNDLE_ROOT" ]]; then
  echo "Could not locate klayout-run.sh / klayout under $OUT_DIR" >&2
  find "$OUT_DIR" -maxdepth 3 -type f | head -n40 >&2
  exit 1
fi

# Stamp for CI logs (never wafer paths).
{
  echo "repo=$REPO"
  echo "artifact=$ARTIFACT"
  echo "artifact_id=$ART_ID"
  echo "created_at=$CREATED"
  echo "workflow_run=$RUN_HINT"
  echo "bundle_root=$BUNDLE_ROOT"
} >"$OUT_DIR/BUNDLE_SOURCE.txt"

# Convenience symlink for callers.
ln -sfn "$(basename "$BUNDLE_ROOT")" "$OUT_DIR/current"
echo "Bundle ready: $BUNDLE_ROOT"
echo "$BUNDLE_ROOT"
rm -rf "$STAGE"
