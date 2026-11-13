#!/usr/bin/env bash
set -u

IP="$(hostname -I 2>/dev/null | awk '{print $1}')"

if service mysql status >/dev/null 2>&1; then
    echo "MySQL    : up"
else
    echo "MySQL    : down"
fi

if pgrep -f cairnkd >/dev/null 2>&1; then
    echo "CairnKV  : up ($(pgrep -fc cairnkd) nodes)"
else
    echo "CairnKV  : down"
fi

if pgrep -f cairnshop-backend >/dev/null 2>&1; then
    echo "backend  : up"
else
    echo "backend  : down"
fi

code="$(curl -s -o /dev/null -w '%{http_code}' \
    http://127.0.0.1:8080/healthz 2>/dev/null || true)"
echo "healthz  : ${code:-no response}"

if pgrep -f "vite --host 0.0.0.0" >/dev/null 2>&1; then
    echo "frontend : up"
else
    echo "frontend : down"
fi

echo "URL      : http://$IP:5173"
