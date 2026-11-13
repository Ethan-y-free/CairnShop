# CairnShop 产品与架构草案（v1）

> 2026/11/02 定稿。CairnShop 是 CairnKV 的第一个真实业务消费者。
> 两个项目独立仓库、独立发布；CairnShop 通过版本化 SDK 依赖 CairnKV，不复制其源码。

## 0. 一句话

**CairnShop 是一个特性开关 / 促销驱动的电商平台：前台商城 + 运营控制台。业务数据在 MySQL，强一致配置在 CairnKV。**

它要证明的不是"能跑通一个 demo"，而是：CairnKV 作为中间件，能支撑一个有登录、下单、库存、促销、审计、限流、可观测、可部署的真实产品。

## 1. 两个独立项目

| | CairnKV | CairnShop |
|---|---|---|
| 角色 | 共识 KV 中间件 | 业务产品 |
| 仓库 | `cairnkv` | `cairnshop` |
| 业务语义 | 无 | 全部 |
| 依赖 | 无业务依赖 | `libcairnkv` 固定 tag（如 v1.0.0） |
| 数据 | 配置 / 开关 / 元数据 | 用户 / 商品 / 订单 / 审计 + CairnKV 配置 |
| 发布 | v0.1 → v1.0，独立 CHANGELOG | v0.1 → v1.0，独立 CHANGELOG |
| CI | 独立 | 独立，含对 CairnKV 的集成测试 |

复用证明：CairnShop 的 CMake 用 `FetchContent` 固定 CairnKV tag，或 `find_package(cairnkv)`；升级 CairnKV 只改 tag，不改业务代码。

## 2. 用户与核心旅程

### 用户

- 顾客（customer）：注册 / 登录、浏览 / 搜索、购物车、下单、支付（模拟）、订单；
- 运营（operator）：商品 / 库存、促销、开关、订单查询、审计、集群状态；
- 管理员（admin）：运营账号与角色（P1）。

### 核心旅程

1. 运营在控制台改促销 / 开关 → 1s 内前台和下单生效；
2. 顾客下单：校验库存（MySQL 事务）→ 读配置快照 → 算价 → 创建订单（幂等）；
3. kill CairnKV leader → 1–2s 选主 → 下单不中断，配置不丢；
4. 运营回滚配置 → 一键恢复上一版本；
5. 审计：谁、何时、改了什么、对应哪个 raft applied_index；
6. 重启所有服务 → 数据恢复，CairnKV 追上日志，业务继续。

## 3. 范围

### 必须完成（P0）

- 登录 / 会话 / 角色；商品 / 分类 / 库存；购物车；下单 / 订单状态机；模拟支付；
- 促销规则 + 特性开关（CairnKV）；配置版本 / 历史 / 回滚；
- 运营控制台；审计；集群状态页；
- Docker Compose 一键部署；迁移 / 种子数据；CI；
- 单测 / 集成 / e2e / 压测；README / 架构 / API / 运维 / 演示文档。

### P1

- 支付回调模拟 + 超时取消；配置 diff；Prometheus + Grafana；限流 / 安全加固；2 个后端实例。

### P2

- 优惠券、评价、收藏、搜索排序、i18n、多租户；不做也行。

### 明确不做

- 真实支付 / 物流 / 发票；微服务拆分 / K8s / 多地域；
- 分布式事务 / 分库分表；秒杀级高并发；
- 自研前端框架 / 组件库。

## 4. 架构

```
Browser (Vue3 SPA: 商城 / 运营台)
        │ HTTPS (Nginx)
        ▼
┌──────────────────────────────┐
│ cairnshop-backend (C++20)    │  模块化单体
│  api / domain / infra / jobs │
└──────┬────────────────┬──────┘
       │                │
       ▼                ▼
   MySQL 8         libcairnkv ──▶ CairnKV ×3 (独立部署)
   业务数据         强一致配置
```

- 后端：模块化单体，非微服务。理由：单人开发、事务边界清晰、部署简单；模块可日后拆。
- 前端：一个 Vue3 SPA，两套布局（`/` 商城、`/ops` 运营台），角色路由。
- 配置：CairnKV 存 flags / promos / global switches；后端读缓存 + 轮询；下单用快照。
- 业务数据：MySQL 存用户 / 会话 / 商品 / 库存 / 购物车 / 订单 / 支付 / 审计。
- 部署：Nginx 托管 SPA 并反代 `/api`；Docker Compose 编排 MySQL、3× CairnKV、后端、Nginx。

