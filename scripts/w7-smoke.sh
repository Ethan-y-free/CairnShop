#!/usr/bin/env bash
set -euo pipefail

B="${B:-http://127.0.0.1:8080}"
H='Content-Type: application/json'
E="alice-$(date +%s)@example.com"
P='secret123'
N='Alice'

printf '{"email":"%s","password":"%s","display_name":"%s"}' "$E" "$P" "$N" > /tmp/w7-reg.json
curl -s -c /tmp/w7.c -H "$H" -d @/tmp/w7-reg.json "$B/api/v1/auth/register"
echo

printf '{"product_id":1,"quantity":1}' > /tmp/w7-add.json
curl -s -b /tmp/w7.c -H "$H" -d @/tmp/w7-add.json "$B/api/v1/cart/items"
echo

curl -s -b /tmp/w7.c "$B/api/v1/cart"
echo

KEY="w7-$(date +%s)"
curl -s -b /tmp/w7.c -H "$H" -H "Idempotency-Key: $KEY" \
    -X POST "$B/api/v1/orders" | tee /tmp/w7-order1.json
echo
curl -s -b /tmp/w7.c -H "$H" -H "Idempotency-Key: $KEY" \
    -X POST "$B/api/v1/orders" | tee /tmp/w7-order2.json
echo

O1="$(sed -n 's/.*"order_no": *"\([^"]*\)".*/\1/p' /tmp/w7-order1.json)"
O2="$(sed -n 's/.*"order_no": *"\([^"]*\)".*/\1/p' /tmp/w7-order2.json)"
if [ -z "$O1" ] || [ "$O1" != "$O2" ]; then
    echo "idempotency FAILED: $O1 vs $O2"
    exit 1
fi
echo "idempotency OK: $O1"

curl -s -b /tmp/w7.c -X POST "$B/api/v1/orders/$O1/pay"
echo
curl -s -b /tmp/w7.c "$B/api/v1/orders"
echo

printf '{"email":"admin@cairnshop.local","password":"admin123456"}' > /tmp/w7-adm.json
curl -s -c /tmp/w7-adm.c -H "$H" -d @/tmp/w7-adm.json "$B/api/v1/auth/login"
echo

curl -s -b /tmp/w7-adm.c "$B/api/v1/ops/config"
echo
curl -s -b /tmp/w7-adm.c "$B/api/v1/ops/config?key=cairnshop/promos/welcome10"
echo

echo "w7 smoke done: $O1"
