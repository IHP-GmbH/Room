#!/usr/bin/env bash
set -euo pipefail

LOG_DIR="${1:-build/logs}"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/stdout}"

if [[ ! -d "$LOG_DIR" ]]; then
  echo "No test logs in $LOG_DIR" >> "$SUMMARY"
  exit 0
fi

{
  echo "## Tests"
  echo
  shopt -s nullglob
  for log in "$LOG_DIR"/*.log; do
    name="$(basename "$log" .log)"
    echo "### ${name}"
    echo
    if grep -q '=== timing summary ===' "$log"; then
      awk '
        /^=== timing summary ===/ { show=1; next }
        show && /^[A-Za-z]/ { print "- " $0 }
        show && /^cells:/ { print "- " $0 }
        show && /^top cells:/ { print "- " $0 }
        show && /^placements:/ { print "- " $0 }
      ' "$log"
    else
      echo "- (no timing summary in log)"
    fi
    echo
  done
} >> "$SUMMARY"