## 5. 数据分工

### 5.1 CairnKV key 设计

```
cairnshop/flags/<name>    → JSON {enabled, rollout_percent, allowlist,
                                  version, updated_at, updated_by, history[≤20]}
cairnshop/promos/<id>     → JSON {id, name, type, threshold_cents, percent,
                                  start_at, end_at, enabled, priority,
                                  version, history[≤20]}
cairnshop/global/checkout → JSON {enabled, message, version, ...}
cairnshop/meta/schema     → JSON {version}
```

- value 上限 64 KiB；history 每条约 100–200B，20 条约 4KB，安全。
- 写流程：MySQL `GET_LOCK('cairnshop:cfg:<key>')` 串行化 → 读当前 JSON → 追加 history → version+1 → `CairnKV.Put` → 得到 applied_index → 写审计。
- 读流程：后台每 500ms 用 `kStale` 刷新一次前缀（任意节点，分散 leader 压力）；缓存 version、value、last_success、applied_index；下单只用缓存；运营台"读最新"用 `kLinearizable`。
- 失败语义：缓存超过 30s 未刷新 → 下单返回 `503 CONFIG_STALE`（**fail closed**：宁可不卖，不按错误价格卖）。

### 5.2 MySQL 表

| 表 | 关键字段 |
|---|---|
| `users` | id, email, password_hash, display_name, role, created_at |
| `sessions` | id, token_hash, user_id, expires_at, created_at, last_seen_at |
| `categories` | id, name, slug |
| `products` | id, category_id, name, description, price_cents, stock, status |
| `carts` / `cart_items` | user_id；cart_id, product_id, quantity, UNIQUE(cart_id, product_id) |
| `orders` | id, order_no, user_id, status, subtotal_cents, discount_cents, total_cents, promo_snapshot JSON, config_version, created_at |
| `order_items` | order_id, product_id, product_name_snapshot, unit_price_cents, quantity |
| `payments` | order_id, status, amount_cents, provider, created_at |
| `idempotency_keys` | user_id, key, request_hash, response_status, response_body, UNIQUE(user_id, key) |
| `audit_logs` | actor_id, actor_role, action, target_type, target_id, before_json, after_json, request_id, ip, created_at |
| `schema_migrations` | version, applied_at |

索引：`products(category_id, status)`、`orders(user_id, created_at)`、`orders(status, created_at)`、`sessions(token_hash)`、`audit_logs(created_at)`。

## 6. 关键设计

### 6.1 下单与库存

- 事务内：`UPDATE products SET stock = stock - ? WHERE id = ? AND stock >= ?`；
- 影响行数为 0 → 库存不足，回滚；
- 订单创建与库存扣减同一事务；支付失败 / 超时取消 → 回补库存；
- 不做跨库事务：MySQL 是业务数据的唯一事务边界。

### 6.2 幂等下单

- 请求头 `Idempotency-Key`；
- `idempotency_keys` 唯一键 `(user_id, key)`；
- 同 key + 同请求体 → 返回缓存响应；同 key + 不同请求体 → 409；
- 并发重复请求：先插入 `processing` 占位，失败方返回 409 / `Retry-After`。

### 6.3 促销评估

- 类型：`THRESHOLD_OFF`（满减）、`PERCENT_OFF`（折扣）；
- 从 CairnKV 缓存取启用且在时间窗内的规则，按优先级选最优；
- 订单保存 `promo_snapshot` 与 `config_version`，保证可复现；
- 时间判断用后端 UTC 时钟，允许秒级偏差，快照兜底。

### 6.4 配置读缓存与 fail closed

- 每实例一个后台刷新线程，500ms 用 `kStale` 拉一次前缀；
- 缓存命中直接读内存，避免每单一次 RPC；
- 超过 30s 未成功刷新 → 定价接口 503，浏览接口展示"配置暂不可用"；
- CairnKV leader 切换（1–2s）不会触发 30s 阈值，业务无感。

### 6.5 订单状态机

```
CREATED → PAID → SHIPPED → COMPLETED
   └──────────→ CANCELLED（超时 / 支付失败 / 运营取消）
```

