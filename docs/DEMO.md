# CairnShop demo and failure drill

## Start

```bash
cd "$HOME/.vs/cairnshop/<hash>/src"
bash scripts/dev-up.sh
bash scripts/dev-status.sh
```

- Storefront: `http://<vm-ip>:5173`
- API: `http://<vm-ip>:8080`
- Admin: `admin@cairnshop.local / admin123456` (or the seeded password)

## Six core journeys

1. **Account**: register a customer; log out; log back in.
2. **Catalog**: search/filter products; open a product detail.
3. **Cart**: add to cart; change quantity; remove.
4. **Checkout**: with the welcome10 promo; `Idempotency-Key` replay returns
   the same order number.
5. **Payment/orders**: pay the pending order; see it under "我的订单";
   cancel another pending order and verify stock is restored.
6. **Ops**: edit `cairnshop/promos/welcome10` percent, save, inspect history,
   roll back; check audit and status tabs; place a new order and compare the
   discount.

## Automated e2e and oversell

```bash
bash scripts/e2e.sh
bash scripts/oversell-test.sh
```

`oversell-test.sh` creates a product with stock 10, 15 buyers each order one
unit concurrently, and asserts exactly 10 orders, 5 conflicts, stock 0.

## Fail-closed config

```bash
CTL="$HOME/.vs/cairnkv/6196b286-cd3f-436e-8a85-083e037e5815/out/build/Linux-GCC-Debug/tools/cairnctl/cairnctl"
"$CTL" --addr 127.0.0.1:6666 put cairnshop/flags/checkout_enabled '{"enabled":false,"version":9}'
sleep 1
# checkout now returns 503 checkout_disabled
"$CTL" --addr 127.0.0.1:6666 put cairnshop/flags/checkout_enabled '{"enabled":true,"version":10}'
```

The config cache is refreshed every 500 ms; the backend fails closed if it is
older than 30 s.

## CairnKV kill-leader drill

1. Keep a customer with a non-empty cart ready.
2. Find the leader and kill it:
   ```bash
   CTL=.../cairnctl
   for a in 6666 6667 6668; do "$CTL" --addr 127.0.0.1:$a status; done
   BASE=/tmp/cairnkv-cluster
   "$CTL" --addr 127.0.0.1:6666 status | grep leader_id
   kill "$(cat "$BASE/node-<id>.pid")"
   ```
3. Immediately checkout with the same customer. The business path reads the
   config snapshot, so the order still succeeds; a new leader is elected in
   1–2 s.
4. Restore the cluster:
   ```bash
   cd "$KSRC"
   ./scripts/stop-cluster.sh
   BIN="$KBUILD/cairnkd" ./scripts/run-cluster.sh
   sleep 2
   cd "$SRC"
   "$BUILD/backend/cairnshop-backend" seed-config
   ```

Configuration keys live in CairnKV's dev data directory under `/tmp`; a fresh
cluster wipes them, so `seed-config` is required after a cluster restart.
MySQL data (users, products, orders, audit) is persistent.

## Metrics

```bash
curl -s http://127.0.0.1:8080/metrics
```

Counters: requests, response classes, auth failures, rate-limited requests,
checkout attempts/failures.
