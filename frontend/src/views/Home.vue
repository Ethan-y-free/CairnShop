<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { useRouter } from 'vue-router';
import { api, type Category, type Product } from '../api/client';
import { useAuthStore } from '../stores/auth';
import { useCartStore } from '../stores/cart';
import ProductCard from '../components/ProductCard.vue';

const auth = useAuthStore();
const cart = useCartStore();
const router = useRouter();

const categories = ref<Category[]>([]);
const products = ref<Product[]>([]);
const search = ref('');
const categoryId = ref<number | null>(null);
const loading = ref(false);
const addingId = ref<number | null>(null);

const promoProducts = computed(() =>
    products.value.filter(
        (product) => product.original_price_cents > product.price_cents
    )
);
const regularProducts = computed(() =>
    products.value.filter(
        (product) => !(product.original_price_cents > product.price_cents)
    )
);
const filtering = computed(
    () => search.value.length > 0 || categoryId.value !== null
);

async function load() {
    loading.value = true;
    try {
        const params = new URLSearchParams();
        params.set('limit', '48');
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
            <div class="hero-copy">
                <p class="eyebrow">DOUBLE 11 · 限时特惠</p>
                <h1>
                    双十一提前购<br />
                    <span class="gradient-text">全网低价 · 品类齐全</span>
                </h1>
                <p class="sub muted">
                    爆款直降、满额再减，正品保障，极速发货。
                </p>
                <div class="hero-actions">
                    <el-button type="primary" round size="large" @click="load">
                        立即抢购
                    </el-button>
                    <span class="muted">限时 3 天 · 售完即止</span>
                </div>
            </div>
            <div class="hero-card">
                <span class="pulse"></span>
                <p>双十一到手价</p>
                <strong>5 折起</strong>
                <span class="muted">已售 12,000+ 件</span>
            </div>
        </section>

        <div class="filters fade-up">
            <el-input
                v-model="search"
                placeholder="搜索商品，例如：耳机、台灯"
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

        <template v-else>
            <section
                v-if="filtering"
                class="section fade-up"
            >
                <header class="section-head">
                    <h2>搜索结果</h2>
                    <span class="muted">{{ products.length }} 件商品</span>
                </header>
                <el-row :gutter="18">
                    <el-col
                        v-for="(product, index) in products"
                        :key="product.id"
                        :xs="24"
                        :sm="12"
                        :md="8"
                        :lg="6"
                        class="card-col fade-up"
                        :style="{ animationDelay: `${index * 40}ms` }"
                    >
                        <ProductCard
                            :product="product"
                            :adding="addingId === product.id"
                            @add="add"
                        />
                    </el-col>
                </el-row>
                <el-empty v-if="products.length === 0" description="没有找到商品" />
            </section>

            <template v-else>
                <section
                    v-if="promoProducts.length > 0"
                    class="section promo-section fade-up"
                >
                    <header class="section-head">
                        <div>
                            <p class="section-eyebrow">DOUBLE 11</p>
                            <h2>双十一狂欢</h2>
                        </div>
                        <span class="muted">限时直降，手慢无</span>
                    </header>
                    <el-row :gutter="18">
                        <el-col
                            v-for="(product, index) in promoProducts"
                            :key="product.id"
                            :xs="24"
                            :sm="12"
                            :md="8"
                            :lg="6"
                            class="card-col fade-up"
                            :style="{ animationDelay: `${index * 40}ms` }"
                        >
                            <ProductCard
                                :product="product"
                                :adding="addingId === product.id"
                                @add="add"
                            />
                        </el-col>
                    </el-row>
                </section>

                <section
                    v-if="regularProducts.length > 0"
                    class="section fade-up"
                >
                    <header class="section-head">
                        <h2>原价好物</h2>
                        <span class="muted">日常价格，品质精选</span>
                    </header>
                    <el-row :gutter="18">
                        <el-col
                            v-for="(product, index) in regularProducts"
                            :key="product.id"
                            :xs="24"
                            :sm="12"
                            :md="8"
                            :lg="6"
                            class="card-col fade-up"
                            :style="{ animationDelay: `${index * 40}ms` }"
                        >
                            <ProductCard
                                :product="product"
                                :adding="addingId === product.id"
                                @add="add"
                            />
                        </el-col>
                    </el-row>
                </section>
            </template>
        </template>
    </div>
</template>

<style scoped>
.hero {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 2rem;
    padding: 2.4rem 2rem;
    margin-bottom: 1.5rem;
    border-radius: 22px;
    background:
        radial-gradient(600px 300px at 85% 20%, rgba(239, 68, 68, 0.18), transparent 60%),
        linear-gradient(120deg, rgba(37, 99, 235, 0.12), rgba(124, 58, 237, 0.12)),
        #fff;
    border: 1px solid rgba(37, 99, 235, 0.12);
    overflow: hidden;
}

.hero-copy {
    max-width: 640px;
}

.eyebrow {
    margin: 0 0 0.5rem;
    font-size: 0.8rem;
    font-weight: 800;
    letter-spacing: 0.2em;
    color: #ef4444;
}

h1 {
    margin: 0;
    font-size: clamp(1.9rem, 4.4vw, 2.8rem);
    line-height: 1.12;
    letter-spacing: -0.04em;
}

.sub {
    margin: 0.9rem 0 1.1rem;
    font-size: 1rem;
}

.hero-actions {
    display: flex;
    align-items: center;
    gap: 1rem;
}

.hero-card {
    min-width: 190px;
    padding: 1.2rem 1.4rem;
    border-radius: 18px;
    background: rgba(255, 255, 255, 0.82);
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
    font-size: 2.2rem;
    letter-spacing: -0.05em;
    color: #ef4444;
}

.pulse {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #ef4444;
    box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.5);
    animation: pulse 2s infinite;
}

@keyframes pulse {
    0% {
        box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.5);
    }
    70% {
        box-shadow: 0 0 0 12px rgba(239, 68, 68, 0);
    }
    100% {
        box-shadow: 0 0 0 0 rgba(239, 68, 68, 0);
    }
}

.filters {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
    margin-bottom: 1.25rem;
}

.search {
    max-width: 340px;
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

.section {
    margin-bottom: 2rem;
}

.promo-section {
    padding: 1.2rem;
    border-radius: 20px;
    background:
        radial-gradient(700px 300px at 0% 0%, rgba(239, 68, 68, 0.10), transparent 55%),
        #fff5f5;
    border: 1px solid rgba(239, 68, 68, 0.18);
}

.section-head {
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
    gap: 1rem;
    margin-bottom: 1rem;
}

.section-head h2 {
    margin: 0;
    letter-spacing: -0.03em;
}

.section-eyebrow {
    margin: 0 0 0.2rem;
    font-size: 0.72rem;
    font-weight: 800;
    letter-spacing: 0.18em;
    color: #ef4444;
}

.card-col {
    margin-bottom: 18px;
}

@media (max-width: 760px) {
    .hero {
        flex-direction: column;
        align-items: flex-start;
    }
    .hero-card {
        width: 100%;
    }
}
</style>
