#!/usr/bin/env bash

set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-coverage}"
REPORT_NAME="${REPORT_NAME:-coverage.html}"

echo "Configuring coverage build in \"$BUILD_DIR\"..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCORE_ENABLE_COVERAGE=ON

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "Cleaning old coverage data in \"$BUILD_DIR\"..."
find "$BUILD_DIR" -type f \( -name "*.gcda" -o -name "*.gcov" \) -delete 2>/dev/null || true

export LD_LIBRARY_PATH="${ROOT_DIR}/third_party/capnp-install/lib:${LD_LIBRARY_PATH:-}"
export CORE_SOURCE_DIR="$ROOT_DIR"

chmod +x "$ROOT_DIR"/scripts/run_sample_gds_roundtrip_test.sh \
    "$ROOT_DIR"/scripts/run_sg13g2_stdcell_gds_roundtrip_test.sh \
    "$ROOT_DIR"/scripts/run_oas_hierarchy_test.sh \
    "$ROOT_DIR"/scripts/run_oas_core_roundtrip_test.sh 2>/dev/null || true

mkdir -p "$BUILD_DIR/tests"

echo "Running tests (Ubuntu coverage build: $BUILD_DIR)..."
pushd "$ROOT_DIR" >/dev/null || exit 1
ctest --test-dir "$BUILD_DIR" --output-on-failure
TEST_EXIT=$?
popd >/dev/null || exit 1

if [[ $TEST_EXIT -ne 0 ]]; then
    echo "Tests reported $TEST_EXIT failure(s)."
else
    echo "All tests passed."
fi

echo "Generating coverage report..."

pushd "$ROOT_DIR" >/dev/null || exit 1

python3 -m gcovr -j 1 \
    -r "$ROOT_DIR" \
    --object-directory "$BUILD_DIR" \
    --merge-mode-functions=merge-use-line-min \
    --gcov-ignore-errors=all \
    --filter "$ROOT_DIR/src/.*" \
    --filter "$ROOT_DIR/utils/.*" \
    --exclude "$ROOT_DIR/tests/.*" \
    --exclude ".*/build/.*" \
    --exclude ".*/build-coverage/.*" \
    --exclude ".*/third_party/.*" \
    --exclude ".*/generated/.*" \
    --exclude ".*/tools/.*" \
    --exclude ".*/examples/.*" \
    --html-details \
    -o "$REPORT_NAME" \
    --print-summary

GCOVR_EXIT=$?

if [[ -f "$REPORT_NAME" ]]; then
    find "$ROOT_DIR" -type f -name "*.gcov" -delete 2>/dev/null || true
else
    echo "Error: $REPORT_NAME not generated."
fi

popd >/dev/null || exit 1

echo
echo "Summary: test failures=$TEST_EXIT, gcovr exit=$GCOVR_EXIT"

if [[ $GCOVR_EXIT -ne 0 ]]; then
    echo "Error: gcovr failed."
    exit "$GCOVR_EXIT"
fi

if [[ $TEST_EXIT -ne 0 ]]; then
    echo "Error: one or more tests failed."
    exit 1
fi

exit 0
