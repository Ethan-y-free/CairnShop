# CairnShop Frontend

Vue 3 + Vite + TypeScript + Element Plus + Pinia + Vue Router.

一个 SPA，两套视图：

- `/` 商城：商品列表/详情、登录/注册、购物车、下单、订单；
- `/ops` 运营台：配置编辑/历史/回滚、商品管理、审计、CairnKV 状态。

## 开发

```bash
cd frontend
npm install
npm run dev
```

Vite 把 `/api` 代理到 `http://127.0.0.1:8080`，所以后端先启动。

国内网络可以切镜像：

```bash
npm config set registry https://registry.npmmirror.com
```

## 构建

```bash
npm run build
```

产物在 `frontend/dist/`。生产环境由 Nginx 托管（见 `docs/DEPLOY.md`）。
构建脚本本身不做 TypeScript 类型检查；需要的话运行：

```bash
npm run typecheck
```

## 目录

```
src/api/client.ts       fetch 封装与 API 类型
src/stores/auth.ts      登录态
src/router/index.ts     路由与权限守卫
src/views/              商城页面
src/views/Ops.vue       运营台（配置 / 商品 / 审计 / 状态）
```
