# Changelog

## v0.3.0 — Operations API and Vue3 Console

### Added

- Versioned configuration writes to CairnKV: MySQL `GET_LOCK`
  serialization, `history[≤20]`, `version + 1`, `updated_by` /
  `updated_at`, and rollback to a previous version.
- Audit log service; config writes and ops catalog writes are recorded and
  exposed through `GET /api/v1/ops/audit`.
- Ops status endpoint: MySQL health, CairnKV cluster role/term/commit/WAL,
  config cache age and staleness.
- Ops product list including `draft` and `archived` products.
- Vue3 SPA: storefront (catalog, auth, cart, checkout, orders) and an
  operations console (config editor/history/rollback, products, audit,
  status). Element Plus + Pinia + Vue Router, `/api` relative in production.
- Production deployment scaffolding: backend/CairnKV/Nginx Dockerfiles,
  Compose profiles for VPS TLS and Cloudflare Tunnel, and `docs/DEPLOY.md`.

### Validation

- Backend: Linux Debug build; `ctest`.
- Frontend: `npm run build` (Vite 5.4, 1633 modules; JS 1.08 MB / CSS
  365 KB gzip 355/49 KB; the chunk-size warning is W9 cleanup).
- Storefront: login -> cart -> checkout with promo -> pay -> order list.
- Ops console: config save -> version increments, history shows the old
  version, rollback restores it, audit rows appear, status page shows
  MySQL / CairnKV / config cache.

### Known limits

- Prepared statements, rate limiting, CSRF, metrics, CI, e2e and load tests
  remain W9 work.
- Deployment scaffolding has not yet been exercised on a public host.
- Nginx currently serves the placeholder page; wiring `frontend/dist` into the
  production image is part of W9.

## v0.2.0 — Cart, Checkout and the Config Plane

### Added

- Cart API: view, add, update quantity, remove; stock-aware quantity cap.
- Checkout transaction: per-cart lock, conditional stock decrement, order +
  order items + payment insert, cart clear, all in one MySQL transaction.
- Promotions evaluated from the CairnKV config snapshot: percent / fixed,
  threshold, priority, time window; the chosen rule is stored on the order as
  `promo_snapshot` together with `config_version`.
- Idempotent checkout: `Idempotency-Key` is inserted in the same transaction;
  a replay returns the stored status and body, no duplicate order.
- Mock payment (`POST /api/v1/orders/:order_no/pay`) and cancellation with
  stock restoration.
- Order list and detail endpoints with ownership checks.
- CairnKV config plane: vendored v1.0.1 client, multi-seed connection,
  500 ms stale-cache refresh, linearizable ops reads, `seed-config`, and
  fail-closed `503 CONFIG_STALE` / `checkout_disabled`.
- `scripts/w7-smoke.sh` covering cart → checkout → idempotency → pay → list →
  admin config read.

### Validation

- `ctest`: 1/1 (crypto, validation, promo).
- W7 smoke: subtotal 12900 → discount 1290 → total 11610; same
  `Idempotency-Key` returns the same `order_no`; payment moves the order to
  `paid`; admin reads 4 config keys with `stale=false`.
- Fail-closed: disabling `cairnshop/flags/checkout_enabled` returns
  `503 checkout_disabled`; re-enabling restores checkout.

### Known limits

- SQL parameters are still escaped with `mysql_real_escape_string`; prepared
  statements remain in the hardening backlog.
- Config writes, version history and rollback are not exposed yet (W8).
- Order list returns order headers; detail returns items.
- No frontend, rate limiting, metrics endpoint, or CI yet.

## v0.1.2 — Plain Product README

### Changed

- README rewritten in a plain product voice. Removed the "not a demo" /
  "first real consumer" framing and the defensive rationale around CairnKV.
  The document now starts with what CairnShop is, followed by features,
  architecture, data split, quick start, API, security, validation, project
  structure, documents, and roadmap.

No code, API, or schema changes.

## v0.1.1 — Documentation

### Changed

- README rewritten as a product introduction: positioning, architecture,
  data split, feature status, quick start, API, security, validation,
  project structure, documents, and roadmap. Internal week-by-week planning
  was removed from the public README and stays in `PRODUCT-draft-v1.md`.
- `third_party/**` marked `linguist-vendored` so GitHub language statistics
  reflect CairnShop's own code instead of the vendored single-header
  dependencies (`httplib.h`, `json.hpp`).

No code, API, or schema changes.

## v0.1.0 — W6 Backend Skeleton

### Added

- C++20 modular monolith skeleton: `api` / `auth` / `catalog` / `infra`.
- MySQL 8 schema migration (`db/migrations/0001_init.sql`) covering users,
  sessions, catalog, cart, orders, payments, idempotency, and audit tables.
- Seed command: admin account, 3 categories, 6 active products.
- Auth: register / login / logout / me, PBKDF2-SHA256 passwords, hashed
  session tokens, HttpOnly cookie, role checks for ops/admin.
- Catalog: public category/product list and detail; ops create/update.
- JSON structured logging, `/healthz`, `/readyz`, per-request log line.
- Unit tests for crypto and input validation.

### Notes

- SQL parameters are escaped with `mysql_real_escape_string` in W6; prepared
  statements are part of W7 hardening.
- CairnKV integration starts in W7 and pins `CairnKV v1.0.0`.
- Frontend (Vue3) starts in W8.
