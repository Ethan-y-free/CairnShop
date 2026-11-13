<script setup lang="ts">
import { ref } from 'vue';
import { useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { useAuthStore } from '../stores/auth';

const auth = useAuthStore();
const router = useRouter();

const email = ref('');
const password = ref('');
const displayName = ref('');
const loading = ref(false);

async function submit() {
    loading.value = true;
    try {
        await auth.register(email.value, password.value, displayName.value);
        router.push('/');
    } catch (error) {
        ElMessage.error((error as Error).message || '注册失败');
    } finally {
        loading.value = false;
    }
}
</script>

<template>
    <el-card class="auth-card">
        <h2>注册</h2>
        <el-form label-position="top" @submit.prevent="submit">
            <el-form-item label="邮箱">
                <el-input v-model="email" type="email" />
            </el-form-item>
            <el-form-item label="显示名称">
                <el-input v-model="displayName" />
            </el-form-item>
            <el-form-item label="密码（至少 8 位）">
                <el-input v-model="password" type="password" show-password />
            </el-form-item>
            <el-button type="primary" :loading="loading" @click="submit">
                注册
            </el-button>
        </el-form>
        <p class="hint">
            已有账号？<router-link to="/login">登录</router-link>
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
</style>
