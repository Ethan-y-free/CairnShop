<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { useRouter } from 'vue-router';
import { api, type Category, type Product, formatCents } from '../api/client';
import { useAuthStore } from '../stores/auth';

const auth = useAuthStore();
const router = useRouter();

const categories = ref<Category[]>([]);
const products = ref<Product[]>([]);
const search = ref('');
const categoryId = ref<number | null>(null);
const loading = ref(false);

async function load() {
    loading.value = true;
    try {
        const params = new URLSearchParams();
        params.set('limit', '24');
        if (search.value) {
            params.set('q', search.value);
        }
        if (categoryId.value) {
            params.set('category_id', String(categoryId.value));
        }
        const data = await api.get<{ products: Product[] }>(
            `/catalog/products?${params.toString()}`
        );
        products.value = data.products;
    } catch (error) {
        ElMessage.error((error as Error).message || '加载商品失败');
    } finally {
        loading.value = false;
    }
}

async function add(product: Product) {
    if (!auth.isLoggedIn) {
        router.push({ path: '/login', query: { redirect: '/' } });
        return;
    }
    try {
        await api.post('/cart/items', {
            product_id: product.id,
            quantity: 1
        });
        ElMessage.success('已加入购物车');
    } catch (error) {
        ElMessage.error((error as Error).message || '加入购物车失败');
    }
}

onMounted(async () => {
    try {
        const data = await api.get<{ categories: Category[] }>(
            '/catalog/categories'
        );
        categories.value = data.categories;
    } catch {
        categories.value = [];
    }
    await load();
});
</script>

<template>
    <div>
        <div class="toolbar">
            <el-input
                v-model="search"
                placeholder="搜索商品"
                clearable
                style="max-width: 260px"
                @keyup.enter="load"
            />
            <el-select
                v-model="categoryId"
                placeholder="全部分类"
                clearable
                style="width: 180px"
                @change="load"
            >
                <el-option
                    v-for="category in categories"
                    :key="category.id"
                    :label="category.name"
                    :value="category.id"
                />
            </el-select>
            <el-button type="primary" @click="load">搜索</el-button>
        </div>

        <el-row v-loading="loading" :gutter="16">
            <el-col
                v-for="product in products"
                :key="product.id"
                :xs="24"
                :sm="12"
                :md="8"
                :lg="6"
                style="margin-bottom: 16px"
            >
                <el-card shadow="hover">
                    <router-link
                        :to="`/product/${product.id}`"
                        class="product-name"
                    >
                        {{ product.name }}
                    </router-link>
                    <p class="muted">{{ product.description }}</p>
                    <div class="price">{{ formatCents(product.price_cents) }}</div>
                    <div class="muted">库存 {{ product.stock }}</div>
                    <el-button
                        type="primary"
                        size="small"
                        style="margin-top: 8px"
                        @click="add(product)"
                    >
                        加入购物车
                    </el-button>
                </el-card>
            </el-col>
        </el-row>

        <el-empty
            v-if="!loading && products.length === 0"
            description="没有商品"
        />
    </div>
</template>

<style scoped>
.toolbar {
    display: flex;
    gap: 0.75rem;
    margin-bottom: 1rem;
}
.product-name {
    font-weight: 600;
    color: #111;
    text-decoration: none;
}
.muted {
    color: #6b7280;
    font-size: 0.85rem;
}
.price {
    color: #dc2626;
    font-weight: 700;
    margin-top: 0.5rem;
}
</style>
