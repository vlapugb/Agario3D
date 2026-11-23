#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
exec docker buildx build --platform linux/amd64 --file packaging/Dockerfile \
    --build-context "huynity=${HUYNITY_SOURCE_DIR:-../Huynity}" --output type=local,dest=dist .
