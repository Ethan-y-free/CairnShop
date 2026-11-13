# CairnShop

特性开关 / 促销驱动的电商平台：前台商城 + 运营控制台。

- 后端：C++20 模块化单体（`cpp-httplib` + `nlohmann/json` + MySQL 8 + OpenSSL）
- 前端：Vue 3 + Vite + Element Plus + Pinia（W8 起）
- 配置：CairnKV 强一致配置（W7 起接入 `CairnKV v1.0.0`）
- 部署：Nginx + Docker Compose（W9）

当前进度：**W6 — 仓库、迁移、种子、登录/会话/角色、商品目录**。

## 数据分工（架构 A，2026/11/13 拍板）

```
Browser (Vue3 SPA)
        │
      Nginx
        │
CairnShop backend (C++20)
   ├── MySQL 8     用户 / 会话 / 商品 / 库存 / 订单 / 支付 / 审计   （交易面）
   └── libcairnkv ──▶ CairnKV ×3
                     flags / promos / global / 配置版本与历史       （规则面）
```

- **MySQL 记"发生了什么"**：交易事实，需要事务、索引、库存扣减、分页。
- **CairnKV 记"现在的规则是什么"**：开关、促销、全局结算配置、配置版本/历史/回滚；小数据、读多写少、必须强一致。
- 订单行保存 `promo_snapshot` + `config_version`，可回放"下单当时用的是哪版规则"。
- 不把业务主数据放进 CairnKV：它没有 CAS/多 key 事务/TTL，纯 KV 做库存扣减无法保证 0 超卖。若未来要让它当主存储，先加 CAS + 多 key 事务 + snapshot（CairnKV v1.1）。

可演示的 CairnKV 行为（W7/W8 落地）：

1. 运营台关闭"结算开关" → 全站 ≤1s 生效，下单返回维护提示；
2. 修改/回滚促销规则 → 新订单使用新版本，旧订单保留快照；
3. kill CairnKV leader → 选主 ≤2s，配置缓存继续服务，下单 0 失败；
4. 配置缓存超过 30s 未刷新 → 下单 `503 CONFIG_STALE`（fail closed）。

## 目录

```
backend/            C++20 后端（api / auth / catalog / mysql / crypto）
db/migrations/      schema 迁移（按文件名排序）
third_party/        vendored httplib.h + nlohmann/json.hpp
deploy/             docker compose（W6 只有 MySQL）
docs/               设计 / 运维 / 演示文档（逐步补齐）
```

## 依赖（Ubuntu 24.04）

```bash
sudo apt install build-essential cmake libmysqlclient-dev libssl-dev libgtest-dev
sudo apt install mysql-server   # 或用 deploy/docker-compose.dev.yml 起 MySQL
```

`cpp-httplib` 与 `nlohmann/json` 已 vendor 在 `third_party/`，不需要联网拉依赖。

## 数据库

```sql
CREATE DATABASE IF NOT EXISTS cairnshop
  CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
CREATE USER IF NOT EXISTS 'cairnshop'@'localhost' IDENTIFIED BY 'cairnshop';
GRANT ALL PRIVILEGES ON cairnshop.* TO 'cairnshop'@'localhost';
FLUSH PRIVILEGES;
```

或：

```bash
docker compose -f deploy/docker-compose.dev.yml up -d
```

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 配置与运行

```bash
cp .env.example .env        # 按需修改，真实密钥不要提交
set -a
. ./.env
set +a

./build/backend/cairnshop-backend migrate
./build/backend/cairnshop-backend seed
./build/backend/cairnshop-backend serve
```

`seed` 默认创建：

- 管理员 `admin@cairnshop.local / admin123456`（可用 `CAIRNSHOP_ADMIN_*` 覆盖）
- 3 个分类、6 个商品（`active`）

健康检查：

```bash
curl -s localhost:8080/healthz
curl -s localhost:8080/readyz
```

## API（W6）

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

示例：

```bash
curl -c /tmp/cairnshop.cookies -H 'Content-Type: application/json' \
  -d '{"email":"alice@example.com","password":"secret123","display_name":"Alice"}' \
  http://localhost:8080/api/v1/auth/register

curl -b /tmp/cairnshop.cookies http://localhost:8080/api/v1/auth/me
curl -s http://localhost:8080/api/v1/catalog/products?limit=5
```

## 安全（W6 已落地）

- 密码 PBKDF2-SHA256（120k 次、16B salt、32B hash），常量时间比较；
- 会话 32B 随机 token，DB 只存 SHA-256；HttpOnly + SameSite=Lax，7 天过期；
- 运营接口强制 `ops`/`admin` 角色；
- 所有 SQL 参数经 `mysql_real_escape_string` 转义；W7 迁移到 prepared statements；
- 输入校验：email / password / display_name / slug / 商品状态 / 数值范围。

## 下一步（W7）

- 购物车、下单、库存事务、促销、幂等键、模拟支付；
- 接入 `CairnKV v1.0.0`（FetchContent 固定 tag）读配置；
- 把 SQL 访问迁移到 prepared statements。
