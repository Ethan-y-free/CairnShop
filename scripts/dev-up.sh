#!/usr/bin/env bash
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$(ls -d "$HOME"/.vs/cairnshop/*/out/build/Linux-GCC-Debug 2>/dev/null | head -1 || true)}"
KSRC="${KSRC:-$(ls -d "$HOME"/.vs/cairnkv/*/src 2>/dev/null | head -1 || true)}"
KBUILD="${KBUILD:-$(ls -d "$HOME"/.vs/cairnkv/*/out/build/Linux-GCC-Debug 2>/dev/null | head -1 || true)}"

# Local development defaults. A $SRC/.env may override them; the production
# env file (with service names like mysql/cairnkv-1) must not be sourced here.
if [ -f "$SRC/.env" ]; then
    set -a
    . "$SRC/.env"
    set +a
fi
export CAIRNSHOP_MYSQL_HOST="${CAIRNSHOP_MYSQL_HOST:-127.0.0.1}"
export CAIRNSHOP_MYSQL_PORT="${CAIRNSHOP_MYSQL_PORT:-3306}"
export CAIRNSHOP_MYSQL_USER="${CAIRNSHOP_MYSQL_USER:-cairnshop}"
export CAIRNSHOP_MYSQL_PASSWORD="${CAIRNSHOP_MYSQL_PASSWORD:-cairnshop}"
export CAIRNSHOP_MYSQL_DATABASE="${CAIRNSHOP_MYSQL_DATABASE:-cairnshop}"
export CAIRNSHOP_CAIRNKV_ADDRS="${CAIRNSHOP_CAIRNKV_ADDRS:-127.0.0.1:6666,127.0.0.1:6667,127.0.0.1:6668}"

if [ -z "$BUILD" ] || [ ! -x "$BUILD/backend/cairnshop-backend" ]; then
    echo "backend binary not found; build in VS first, or set BUILD=..." >&2
    exit 1
fi

echo "[1/5] MySQL"
if ! service mysql status >/dev/null 2>&1; then
    sudo service mysql start
fi

echo "[2/5] CairnKV"
if ! pgrep -f cairnkd >/dev/null 2>&1; then
    if [ -z "$KSRC" ] || [ -z "$KBUILD" ] || [ ! -x "$KBUILD/cairnkd" ]; then
        echo "CairnKV is not running and KSRC/KBUILD were not found" >&2
        exit 1
    fi
    (cd "$KSRC" && BIN="$KBUILD/cairnkd" ./scripts/run-cluster.sh)
    sleep 2
fi

echo "[3/5] migrate + seed"
cd "$SRC"
"$BUILD/backend/cairnshop-backend" migrate
"$BUILD/backend/cairnshop-backend" seed

echo "[4/5] config keys"
for _ in 1 2 3 4 5; do
    if "$BUILD/backend/cairnshop-backend" seed-config; then
        break
    fi
    sleep 1
done

echo "[5/5] backend + frontend"
if ! pgrep -f cairnshop-backend >/dev/null 2>&1; then
    export CAIRNSHOP_CAIRNKV_ADDRS="${CAIRNSHOP_CAIRNKV_ADDRS:-127.0.0.1:6666,127.0.0.1:6667,127.0.0.1:6668}"
    nohup "$BUILD/backend/cairnshop-backend" serve \
        > /tmp/cairnshop.log 2>&1 &
    sleep 1
fi

if ! pgrep -f "vite --host 0.0.0.0" >/dev/null 2>&1; then
    if ! command -v npm >/dev/null 2>&1; then
        echo "npm not found; run: sudo apt install -y nodejs npm" >&2
        exit 1
    fi
    cd "$SRC/frontend"
    if [ ! -d node_modules ]; then
        npm config set registry https://registry.npmmirror.com >/dev/null 2>&1 || true
        npm install
    fi
    nohup npm run dev -- --host 0.0.0.0 \
        > /tmp/cairnshop-frontend.log 2>&1 &
    sleep 2
fi

IP="$(hostname -I | awk '{print $1}')"
echo
echo "backend : http://$IP:8080"
echo "frontend: http://$IP:5173"
echo "logs    : /tmp/cairnshop.log /tmp/cairnshop-frontend.log"
