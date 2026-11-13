# Changelog

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
