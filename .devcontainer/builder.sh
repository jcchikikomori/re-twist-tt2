#!/usr/bin/env bash
# Build helper for the tt2-builder image. Run from the project root.
set -euo pipefail
cd "$(dirname "$0")/.."
podman build -t tt2-builder:bookworm -f .devcontainer/Dockerfile .
