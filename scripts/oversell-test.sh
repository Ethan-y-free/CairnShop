#!/usr/bin/env bash
set -uo pipefail

B="${B:-http://127.0.0.1:8080}"
ADMIN_EMAIL="${ADMIN_EMAIL:-admin@cairnshop.local}"
ADMIN_PASSWORD="${ADMIN_PASSWORD:-admin123456}"
STOCK=10
BUYERS=15
TS="$(date +%s)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
H='Content-Type: application/json'

fail() {
    echo "FAIL $1"
    exit 1
}

printf '{"email":"%s","password":"%s"}' "$ADMIN_EMAIL" "$ADMIN_PASSWORD" \
    > "$TMP/admin.json"
code="$(curl -s -o /dev/null -w '%{http_code}' -c "$TMP/admin.cookie" \
    -H "$H" -d @"$TMP/admin.json" "$B/api/v1/auth/login")"
[ "$code" = 200 ] || fail "admin login ($code)"

printf '{"category_id":1,"sku":"LOAD-%s","name":"Load %s","description":"oversell test","price_cents":100,"stock":%s,"status":"active"}' \
    "$TS" "$TS" "$STOCK" > "$TMP/product.json"
code="$(curl -s -o "$TMP/product.out" -w '%{http_code}' \
    -b "$TMP/admin.cookie" -H "$H" -d @"$TMP/product.json" \
    "$B/api/v1/ops/catalog/products")"
[ "$code" = 201 ] || fail "create product ($code)"
PID="$(sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' "$TMP/product.out" | head -1)"
[ -n "$PID" ] || fail "parse product id"

echo "product=$PID stock=$STOCK buyers=$BUYERS"

for i in $(seq 1 "$BUYERS"); do
    EMAIL="load-${TS}-${i}@example.com"
    printf '{"email":"%s","password":"secret123","display_name":"Load %s"}' \
        "$EMAIL" "$i" > "$TMP/reg-$i.json"
    code="$(curl -s -o /dev/null -w '%{http_code}' -c "$TMP/cookie-$i" \
        -H "$H" -d @"$TMP/reg-$i.json" "$B/api/v1/auth/register")"
    [ "$code" = 201 ] || fail "register $i ($code)"
    curl -s -o /dev/null -b "$TMP/cookie-$i" -H "$H" \
        -d "{\"product_id\":$PID,\"quantity\":1}" \
        "$B/api/v1/cart/items"
done

for i in $(seq 1 "$BUYERS"); do
    (
        curl -s -o "$TMP/order-$i.json" -w '%{http_code}' \
            -b "$TMP/cookie-$i" -H "Idempotency-Key: load-${TS}-${i}" \
            -X POST "$B/api/v1/orders" > "$TMP/code-$i"
    ) &
done
wait

CREATED=0
REJECTED=0
OTHER=0
for i in $(seq 1 "$BUYERS"); do
    code="$(cat "$TMP/code-$i" 2>/dev/null || echo 000)"
    case "$code" in
        201) CREATED=$((CREATED + 1)) ;;
        409) REJECTED=$((REJECTED + 1)) ;;
        *) OTHER=$((OTHER + 1)); echo "unexpected code $code for buyer $i" ;;
    esac
done

curl -s "$B/api/v1/catalog/products/$PID" > "$TMP/final.json"
STOCK_LEFT="$(sed -n 's/.*"stock": *\([0-9][0-9]*\).*/\1/p' "$TMP/final.json" | head -1)"

echo "created=$CREATED rejected=$REJECTED other=$OTHER stock_left=$STOCK_LEFT"
if [ "$CREATED" -ne "$STOCK" ] || [ "$REJECTED" -ne "$((BUYERS - STOCK))" ] ||
   [ "$OTHER" -ne 0 ] || [ "$STOCK_LEFT" != "0" ]; then
    fail "oversell invariant violated"
fi

echo "oversell OK: $STOCK orders created, $REJECTED rejected, stock 0"
