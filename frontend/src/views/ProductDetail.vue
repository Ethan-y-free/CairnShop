<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { api, type Product, formatCents } from '../api/client';
import { useAuthStore } from '../stores/auth';

const route = useRoute();
const router = useRouter();
const auth = useAuthStore();

const product = ref<Product | null>(null);
const quantity = ref(1);
const loading = ref(true);

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
    try {
        await api.post('/cart/items', {
            product_id: product.value.id,
            quantity: quantity.value
        });
        ElMessage.success('已加入购物车');
    } catch (error) {
        ElMessage.error((error as Error).message || '加入购物车失败');
    }
}

onMounted(load);
</script>

<template>
    <div v-loading="loading">
        <el-page-header content="商品详情" @back="router.back()" />
        <el-card v-if="product" style="margin-top: 16px">
            <h2>{{ product.name }}</h2>
            <p class="muted">{{ product.description }}</p>
            <div class="price">{{ formatCents(product.price_cents) }}</div>
            <p class="muted">
                库存 {{ product.stock }} · SKU {{ product.sku }}
            </p>
            <div class="actions">
                <el-input-number
                    v-model="quantity"
                    :min="1"
                    :max="Math.min(product.stock, 99)"
                />
                <el-button
                    type="primary"
                    :disabled="product.stock <= 0"
                    @click="add"
                >
                    加入购物车
                </el-button>
            </div>
        </el-card>
    </div>
</template>

<style scoped>
.muted {
    color: #6b7280;
}
.price {
    color: #dc2626;
    font-weight: 700;
    font-size: 1.4rem;
    margin: 1rem 0;
}
.actions {
    display: flex;
    gap: 0.75rem;
    align-items: center;
}
</style>
