<script setup lang="ts">
import { computed, reactive, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ElMessage, type FormInstance, type FormRules } from 'element-plus';
import { useAuthStore } from '../stores/auth';
import {
    isValidEmail,
    isValidPhone,
    normalizePhone,
    trim
} from '../utils/validation';

const auth = useAuthStore();
const router = useRouter();
const route = useRoute();

const mode = ref<'email' | 'phone'>('email');
const formRef = ref<FormInstance>();
const form = reactive({
    identifier: '',
    password: ''
});
const loading = ref(false);

const rules = computed<FormRules>(() => ({
    identifier: [
        {
            validator: (_rule, value: string, callback) => {
                const valid =
                    mode.value === 'email'
                        ? isValidEmail(value)
                        : isValidPhone(value);
                callback(
                    valid
                        ? undefined
                        : new Error(
                              mode.value === 'email'
                                  ? '请输入有效的邮箱地址'
                                  : '请输入 11 位手机号'
                          )
                );
            },
            trigger: 'blur'
        }
    ],
    password: [
        {
            required: true,
            message: '请输入密码',
            trigger: 'blur'
        }
    ]
}));

const placeholder = computed(() =>
    mode.value === 'email' ? 'you@example.com' : '13800000000'
);

function switchMode(next: 'email' | 'phone') {
    if (mode.value === next) {
        return;
    }
    mode.value = next;
    form.identifier = '';
    formRef.value?.clearValidate();
}

async function submit() {
    if (!formRef.value) {
        return;
    }
    const valid = await formRef.value.validate().catch(() => false);
    if (!valid) {
        return;
    }

    const identifier =
        mode.value === 'email'
            ? trim(form.identifier)
            : normalizePhone(form.identifier);

    loading.value = true;
    try {
        await auth.login(identifier, form.password);
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

        <div class="mode-tabs">
            <button
                :class="{ active: mode === 'email' }"
                @click="switchMode('email')"
            >
                邮箱登录
            </button>
            <button
                :class="{ active: mode === 'phone' }"
                @click="switchMode('phone')"
            >
                手机号登录
            </button>
        </div>

        <el-form
            ref="formRef"
            :model="form"
            :rules="rules"
            label-position="top"
            status-icon
        >
            <el-form-item
                :label="mode === 'email' ? '邮箱' : '手机号'"
                prop="identifier"
            >
                <el-input
                    :key="mode"
                    v-model="form.identifier"
                    :type="mode === 'email' ? 'email' : 'text'"
                    :placeholder="placeholder"
                    :autocomplete="mode === 'email' ? 'email' : 'tel'"
                    @keyup.enter="submit"
                />
            </el-form-item>
            <el-form-item label="密码" prop="password">
                <el-input
                    v-model="form.password"
                    type="password"
                    show-password
                    autocomplete="current-password"
                    @keyup.enter="submit"
                />
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
.mode-tabs {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 1rem;
    padding: 0.25rem;
    border-radius: 999px;
    background: #f3f4f6;
}
.mode-tabs button {
    flex: 1;
    border: none;
    background: transparent;
    padding: 0.5rem 0.75rem;
    border-radius: 999px;
    cursor: pointer;
    color: #6b7280;
    font-weight: 600;
    transition: all 0.2s ease;
}
.mode-tabs button.active {
    background: #fff;
    color: var(--primary);
    box-shadow: 0 4px 14px rgba(15, 23, 42, 0.1);
}
</style>
