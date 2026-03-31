#!/bin/bash
# Run unit tests (and optionally coverage) inside Docker.
# Uses the same Docker image as coverage.sh but skips the nvidia-fdr
# service startup that requires a real BMC/DBus environment.
#
# Usage:
#   ./coverage/run_unit_tests.sh              # build + run tests + coverage
#   ./coverage/run_unit_tests.sh --tests-only # build + run tests (no coverage report)
#   ./coverage/run_unit_tests.sh --shell      # drop into an interactive shell

set -euo pipefail

IMAGE=nvidia-fdr-coverage
TAG=20230922
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Build the Docker image if it doesn't exist
if ! docker inspect "$IMAGE:$TAG" > /dev/null 2>&1; then
    echo "==> Building Docker image $IMAGE:$TAG ..."
    docker build \
        --build-arg UNAME="$(whoami)" \
        --build-arg UID="$(id -u)" \
        --build-arg GID="$(id -g)" \
        -t "$IMAGE:$TAG" \
        "$SCRIPT_DIR"
fi

MODE="${1:---full}"

case "$MODE" in
    --shell)
        echo "==> Dropping into interactive shell inside container"
        docker run -it --rm \
            -v "$REPO_DIR":/nvidia-fdr \
            --privileged \
            "$IMAGE:$TAG" \
            "bash"
        ;;
    --tests-only)
        echo "==> Building and running unit tests (no coverage)"
        docker run -it --rm \
            -v "$REPO_DIR":/nvidia-fdr \
            --privileged \
            "$IMAGE:$TAG" \
            "cd /nvidia-fdr && rm -rf build_test && meson setup build_test -Dtests=enabled && ninja test -C build_test -v"
        ;;
    --full|*)
        echo "==> Building and running unit tests + coverage report"
        docker run -it --rm \
            -v "$REPO_DIR":/nvidia-fdr \
            --privileged \
            "$IMAGE:$TAG" \
            "set -e; cd /nvidia-fdr && rm -rf build_coverage && \
             meson setup build_coverage -Db_coverage=true -Dtests=enabled && \
             ninja test -C build_coverage -v && \
             echo '=== Unit tests passed ===' && \
             /usr/bin/gcovr --config /nvidia-fdr/gcovr.cfg \
               -r /nvidia-fdr /nvidia-fdr/build_coverage \
               --print-summary && \
             /usr/bin/gcovr --config /nvidia-fdr/gcovr.cfg \
               -r /nvidia-fdr /nvidia-fdr/build_coverage \
               -o /nvidia-fdr/build_coverage/coverage.html \
               --html-details --html-details-syntax-highlighting && \
             echo 'HTML report: build_coverage/coverage.html'"
        ;;
esac
