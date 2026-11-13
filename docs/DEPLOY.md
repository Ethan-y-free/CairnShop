# Production deployment

Target: one Linux VPS running Docker Compose. Only ports **80/443** are
public; MySQL, the three CairnKV nodes, and the backend stay on the internal
Docker network. Nginx terminates TLS with a Let's Encrypt certificate.

```
Internet
  │ 80/443
  ▼
Nginx ── /api ──► backend:8080
  │                 ├── mysql:3306
  │                 └── cairnkv-{1,2,3}:6666
  └── static SPA (frontend build)
```

This is the production path; `127.0.0.1` only appears in local development.

## 1. Prerequisites

- Ubuntu 24.04 VPS, 2 vCPU / 4 GB RAM or more.
- Docker Engine 27+ with Compose v2.
- A domain whose A record points at the VPS.
- Ports 22, 80, 443 open; no other inbound ports.
- Mainland China: a public domain served from a mainland host requires ICP
  filing. A Hong Kong / overseas VPS avoids that requirement.

## 2. Secrets and environment

```bash
cd /opt/cairnshop
cp .env.production.example .env.production
```

Edit `.env.production`:

- `DOMAIN`, `ACME_EMAIL`
- `MYSQL_ROOT_PASSWORD`, `MYSQL_PASSWORD` — generate with
  `openssl rand -hex 24`
- `CAIRNSHOP_ADMIN_PASSWORD` — change before first boot
- keep `CAIRNSHOP_COOKIE_SECURE=1`

`.env.production` is ignored by git and must never be committed.

If the source tree is synced into a temporary directory (for example a VS
remote-build copy), keep the env file outside it and point Compose at it:

```bash
export CAIRNSHOP_ENV_FILE=/home/ethany/cairnshop.env
docker compose --env-file "$CAIRNSHOP_ENV_FILE" \
  -f deploy/docker-compose.prod.yml ...
```

## 3. Build images

```bash
docker compose --profile vps --env-file .env.production \
  -f deploy/docker-compose.prod.yml build
```

`deploy/Dockerfile.cairnkv` clones `CairnKV v1.0.1` from GitHub. If the build
host cannot reach GitHub, build the images from a local CairnKV checkout:

```bash
CAIRNKV_SRC="$HOME/.vs/cairnkv/<hash>/src" bash deploy/build-images.sh
```

`build-images.sh` builds `cairnshop-backend`, `cairnshop-cairnkv`,
`cairnshop-nginx`, and `cairnshop-nginx-tunnel` from local sources. Then run
Compose with `--no-build` (see the tunnel section). The base images
(`ubuntu:24.04`, `mysql:8.4`, `nginx:1.27`, `cloudflare/cloudflared`) still
come from Docker Hub; if Docker Hub is unreachable, configure a registry
mirror or mirror those images locally.

## 4. First TLS certificate

The script starts only data services first, obtains a certificate with
Certbot standalone (port 80 must be free), then starts Nginx.

```bash
set -a
. ./.env.production
set +a

COMPOSE="docker compose --profile vps --env-file .env.production -f deploy/docker-compose.prod.yml"

$COMPOSE up -d mysql cairnkv-1 cairnkv-2 cairnkv-3
$COMPOSE up -d migrate seed seed-config backend

$COMPOSE run --rm --service-ports certbot \
  certonly --standalone -d "$DOMAIN" --email "$ACME_EMAIL" \
  --agree-tos -n

$COMPOSE up -d nginx
```

The one-shot `migrate`, `seed`, and `seed-config` services are idempotent;
`backend` waits for all three to complete.

## 5. Verify

```bash
$COMPOSE ps
curl -fsS "https://$DOMAIN/healthz"; echo
curl -fsS "https://$DOMAIN/readyz"; echo
curl -fsS "https://$DOMAIN/api/v1/catalog/products?limit=1"; echo
```

`readyz` requires MySQL and a fresh CairnKV config cache. The Vue3 app lives
in `frontend/`; wiring its build into the Nginx image is W9 work. Until then
the Nginx image serves a placeholder.

