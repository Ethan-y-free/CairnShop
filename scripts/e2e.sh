#!/usr/bin/env bash
set -uo pipefail

B="${B:-http://127.0.0.1:8080}"
ADMIN_EMAIL="${ADMIN_EMAIL:-admin@cairnshop.local}"
ADMIN_PASSWORD="${ADMIN_PASSWORD:-admin123456}"
H='Content-Type: application/json'
TS="$(date +%s)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0
pass() { echo "PASS $1"; PASS=$((PASS + 1)); }
fail() { echo "FAIL $1"; FAIL=$((FAIL + 1)); }
check_eq() {
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1 (expected $2, got $3)"
    fi
}
status() {
    curl -s -o "$TMP/body" -w '%{http_code}' "$@"
}
order_no() {
    sed -n 's/.*"order_no": *"\([^"]*\)".*/\1/p' "$TMP/body" | head -1
}

echo "== health =="
check_eq "healthz" 200 "$(status "$B/healthz")"
check_eq "readyz" 200 "$(status "$B/readyz")"
curl -s "$B/metrics" > "$TMP/metrics"
if grep -q 'cairnshop_http_requests_total' "$TMP/metrics"; then
    pass "metrics"
else
    fail "metrics"
fi

echo "== admin =="
ADMIN_COOKIE="$TMP/admin.cookie"
printf '{"email":"%s","password":"%s"}' "$ADMIN_EMAIL" "$ADMIN_PASSWORD" \
    > "$TMP/admin.json"
check_eq "admin login" 200 \
    "$(status -c "$ADMIN_COOKIE" -H "$H" -d @"$TMP/admin.json" \
        "$B/api/v1/auth/login")"

echo "== customer journey =="
EMAIL="e2e-${TS}-$RANDOM@example.com"
COOKIE="$TMP/customer.cookie"
printf '{"email":"%s","password":"secret123","display_name":"E2E"}' "$EMAIL" \
    > "$TMP/reg.json"
check_eq "register" 201 \
    "$(status -c "$COOKIE" -H "$H" -d @"$TMP/reg.json" \
        "$B/api/v1/auth/register")"
check_eq "catalog" 200 "$(status "$B/api/v1/catalog/products?limit=1")"
check_eq "cart add" 200 \
    "$(status -b "$COOKIE" -H "$H" -d '{"product_id":1,"quantity":1}' \
        "$B/api/v1/cart/items")"

KEY="e2e-${TS}-$RANDOM"
check_eq "checkout" 201 \
    "$(status -b "$COOKIE" -H "Idempotency-Key: $KEY" -X POST \
        "$B/api/v1/orders")"
ORDER1="$(order_no)"
check_eq "checkout replay" 201 \
    "$(status -b "$COOKIE" -H "Idempotency-Key: $KEY" -X POST \
        "$B/api/v1/orders")"
ORDER2="$(order_no)"
check_eq "idempotent order_no" "$ORDER1" "$ORDER2"
check_eq "pay" 200 \
    "$(status -b "$COOKIE" -X POST "$B/api/v1/orders/$ORDER1/pay")"
check_eq "order detail" 200 "$(status -b "$COOKIE" \
    "$B/api/v1/orders/$ORDER1")"
if grep -q '"status": *"paid"' "$TMP/body"; then
    pass "order paid"
else
    fail "order paid"
fi

echo "== phone account =="
PHONE="139$(date +%s | cut -c3-10)"
PHONE_COOKIE="$TMP/phone.cookie"
printf '{"phone":"%s","password":"secret123","display_name":"手机用户"}' \
    "$PHONE" > "$TMP/phone-reg.json"
check_eq "phone register" 201 \
    "$(status -c "$PHONE_COOKIE" -H "$H" -d @"$TMP/phone-reg.json" \
        "$B/api/v1/auth/register")"
printf '{"identifier":"%s","password":"secret123"}' "$PHONE" \
    > "$TMP/phone-login.json"
check_eq "phone login" 200 \
    "$(status -c "$PHONE_COOKIE" -H "$H" -d @"$TMP/phone-login.json" \
        "$B/api/v1/auth/login")"

echo "== rbac =="
check_eq "customer ops status 403" 403 \
    "$(status -b "$COOKIE" "$B/api/v1/ops/status")"

echo "== versioned config =="
OPS_KEY="cairnshop/promos/e2e-${TS}"
printf '{"key":"%s","value":{"id":"e2e","name":"E2E","type":"percent","percent":5,"threshold_cents":0,"enabled":false,"priority":1}}' \
    "$OPS_KEY" > "$TMP/cfg1.json"
check_eq "config create" 200 \
    "$(status -b "$ADMIN_COOKIE" -H "$H" -X PUT -d @"$TMP/cfg1.json" \
        "$B/api/v1/ops/config")"
printf '{"key":"%s","value":{"id":"e2e","name":"E2E","type":"percent","percent":6,"threshold_cents":0,"enabled":false,"priority":1}}' \
    "$OPS_KEY" > "$TMP/cfg2.json"
check_eq "config update" 200 \
    "$(status -b "$ADMIN_COOKIE" -H "$H" -X PUT -d @"$TMP/cfg2.json" \
        "$B/api/v1/ops/config")"

ENCODED_KEY="$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1]))' \
    "$OPS_KEY")"
check_eq "config history" 200 \
    "$(status -b "$ADMIN_COOKIE" \
        "$B/api/v1/ops/config/history?key=$ENCODED_KEY")"
if grep -q '"version": *1' "$TMP/body"; then
    pass "history contains v1"
else
    fail "history contains v1"
fi
printf '{"key":"%s","version":1}' "$OPS_KEY" > "$TMP/rollback.json"
check_eq "config rollback" 200 \
    "$(status -b "$ADMIN_COOKIE" -H "$H" -d @"$TMP/rollback.json" \
        "$B/api/v1/ops/config/rollback")"
check_eq "audit" 200 "$(status -b "$ADMIN_COOKIE" \
    "$B/api/v1/ops/audit?limit=20")"
if grep -q 'config.rollback' "$TMP/body"; then
    pass "audit rollback"
else
    fail "audit rollback"
fi

echo "== fail closed =="
printf '{"key":"cairnshop/flags/checkout_enabled","value":{"enabled":false}}' \
    > "$TMP/flag-off.json"
check_eq "disable checkout" 200 \
    "$(status -b "$ADMIN_COOKIE" -H "$H" -X PUT -d @"$TMP/flag-off.json" \
        "$B/api/v1/ops/config")"
sleep 1
status -b "$COOKIE" -H "$H" -d '{"product_id":1,"quantity":1}' \
    "$B/api/v1/cart/items" >/dev/null
check_eq "checkout disabled 503" 503 \
    "$(status -b "$COOKIE" -H "Idempotency-Key: e2e-off-${TS}" -X POST \
        "$B/api/v1/orders")"
if grep -q 'checkout_disabled' "$TMP/body"; then
    pass "checkout_disabled body"
else
    fail "checkout_disabled body"
fi
printf '{"key":"cairnshop/flags/checkout_enabled","value":{"enabled":true}}' \
    > "$TMP/flag-on.json"
check_eq "restore checkout" 200 \
    "$(status -b "$ADMIN_COOKIE" -H "$H" -X PUT -d @"$TMP/flag-on.json" \
        "$B/api/v1/ops/config")"

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
