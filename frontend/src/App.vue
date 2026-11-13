<script setup lang="ts">
import { onMounted } from 'vue';
import { useRouter } from 'vue-router';
import { useAuthStore } from './stores/auth';

const auth = useAuthStore();
const router = useRouter();

async function logout() {
    await auth.logout();
    router.push('/');
}

onMounted(() => {
    if (!auth.loaded) {
        auth.load();
    }
});
</script>

<template>
    <div class="app">
        <header class="topbar">
            <router-link to="/" class="brand">CairnShop</router-link>
            <nav>
                <router-link to="/">商城</router-link>
                <router-link v-if="auth.isLoggedIn" to="/cart">购物车</router-link>
                <router-link v-if="auth.isLoggedIn" to="/orders">订单</router-link>
                <router-link v-if="auth.isOps" to="/ops">运营台</router-link>
            </nav>
            <div class="account">
                <template v-if="auth.isLoggedIn">
                    <span>{{ auth.user?.display_name }}（{{ auth.user?.role }}）</span>
                    <el-button size="small" @click="logout">退出</el-button>
                </template>
                <template v-else>
                    <router-link to="/login">登录</router-link>
                    <router-link to="/register">注册</router-link>
                </template>
            </div>
        </header>
        <main class="content">
            <router-view />
        </main>
    </div>
</template>

<style>
body {
    margin: 0;
    font-family: system-ui, -apple-system, 'Segoe UI', sans-serif;
    background: #f6f7f9;
    color: #1f2328;
}
.app {
    min-height: 100vh;
}
.topbar {
    display: flex;
    align-items: center;
    gap: 1.5rem;
    padding: 0.75rem 1.5rem;
    background: #fff;
    border-bottom: 1px solid #e5e7eb;
}
.brand {
    font-weight: 700;
    font-size: 1.15rem;
    color: #111;
    text-decoration: none;
}
.topbar nav {
    display: flex;
    gap: 1rem;
    flex: 1;
}
.topbar nav a,
.account a {
    color: #374151;
    text-decoration: none;
}
.topbar nav a.router-link-active {
    color: #2563eb;
    font-weight: 600;
}
.account {
    display: flex;
    align-items: center;
    gap: 0.75rem;
}
.content {
    max-width: 1100px;
    margin: 1.5rem auto;
    padding: 0 1rem;
}
</style>