## 6. Certificate renewal

The `certbot` service is a one-shot runner, not a daemon. Add a host cron:

```cron
0 3 * * * cd /opt/cairnshop && docker compose --profile vps --env-file .env.production -f deploy/docker-compose.prod.yml run --rm certbot renew --webroot -w /var/www/certbot --quiet && docker compose --profile vps --env-file .env.production -f deploy/docker-compose.prod.yml exec -T nginx nginx -s reload
```

## 7. Backups

```bash
# MySQL
docker compose --profile vps --env-file .env.production -f deploy/docker-compose.prod.yml \
  exec -T mysql sh -c 'mysqldump -uroot -p"$MYSQL_ROOT_PASSWORD" --single-transaction cairnshop' \
  > cairnshop-$(date +%F).sql

# CairnKV data volumes (one per node)
for n in 1 2 3; do
  docker run --rm \
    -v cairnshop_cairnkv-${n}-data:/data:ro \
    -v "$PWD":/backup alpine \
    tar czf /backup/cairnkv-${n}-$(date +%F).tgz -C /data .
done
```

Also back up `.env.production` in a secret manager, not in git.

## 8. Upgrades

```bash
git pull
docker compose --profile vps --env-file .env.production \
  -f deploy/docker-compose.prod.yml build
docker compose --profile vps --env-file .env.production \
  -f deploy/docker-compose.prod.yml up -d
```

Migrations run in the one-shot `migrate` service before the backend starts.

## 9. Security checklist

- SSH keys only; disable password login.
- Firewall: allow 22/80/443, deny everything else.
- Strong MySQL and admin secrets; never the example values.
- `CAIRNSHOP_COOKIE_SECURE=1` and HTTPS only.
- Backups tested by restoring into a scratch database.
- Rate limiting, CSRF double-submit, and metrics are planned for W9.

## 10. Local development vs production

Local development may point at `127.0.0.1` and use `deploy/docker-compose.dev.yml`.
Production uses the service names above (`mysql`, `cairnkv-1..3`, `backend`)
and exposes only Nginx.

## 11. Zero-cost option: home machine + Cloudflare Tunnel

If a machine can stay on 24/7, skip the VPS and expose the Compose stack
through a Cloudflare Tunnel. Cloudflare terminates TLS at its edge; Nginx
listens on the internal network only, and no router port forwarding is
needed.

Build once (on a machine that can reach GitHub), or use
`deploy/build-images.sh` for a local-source build:

```bash
docker compose --env-file .env.production \
  -f deploy/docker-compose.prod.yml --profile tunnel-quick build
```

Quick tunnel (no domain; URL changes on restart):

```bash
COMPOSE="docker compose --env-file .env.production -f deploy/docker-compose.prod.yml"
$COMPOSE --profile tunnel-quick up -d --no-build
$COMPOSE logs -f cloudflared-quick
```

Look for `https://<random>.trycloudflare.com` in the logs. The whole stack
uses the same MySQL/CairnKV/backend services; only Nginx and cloudflared
differ.

Stable URL with a domain (recommended):

1. In Cloudflare Zero Trust → Networks → Tunnels, create a tunnel.
2. Add a public hostname, e.g. `shop.example.com`, with service
   `http://nginx-tunnel:80`.
3. Put the tunnel token in `.env.production` as
   `CLOUDFLARE_TUNNEL_TOKEN=...`.
4. Start the named profile:

```bash
$COMPOSE --profile tunnel-named up -d --no-build
$COMPOSE logs -f cloudflared-named
```

The tunnel makes outbound connections only; the home IP and ports stay
closed. The machine must stay on. For a stable public link use the named
tunnel; the quick tunnel is for a one-off demo.

Notes:

- `CAIRNSHOP_COOKIE_SECURE=1` still works: the browser sees HTTPS at
  Cloudflare even though the origin connection is HTTP.
- Cloudflare's free edge is outside mainland China; access from the mainland
  can be slower than a domestic VPS.
- Add Cloudflare Access in front of `/api/v1/ops` later if the operations
  console is exposed.
