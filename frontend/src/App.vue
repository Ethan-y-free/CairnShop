<script setup lang="ts">
import { onMounted, watch } from 'vue';
import { useRouter } from 'vue-router';
import { useAuthStore } from './stores/auth';
import { useCartStore } from './stores/cart';

const auth = useAuthStore();
const cart = useCartStore();
const router = useRouter();

async function logout() {
    await auth.logout();
    cart.clear();
    router.push('/');
}

onMounted(() => {
    if (!auth.loaded) {
        auth.load();
    }
    if (auth.isLoggedIn) {
        cart.refresh();
    }
});

watch(
    () => auth.user,
    (user) => {
        if (user) {
            cart.refresh();
        } else {
            cart.clear();
        }
    }
);
</script>

<template>
    <div class="app">
        <header class="topbar glass">
            <router-link to="/" class="brand">
                Cairn<span>Shop</span>
            </router-link>
            <nav>
                <router-link to="/">商城</router-link>
                <router-link v-if="auth.isLoggedIn" to="/cart" class="cart-link">
                    购物车
                    <span v-if="cart.count > 0" class="badge-dot">{{ cart.count }}</span>
                </router-link>
                <router-link v-if="auth.isLoggedIn" to="/orders">订单</router-link>
                <router-link v-if="auth.isOps" to="/ops">运营台</router-link>
            </nav>
            <div class="account">
                <template v-if="auth.isLoggedIn">
                    <span class="who">{{ auth.user?.display_name }}</span>
                    <el-tag size="small" effect="plain">{{ auth.user?.role }}</el-tag>
                    <el-button size="small" round @click="logout">退出</el-button>
                </template>
                <template v-else>
                    <router-link to="/login" class="login-link">登录</router-link>
                    <router-link to="/register">
                        <el-button size="small" type="primary" round>注册</el-button>
                    </router-link>
                </template>
            </div>
        </header>

        <main class="content">
            <router-view v-slot="{ Component }">
                <transition name="page" mode="out-in">
                    <component :is="Component" />
                </transition>
            </router-view>
        </main>

        <footer class="footer muted">
            CairnShop · 配置由 CairnKV 提供
        </footer>
    </div>
</template>

<style scoped>
.app {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
}

.topbar {
    position: sticky;
    top: 0;
    z-index: 20;
    display: flex;
    align-items: center;
    gap: 1.75rem;
    padding: 0.7rem 1.5rem;
    border-bottom: 1px solid var(--border);
}

.brand {
    font-weight: 800;
    font-size: 1.2rem;
    letter-spacing: -0.03em;
    text-decoration: none;
    background: linear-gradient(120deg, var(--primary), var(--primary-2));
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
}

.brand span {
    opacity: 0.85;
}

nav {
    display: flex;
    align-items: center;
    gap: 1.35rem;
    flex: 1;
}

nav a {
    position: relative;
    color: #374151;
    text-decoration: none;
    padding: 0.35rem 0;
    transition: color 0.2s ease;
}

nav a:hover {
    color: var(--primary);
}

nav a.router-link-active {
    color: var(--primary);
    font-weight: 600;
}

nav a.router-link-active::after {
    content: '';
    position: absolute;
    left: 0;
    right: 0;
    bottom: -0.2rem;
    height: 2px;
    border-radius: 2px;
    background: linear-gradient(90deg, var(--primary), var(--primary-2));
    animation: underline-in 0.25s ease;
}

@keyframes underline-in {
    from {
        transform: scaleX(0.3);
        opacity: 0.4;
    }
    to {
        transform: scaleX(1);
        opacity: 1;
    }
}

.cart-link {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
}

.account {
    display: flex;
    align-items: center;
    gap: 0.65rem;
}

.who {
    font-weight: 600;
}

.login-link {
    text-decoration: none;
    color: #374151;
}

.content {
    width: min(1120px, 100%);
    margin: 1.75rem auto 0;
    padding: 0 1rem;
    flex: 1;
}

.footer {
    text-align: center;
    padding: 2rem 1rem;
    font-size: 0.85rem;
}
</style>