超时取消任务：MySQL `SELECT ... FOR UPDATE SKIP LOCKED` 领取过期订单，回补库存；无需额外协调服务。

## 7. API（/api/v1）

### 商城

```
POST   /auth/register | /auth/login | /auth/logout
GET    /me
GET    /products?category=&q=&cursor=&limit=
GET    /products/{id}
GET    /cart
POST   /cart/items          {product_id, quantity}
PATCH  /cart/items/{id}     {quantity}
DELETE /cart/items/{id}
POST   /orders              Idempotency-Key
POST   /orders/{order_no}/pay
GET    /orders?status=&cursor=
GET    /orders/{order_no}
```

### 运营台

```
CRUD   /ops/products | /ops/promotions | /ops/flags
GET    /ops/orders?status=&q=&page=
GET    /ops/orders/{order_no}
GET    /ops/audit?actor=&action=&page=
GET    /ops/cluster
GET    /ops/configs/{key}/history
POST   /ops/configs/{key}/rollback
GET    /ops/dashboard
```

### 通用

```
GET /healthz   GET /readyz   GET /metrics
```

## 8. 前端

Vue3 + Vite + Element Plus + Pinia + Vue Router，一个 SPA 两套布局。

### 商城

首页（促销 Banner + 推荐）、商品列表 / 搜索、商品详情、购物车、结算、订单列表 / 详情、登录 / 注册。

### 运营台

| 页面 | 内容 |
|---|---|
| Dashboard | 今日订单 / GMV、活跃促销、CairnKV 集群状态、错误率 |
| 商品 | CRUD、库存调整、上下架 |
| 促销 | CRUD、启用 / 停用、时间窗、预览 |
| 开关 | 全局开关、rollout、allowlist、变更历史 |
| 订单 | 搜索、筛选、详情、状态操作 |
| 审计 | 筛选、分页、before/after diff |
| 集群 | 节点角色 / term / commitIndex / lastApplied / WAL、leader 高亮 |
| 配置历史 | 版本时间线、diff、回滚 |

## 9. 真实产物清单

| 维度 | Demo 做法 | 本项目验收 |
|---|---|---|
| 用户 | 无登录 / 一个 admin | 注册登录、角色、会话、密码哈希 |
| 业务 | 一个 put/get 页面 | 商品 → 购物车 → 下单 → 支付 → 订单状态机 |
| 数据 | 内存 / 单表 | 迁移、事务、索引、幂等键、审计 |
| 配置 | 改代码 | CairnKV 强一致配置、版本、回滚 |
| 故障 | 不测 | kill leader / 重启 / 分区演练，业务不中断 |
| 安全 | 无 | 输入校验、SQL 注入防护、限流、CSRF、安全头 |
| 可观测 | console.log | 结构化日志、request_id、metrics、健康检查 |
| 部署 | 本机跑 | Compose 一键起、健康检查、卷、备份 |
| 测试 | 手动点 | 单测 + 集成 + e2e + 压测，CI 全绿 |
| 文档 | 一句话 README | 架构 / API / 运维 / 演示 / 性能报告 / ADR |
| 版本 | 无 | semver、CHANGELOG、镜像 tag、SDK 兼容 |

## 10. 安全

- 密码：Argon2id（libsodium）或 PBKDF2-SHA256（OpenSSL），绝不明文；
- 会话：32 字节随机 token，DB 存 SHA-256；HttpOnly、SameSite=Lax、生产 Secure；7 天过期；
- CSRF：SameSite=Lax + 写请求 `Origin` 校验；P1 加 double-submit token；
- SQL：只用 prepared statements；
- 输入：长度、枚举、JSON 字段校验；
- 限流：登录 5/min/IP，下单 10/min/用户，运营写 30/min/用户（单实例内存桶，P1 换 Redis）；
- 响应头：CSP、X-Frame-Options、X-Content-Type-Options、Referrer-Policy、HSTS（生产）；
- 密钥：环境变量，`.env` 不入库，只提交 `.env.example`；
- 权限：运营路由强制角色校验；所有写操作进审计；
- TLS：Nginx 自签证书演示，文档写明生产替换。

## 11. 可观测性

