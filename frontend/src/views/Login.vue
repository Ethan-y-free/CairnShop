<script setup lang="ts">
import { ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { useAuthStore } from '../stores/auth';

const auth = useAuthStore();
const router = useRouter();
const route = useRoute();

const email = ref('');
const password = ref('');
const loading = ref(false);

async function submit() {
    loading.value = true;
    try {
        await auth.login(email.value, password.value);
        const redirect =
            typeof route.query.redirect === 'string'
                ? route.query.redirect
                : '/';
        router.push(redirect);
    } catch (error) {
        ElMessage.error((error as Error).message || '登录失败');
    } finally {
        loading.value = false;
    }
}
</script>

<template>
    <el-card class="auth-card fade-up">
        <h2>欢迎回来</h2>
        <p class="sub muted">登录后可以加购、下单和查看订单。</p>
        <el-form label-position="top" @submit.prevent="submit">
            <el-form-item label="邮箱">
                <el-input v-model="email" type="email" />
            </el-form-item>
            <el-form-item label="密码">
                <el-input v-model="password" type="password" show-password />
            </el-form-item>
            <el-button type="primary" :loading="loading" @click="submit">
                登录
            </el-button>
        </el-form>
        <p class="hint">
            没有账号？<router-link to="/register">注册</router-link>
        </p>
    </el-card>
</template>

<style scoped>
.auth-card {
    max-width: 420px;
    margin: 40px auto;
}
.hint {
    margin-top: 1rem;
    color: #6b7280;
}
.sub {
    margin: 0 0 1rem;
    font-size: 0.9rem;
}
</style>
