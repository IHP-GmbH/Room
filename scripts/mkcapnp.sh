#!/usr/bin/env bash
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec "$ROOT/scripts/build_capnp_linux.sh" \
  "https://github.com/capnproto/capnproto.git" branch master "" "" \
  "$ROOT/third_party/capnproto" "$ROOT/third_party/capnp-install"
