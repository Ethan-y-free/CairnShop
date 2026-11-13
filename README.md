# CairnShop

An e-commerce platform with a storefront and an operations console.

**v0.1.1** — authentication, roles, catalog, migrations and seed data.

## 功能

CairnShop 包含两部分：

- **商城前台**：注册登录、商品浏览、购物车、下单、订单查询。
- **运营控制台**：商品与库存、促销规则、特性开关、订单、审计、配置版本与回滚。

功能状态：

| 模块 | 状态 |
|---|---|
| 注册 / 登录 / 会话 / 角色（customer / ops / admin） | 已实现 |
| 商品目录（分类、列表、详情、运营创建/更新） | 已实现 |
| MySQL schema 迁移 + 种子数据 | 已实现 |
| 购物车 | 规划中 |
| 下单 / 库存事务 / 订单状态机 | 规划中 |
| 促销 / 特性开关 / 配置版本与回滚 | 规划中 |
| 幂等下单 / 模拟支付 | 规划中 |
| 运营台（审计、配置历史、集群状态） | 规划中 |
| Vue3 商城 + 运营前端 | 规划中 |
| Docker Compose / CI / e2e / 压测 | 规划中 |

## 架构

```
Browser (Vue3 SPA: 商城 / 运营台)
        │ HTTPS
        ▼
      Nginx
        │ /api
        ▼
┌──────────────────────────────┐
│ CairnShop backend (C++20)    │  模块化单体
│  api / auth / catalog        │
│  cart / order / promotion    │
└──────┬────────────────┬──────┘
       │                │
       ▼                ▼
   MySQL 8          libcairnkv ──▶ CairnKV ×3
   业务数据           配置数据
```

- **后端**：C++20 模块化单体；HTTP 使用 `cpp-httplib`，JSON 使用
  `nlohmann/json`。
- **MySQL 8**：用户、会话、商品、库存、购物车、订单、支付、审计。
- **CairnKV**：独立的 Raft KV 仓库；CairnShop 通过 `libcairnkv` 固定 tag
  依赖，存放 feature flags、促销规则、全局结算配置及其版本历史。
- **部署**：Nginx + 后端 + MySQL + 3 节点 CairnKV，Docker Compose 编排。

## 数据划分

| 数据 | 存储 | 说明 |
|---|---|---|
| 用户、会话、商品、库存、购物车、订单、支付、审计 | MySQL 8 | 需要事务、索引、分页、库存扣减 |
| feature flags、促销规则、全局结算配置、配置版本/历史 | CairnKV | 数据量小、读多写少、需要线性一致读 |
| 下单时的规则快照 | MySQL `orders` 列 | `promo_snapshot` + `config_version`，可回放 |

CairnKV 的 API 是 Put / Get / Scan / Delete，没有 CAS、多 key 事务和 TTL，
因此库存扣减和订单事务放在 MySQL；CairnKV 只存配置。

配置读取：

- 后端每 500ms 用 `kStale` 刷新配置缓存，下单只读缓存；
- 运营台“读最新”使用 `kLinearizable`；
- 缓存超过 30s 未刷新时，下单返回 `503 CONFIG_STALE`，避免使用过期规则。

## 快速开始

依赖（Ubuntu 24.04）：

```bash
sudo apt install build-essential cmake libmysqlclient-dev libssl-dev libgtest-dev
sudo apt install mysql-server
```

或只用容器起 MySQL：

```bash
docker compose -f deploy/docker-compose.dev.yml up -d
```

建库与账号：

```sql
CREATE DATABASE IF NOT EXISTS cairnshop
  CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
CREATE USER IF NOT EXISTS 'cairnshop'@'localhost' IDENTIFIED BY 'cairnshop';
GRANT ALL PRIVILEGES ON cairnshop.* TO 'cairnshop'@'localhost';
FLUSH PRIVILEGES;
```

构建与测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

迁移、种子、启动：

```bash
cp .env.example .env
set -a
. ./.env
set +a

./build/backend/cairnshop-backend migrate
./build/backend/cairnshop-backend seed
./build/backend/cairnshop-backend serve
```

`seed` 默认创建管理员 `admin@cairnshop.local / admin123456`（可用
`CAIRNSHOP_ADMIN_*` 覆盖）、3 个分类、6 个商品。

健康检查：

```bash
curl -s localhost:8080/healthz
curl -s localhost:8080/readyz
```

## API

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/v1/auth/register` | 注册并建立会话 |
| POST | `/api/v1/auth/login` | 登录，写 HttpOnly cookie |
| POST | `/api/v1/auth/logout` | 注销当前会话 |
| GET | `/api/v1/auth/me` | 当前用户 |
| GET | `/api/v1/catalog/categories` | 分类列表 |
| GET | `/api/v1/catalog/products` | 商品列表（`category_id` / `q` / `limit` / `offset`） |
| GET | `/api/v1/catalog/products/:id` | 商品详情（仅 `active`） |
| POST | `/api/v1/ops/catalog/categories` | 运营：创建分类（ops/admin） |
| POST | `/api/v1/ops/catalog/products` | 运营：创建商品 |
| PATCH | `/api/v1/ops/catalog/products/:id` | 运营：更新商品 |

```bash
curl -c /tmp/cairnshop.cookies -H 'Content-Type: application/json' \
  -d '{"email":"alice@example.com","password":"secret123","display_name":"Alice"}' \
  http://localhost:8080/api/v1/auth/register

curl -b /tmp/cairnshop.cookies http://localhost:8080/api/v1/auth/me
curl -s 'http://localhost:8080/api/v1/catalog/products?limit=5'
```

## 安全

- 密码 PBKDF2-SHA256（120k 次、16B salt、32B hash），常量时间比较；
- 会话 32B 随机 token，DB 只存 SHA-256；HttpOnly + SameSite=Lax，7 天过期；
- 运营接口强制 `ops` / `admin` 角色；
- SQL 参数统一经转义层处理；prepared statements 在后续加固中；
- 输入校验：email / password / display_name / slug / 商品状态 / 数值范围。

## 验证

- 单元测试：密码哈希、token、输入校验。
- 集成冒烟：迁移 + 种子 + 健康检查 + 注册 / 登录 / me + 商品列表 +
  运营创建商品 + customer 访问运营接口返回 403。
- 后续：订单事务 / 幂等 / 库存并发 / CairnKV 故障演练 / e2e / 压测。

## 工程结构

```
backend/include/cairnshop/   公共头（config / logging / mysql / crypto / auth / catalog）
backend/src/                 实现与 HTTP 入口
backend/tests/               GTest
db/migrations/               schema 迁移
deploy/                      docker compose
frontend/                    Vue3 SPA（占位）
third_party/                 vendored httplib.h + nlohmann/json.hpp
docs/                        设计 / 发布说明
```

## 文档

- [产品与范围草案](PRODUCT-draft-v1.md)：用户旅程、数据划分、API、验收标准
- [v0.1.0 release notes](docs/releases/v0.1.0.md)
- [v0.1.1 release notes](docs/releases/v0.1.1.md)
- [CHANGELOG](CHANGELOG.md)

## Roadmap

- 购物车、下单、库存事务、订单状态机。
- 促销、特性开关、配置版本 / 回滚。
- 幂等下单与模拟支付。
- 运营台：审计、配置历史、CairnKV 集群状态。
- Vue3 商城 + 运营前端。
- Docker Compose、CI、e2e、压测与故障演练。
- 工程加固：prepared statements、限流、CSRF、metrics、结构化请求日志。
