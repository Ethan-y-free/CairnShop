#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAIRNKV_SRC="${CAIRNKV_SRC:-}"
VERSION="${VERSION:-v0.2.0}"
CAIRNKV_TAG="${CAIRNKV_TAG:-v1.0.0}"

if [ -z "$CAIRNKV_SRC" ] || [ ! -d "$CAIRNKV_SRC" ]; then
    echo "set CAIRNKV_SRC to the CairnKV source directory" >&2
    echo "example: CAIRNKV_SRC=\$HOME/.vs/cairnkv/<hash>/src $0" >&2
    exit 1
fi

echo "backend   : cairnshop-backend:$VERSION"
docker build -f "$ROOT/deploy/Dockerfile.backend" \
    -t "cairnshop-backend:$VERSION" "$ROOT"

echo "cairnkv   : cairnshop-cairnkv:$CAIRNKV_TAG (local source)"
docker build -f "$ROOT/deploy/Dockerfile.cairnkv.local" \
    -t "cairnshop-cairnkv:$CAIRNKV_TAG" "$CAIRNKV_SRC"

echo "nginx tls : cairnshop-nginx:$VERSION"
docker build -f "$ROOT/deploy/nginx/Dockerfile" \
    -t "cairnshop-nginx:$VERSION" "$ROOT"

echo "nginx tun : cairnshop-nginx-tunnel:$VERSION"
docker build -f "$ROOT/deploy/nginx/Dockerfile.tunnel" \
    -t "cairnshop-nginx-tunnel:$VERSION" "$ROOT"

echo "done"
