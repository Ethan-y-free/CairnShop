<script setup lang="ts">
import { computed, reactive, ref } from 'vue';
import { useRouter } from 'vue-router';
import { ElMessage, type FormInstance, type FormRules } from 'element-plus';
import { useAuthStore } from '../stores/auth';
import {
    isValidDisplayName,
    isValidEmail,
    isValidPassword,
    isValidPhone,
    normalizePhone,
    trim
} from '../utils/validation';

const auth = useAuthStore();
const router = useRouter();

const mode = ref<'email' | 'phone'>('email');
const formRef = ref<FormInstance>();
const form = reactive({
    account: '',
    displayName: '',
    password: ''
});
const loading = ref(false);

const rules = computed<FormRules>(() => ({
    account: [
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
    displayName: [
        {
            validator: (_rule, value: string, callback) => {
                callback(
                    isValidDisplayName(value)
                        ? undefined
                        : new Error('显示名称需为 1–64 个字符')
                );
            },
            trigger: 'blur'
        }
    ],
    password: [
        {
            validator: (_rule, value: string, callback) => {
                callback(
                    isValidPassword(value)
                        ? undefined
                        : new Error('密码长度需为 8–128 位')
                );
            },
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
    form.account = '';
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

    const payload: {
        email?: string;
        phone?: string;
        password: string;
        display_name: string;
    } = {
        password: form.password,
        display_name: trim(form.displayName)
    };
    if (mode.value === 'email') {
        payload.email = trim(form.account);
    } else {
        payload.phone = normalizePhone(form.account);
    }

    loading.value = true;
    try {
        await auth.register(payload);
        router.push('/');
    } catch (error) {
        ElMessage.error((error as Error).message || '注册失败');
    } finally {
        loading.value = false;
    }
}
</script>

<template>
    <el-card class="auth-card fade-up">
        <h2>创建账号</h2>
        <p class="sub muted">注册后即可参与促销、下单和查订单。</p>

        <div class="mode-tabs">
            <button
                :class="{ active: mode === 'email' }"
                @click="switchMode('email')"
            >
                邮箱注册
            </button>
            <button
                :class="{ active: mode === 'phone' }"
                @click="switchMode('phone')"
            >
                手机号注册
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
                prop="account"
            >
                <el-input
                    :key="mode"
                    v-model="form.account"
                    :type="mode === 'email' ? 'email' : 'text'"
                    :placeholder="placeholder"
                    :autocomplete="mode === 'email' ? 'email' : 'tel'"
                />
            </el-form-item>
            <el-form-item label="显示名称" prop="displayName">
                <el-input v-model="form.displayName" autocomplete="nickname" />
            </el-form-item>
            <el-form-item label="密码（8–128 位）" prop="password">
                <el-input
                    v-model="form.password"
                    type="password"
                    show-password
                    autocomplete="new-password"
                    @keyup.enter="submit"
                />
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