- JSON 结构化日志：time / level / request_id / user_id / route / status / latency_ms；
- 请求 ID 中间件，贯穿后端与 CairnKV client 日志；
- `/metrics`：HTTP QPS / 延迟直方图 / 错误率、DB 连接池、CairnKV 客户端延迟与错误、配置缓存年龄、订单数 / GMV；
- `/healthz` 存活；`/readyz` 检查 MySQL 与 CairnKV 连通；
- 告警阈值写入运维文档：错误率 >1%、p99 >500ms、配置缓存 >30s、CairnKV 无 leader >5s。

## 12. 部署

- `docker compose up -d` 启动：MySQL、3× CairnKV、后端、Nginx；
- 健康检查：mysqladmin ping、cairnctl status、后端 /healthz、Nginx；
- 卷：`mysql-data`、`cairn-data-1/2/3`；
- 迁移：`make migrate`（后端子命令，`schema_migrations` 表）；
- 种子：`make seed`（商品 / 运营账号 / 示例促销）；
- 备份 / 恢复：`scripts/backup.sh`、`scripts/restore.sh` + 运维 runbook；
- 环境：`.env.example`，不提交真实密钥。

## 13. 测试与验证

- 单测：促销计算、订单状态机、鉴权、校验、仓储；
- 集成：迁移、事务 / 库存、幂等下单、CairnKV client 对 3 节点；
- e2e：API 级脚本覆盖 6 条核心旅程；P1 加 Playwright；
- 压测：k6 浏览 + 下单场景，100 VU；记录 p50/p99、错误率、是否超卖；
- 混沌：压测中 kill CairnKV leader / 重启节点 / 分区；
- CI：构建、单测、集成（compose）、e2e、前端构建。

### CairnKV 侧验证指标

| 指标 | 目标 |
|---|---|
| 配置读 p50 / p99 | 记录实测 |
| 开关生效时间 | ≤1s（缓存 TTL） |
| kill leader 选主 | ≤2s |
| kill leader 期间下单失败 | 0（缓存 30s 兜底） |
| 节点重启追日志 | 记录实测 |
| SDK 重连 | ≤3s |
| 100 并发下单超卖 | 0 |
| 同 Idempotency-Key 重复订单 | 0 |

结果写入 CairnKV `docs/CASE-STUDY.md`。

## 14. 排期

| 周 | 内容 |
|---|---|
| W1–W5 | CairnKV 核心（v1.0：事件驱动 + ReadIndex） |
| W6 | CairnShop 仓库、迁移、种子、登录 / 会话 / 角色、商品目录 |
| W7 | 购物车、下单、库存事务、促销、幂等、模拟支付 |
| W8 | 运营 API、审计、集群状态、配置历史 / 回滚；商城前端 |
| W9 | 运营前端、Compose、CI、e2e、压测、文档、截图 |
| W10 | 缓冲：安全加固、性能、演示视频 |

## 15. 验收标准

1. 全新 clone + `docker compose up` <10 分钟起全站，含种子数据；
2. 6 条核心旅程全部通过，e2e 脚本可复跑；
3. 压测中 kill leader：0 失败下单，选主 ≤2s，配置正确；
4. 100 并发下单：0 超卖；同幂等键 0 重复订单；
5. 未登录访问运营 API → 401；顾客访问运营 API → 403；SQLi payload 无效；限流触发；
6. 重启全部服务：数据不丢，CairnKV 恢复，后端自动重连；
7. CI 全绿；README / API / 运维 / 性能 / 演示文档齐全。

## 16. 风险与砍单顺序

| 风险 | 缓解 |
|---|---|
| 范围过大 | P0 优先；P1/P2 可砍 |
| C++ Web 开发慢 | `cpp-httplib` + nlohmann/json，不手写 HTTP |
| 鉴权 / CSRF 复杂 | 会话表 + SameSite + Origin 校验，P1 再加 CSRF token |
| 前端耗时 | 一个 SPA、Element Plus、AI 生成 + 用户验收 |
| 集成问题 | 先 API 级 e2e，再做 UI |

砍单顺序：P2 → 运营台细节 → 压测 → 审计对账 → 配置历史 → **保商城 + 下单 + 开关 + Compose**。

## 17. 非目标

- 真实支付、物流、发票、退款；
- 微服务、K8s、多地域、多租户；
- 分布式事务、分库分表、秒杀级并发；
- 生产级 SLA；规模边界写进文档。
