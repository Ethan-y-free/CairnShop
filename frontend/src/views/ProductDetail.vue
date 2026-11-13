<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { api, type Product, formatCents } from '../api/client';
import { useAuthStore } from '../stores/auth';
import { useCartStore } from '../stores/cart';

const route = useRoute();
const router = useRouter();
const auth = useAuthStore();
const cart = useCartStore();

const product = ref<Product | null>(null);
const quantity = ref(1);
const loading = ref(true);
const adding = ref(false);

async function load() {
    try {
        product.value = await api.get<Product>(
            `/catalog/products/${route.params.id}`
        );
    } catch (error) {
        ElMessage.error((error as Error).message || '商品不存在');
        router.push('/');
    } finally {
        loading.value = false;
    }
}

async function add() {
    if (!product.value) {
        return;
    }
    if (!auth.isLoggedIn) {
        router.push({ path: '/login', query: { redirect: route.fullPath } });
        return;
    }
    adding.value = true;
    try {
        await api.post('/cart/items', {
            product_id: product.value.id,
            quantity: quantity.value
        });
        await cart.refresh();
        ElMessage.success('已加入购物车');
    } catch (error) {
        ElMessage.error((error as Error).message || '加入购物车失败');
    } finally {
        adding.value = false;
    }
}

onMounted(load);
</script>

<template>
    <div v-loading="loading">
        <el-page-header content="商品详情" @back="router.back()" />
        <div v-if="product" class="detail fade-up">
            <div class="visual">
                <span>{{ product.name.slice(0, 1) }}</span>
                <em v-if="product.stock <= 5" class="low">库存紧张</em>
            </div>
            <div class="info">
                <el-tag effect="plain" round>{{ product.sku }}</el-tag>
                <h1>{{ product.name }}</h1>
                <p class="muted">{{ product.description }}</p>
                <div class="price">{{ formatCents(product.price_cents) }}</div>
                <p class="meta muted">
                    库存 {{ product.stock }} · 分类 #{{ product.category_id }}
                </p>
                <div class="actions">
                    <el-input-number
                        v-model="quantity"
                        :min="1"
                        :max="Math.min(product.stock, 99)"
                    />
                    <el-button
                        type="primary"
                        round
                        :loading="adding"
                        :disabled="product.stock <= 0"
                        @click="add"
                    >
                        加入购物车
                    </el-button>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
.detail {
    display: grid;
    grid-template-columns: minmax(240px, 360px) 1fr;
    gap: 2rem;
    margin-top: 1.25rem;
    padding: 1.5rem;
    border-radius: 20px;
    background: #fff;
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
}

.visual {
    position: relative;
    min-height: 260px;
    border-radius: 16px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: linear-gradient(135deg, rgba(37, 99, 235, 0.16), rgba(124, 58, 237, 0.22));
    overflow: hidden;
}

.visual span {
    font-size: 5rem;
    font-weight: 800;
    color: rgba(37, 99, 235, 0.7);
    animation: float 4s ease-in-out infinite;
}

@keyframes float {
    0%,
    100% {
        transform: translateY(0);
    }
    50% {
        transform: translateY(-8px);
    }
}

.low {
    position: absolute;
    top: 12px;
    right: 12px;
    font-style: normal;
    font-size: 0.75rem;
    padding: 0.2rem 0.6rem;
    border-radius: 999px;
    background: rgba(220, 38, 38, 0.12);
    color: var(--danger);
}

h1 {
    margin: 0.7rem 0 0.4rem;
    letter-spacing: -0.03em;
}

.price {
    font-size: 1.8rem;
    margin: 1rem 0 0.3rem;
}

.meta {
    margin: 0 0 1.2rem;
}

.actions {
    display: flex;
    gap: 0.8rem;
    align-items: center;
}

@media (max-width: 720px) {
    .detail {
        grid-template-columns: 1fr;
    }
}
</style>
