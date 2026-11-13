# Changelog

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
