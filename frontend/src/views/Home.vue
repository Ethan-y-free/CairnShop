<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { useRouter } from 'vue-router';
import { api, type Category, type Product, formatCents } from '../api/client';
import { useAuthStore } from '../stores/auth';
import { useCartStore } from '../stores/cart';

const auth = useAuthStore();
const cart = useCartStore();
const router = useRouter();

const categories = ref<Category[]>([]);
const products = ref<Product[]>([]);
const search = ref('');
const categoryId = ref<number | null>(null);
const loading = ref(false);
const addingId = ref<number | null>(null);

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

function selectCategory(id: number | null) {
    categoryId.value = id;
    load();
}

async function add(product: Product) {
    if (!auth.isLoggedIn) {
        router.push({ path: '/login', query: { redirect: '/' } });
        return;
    }
    addingId.value = product.id;
    try {
        await api.post('/cart/items', {
            product_id: product.id,
            quantity: 1
        });
        await cart.refresh();
        ElMessage.success('已加入购物车');
    } catch (error) {
        ElMessage.error((error as Error).message || '加入购物车失败');
    } finally {
        addingId.value = null;
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
        <section class="hero fade-up">
            <div>
                <p class="eyebrow">CairnShop</p>
                <h1>
                    强一致配置驱动的<br />
                    <span class="gradient-text">促销电商</span>
                </h1>
                <p class="sub muted">
                    实时开关、促销规则与库存，在同一套系统里完成。
                </p>
            </div>
            <div class="hero-card">
                <div class="pulse"></div>
                <p>配置生效延迟</p>
                <strong>&lt; 1s</strong>
                <span class="muted">CairnKV 500ms 缓存</span>
            </div>
        </section>

        <div class="filters fade-up">
            <el-input
                v-model="search"
                placeholder="搜索商品"
                clearable
                class="search"
                @keyup.enter="load"
            />
            <div class="chips">
                <button
                    class="chip"
                    :class="{ active: categoryId === null }"
                    @click="selectCategory(null)"
                >
                    全部
                </button>
                <button
                    v-for="category in categories"
                    :key="category.id"
                    class="chip"
                    :class="{ active: categoryId === category.id }"
                    @click="selectCategory(category.id)"
                >
                    {{ category.name }}
                </button>
                <el-button type="primary" round @click="load">搜索</el-button>
            </div>
        </div>

        <div v-if="loading" class="skeleton">
            <el-skeleton :rows="6" animated />
        </div>

        <el-row v-else :gutter="18">
            <el-col
                v-for="(product, index) in products"
                :key="product.id"
                :xs="24"
                :sm="12"
                :md="8"
                :lg="6"
                class="card-col fade-up"
                :style="{ animationDelay: `${index * 45}ms` }"
            >
                <div class="product-card card-hover">
                    <div class="cover">
                        <span>{{ product.name.slice(0, 1) }}</span>
                        <em v-if="product.stock <= 5" class="low">仅剩 {{ product.stock }}</em>
                    </div>
                    <div class="body">
                        <router-link :to="`/product/${product.id}`" class="name">
                            {{ product.name }}
                        </router-link>
                        <p class="desc muted">{{ product.description }}</p>
                        <div class="row">
                            <span class="price">{{ formatCents(product.price_cents) }}</span>
                            <span class="stock muted">库存 {{ product.stock }}</span>
                        </div>
                        <el-button
                            type="primary"
                            round
                            class="add"
                            :loading="addingId === product.id"
                            :disabled="product.stock <= 0"
                            @click="add(product)"
                        >
                            加入购物车
                        </el-button>
                    </div>
                </div>
            </el-col>
        </el-row>

        <el-empty
            v-if="!loading && products.length === 0"
            description="没有找到商品"
        />
    </div>
</template>

<style scoped>
.hero {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 2rem;
    padding: 2.2rem 2rem;
    margin-bottom: 1.5rem;
    border-radius: 22px;
    background:
        linear-gradient(120deg, rgba(37, 99, 235, 0.12), rgba(124, 58, 237, 0.12)),
        #fff;
    border: 1px solid rgba(37, 99, 235, 0.12);
    overflow: hidden;
}

.eyebrow {
    margin: 0 0 0.5rem;
    font-size: 0.8rem;
    font-weight: 700;
    letter-spacing: 0.18em;
    text-transform: uppercase;
    color: var(--primary);
}

h1 {
    margin: 0;
    font-size: clamp(1.8rem, 4vw, 2.6rem);
    line-height: 1.15;
    letter-spacing: -0.03em;
}

.sub {
    margin: 0.9rem 0 0;
}

.hero-card {
    min-width: 180px;
    padding: 1.2rem 1.4rem;
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.75);
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
    display: grid;
    gap: 0.25rem;
}

.hero-card p {
    margin: 0;
    color: var(--muted);
    font-size: 0.85rem;
}

.hero-card strong {
    font-size: 2rem;
    letter-spacing: -0.04em;
}

.pulse {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #22c55e;
    box-shadow: 0 0 0 0 rgba(34, 197, 94, 0.5);
    animation: pulse 2s infinite;
}

@keyframes pulse {
    0% {
        box-shadow: 0 0 0 0 rgba(34, 197, 94, 0.5);
    }
    70% {
        box-shadow: 0 0 0 12px rgba(34, 197, 94, 0);
    }
    100% {
        box-shadow: 0 0 0 0 rgba(34, 197, 94, 0);
    }
}

.filters {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
    margin-bottom: 1.25rem;
}

.search {
    max-width: 320px;
}

.chips {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    flex-wrap: wrap;
}

.chip {
    border: 1px solid var(--border);
    background: #fff;
    color: #374151;
    border-radius: 999px;
    padding: 0.35rem 0.9rem;
    cursor: pointer;
    transition: all 0.2s ease;
}

.chip:hover {
    border-color: var(--primary);
    color: var(--primary);
}

.chip.active {
    background: linear-gradient(120deg, var(--primary), var(--primary-2));
    border-color: transparent;
    color: #fff;
    box-shadow: 0 8px 20px rgba(37, 99, 235, 0.25);
}

.skeleton {
    background: #fff;
    border-radius: var(--radius);
    padding: 1.5rem;
    box-shadow: var(--shadow);
}

.card-col {
    margin-bottom: 18px;
}

.product-card {
    height: 100%;
    border-radius: var(--radius);
    background: var(--card);
    border: 1px solid var(--border);
    overflow: hidden;
    box-shadow: var(--shadow);
}

.cover {
    position: relative;
    height: 120px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: linear-gradient(135deg, rgba(37, 99, 235, 0.18), rgba(124, 58, 237, 0.22));
}

.cover span {
    font-size: 2.4rem;
    font-weight: 800;
    color: rgba(37, 99, 235, 0.75);
    transition: transform 0.3s ease;
}

.product-card:hover .cover span {
    transform: scale(1.12) rotate(-4deg);
}

.low {
    position: absolute;
    top: 10px;
    right: 10px;
    font-style: normal;
    font-size: 0.75rem;
    padding: 0.15rem 0.5rem;
    border-radius: 999px;
    background: rgba(220, 38, 38, 0.1);
    color: var(--danger);
}

.body {
    padding: 0.9rem 1rem 1rem;
}

.name {
    font-weight: 700;
    text-decoration: none;
    color: #111827;
}

.desc {
    margin: 0.4rem 0 0.8rem;
    font-size: 0.85rem;
    min-height: 2.4em;
}

.row {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-bottom: 0.8rem;
}

.price {
    font-size: 1.1rem;
}

.stock {
    font-size: 0.8rem;
}

.add {
    width: 100%;
}
</style>
