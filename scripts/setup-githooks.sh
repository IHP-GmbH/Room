#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
git config core.hooksPath .githooks
echo "Git hooks path set to .githooks for this repository."
echo "Cursor co-author trailers will be stripped on commit."
