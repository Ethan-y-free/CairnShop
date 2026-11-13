#!/usr/bin/env bash
set -u

KSRC="${KSRC:-$(ls -d "$HOME"/.vs/cairnkv/*/src 2>/dev/null | head -1 || true)}"

echo "stopping frontend"
pkill -f "vite --host 0.0.0.0" 2>/dev/null || true

echo "stopping backend"
pkill -f cairnshop-backend 2>/dev/null || true

if [ "${STOP_CAIRNKV:-0}" = "1" ]; then
    echo "stopping CairnKV"
    if [ -n "$KSRC" ]; then
        (cd "$KSRC" && ./scripts/stop-cluster.sh) 2>/dev/null || true
    fi
else
    echo "CairnKV left running (config keys survive; STOP_CAIRNKV=1 to stop)"
fi

echo "MySQL left running (sudo service mysql stop to stop it)"
