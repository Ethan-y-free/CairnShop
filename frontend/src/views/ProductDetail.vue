<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { api, type Product, formatCents } from '../api/client';
import { useAuthStore } from '../stores/auth';
import { useCartStore } from '../stores/cart';
import ProductImage from '../components/ProductImage.vue';

const route = useRoute();
const router = useRouter();
const auth = useAuthStore();
const cart = useCartStore();

const product = ref<Product | null>(null);
const quantity = ref(1);
const loading = ref(true);
const adding = ref(false);

const discount = computed(() => {
    const item = product.value;
    if (!item || item.original_price_cents <= item.price_cents) {
        return 0;
    }
    return Math.round(
        (1 - item.price_cents / item.original_price_cents) * 100
    );
});

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
                <ProductImage
                    :src="product.image_url"
                    :alt="product.name"
                    :label="product.name"
                />
                <em v-if="product.stock <= 5" class="low">库存紧张</em>
            </div>
            <div class="info">
                <div class="tags">
                    <el-tag effect="plain" round>{{ product.sku }}</el-tag>
                    <el-tag v-if="discount > 0" type="danger" round effect="dark">
                        双十一 -{{ discount }}%
                    </el-tag>
                </div>
                <h1>{{ product.name }}</h1>
                <p class="muted">{{ product.description }}</p>
                <div class="price-row">
                    <span class="price">{{ formatCents(product.price_cents) }}</span>
                    <s v-if="discount > 0" class="original">
                        {{ formatCents(product.original_price_cents) }}
                    </s>
                </div>
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
                        {{ product.stock <= 0 ? '已售罄' : '加入购物车' }}
                    </el-button>
                </div>
                <div class="assurance muted">
                    正品保障 · 7 天无理由 · 满 99 包邮
                </div>
            </div>
        </div>

        <section v-if="product" class="detail-section fade-up">
            <h2>商品详情</h2>
            <p>{{ product.detail }}</p>
            <ul>
                <li>商品编号：{{ product.sku }}</li>
                <li>当前库存：{{ product.stock }} 件</li>
                <li>双十一期间下单，按促销规则自动计算优惠。</li>
            </ul>
        </section>
    </div>
</template>

<style scoped>
.detail {
    display: grid;
    grid-template-columns: minmax(260px, 420px) 1fr;
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
    min-height: 320px;
    border-radius: 16px;
    overflow: hidden;
    background: linear-gradient(135deg, rgba(37, 99, 235, 0.14), rgba(124, 58, 237, 0.18));
}

.low {
    position: absolute;
    top: 12px;
    right: 12px;
    font-style: normal;
    font-size: 0.75rem;
    padding: 0.2rem 0.6rem;
    border-radius: 999px;
    background: rgba(15, 23, 42, 0.72);
    color: #fff;
}

.tags {
    display: flex;
    gap: 0.5rem;
    flex-wrap: wrap;
}

h1 {
    margin: 0.7rem 0 0.4rem;
    letter-spacing: -0.03em;
}

.price-row {
    display: flex;
    align-items: baseline;
    gap: 0.7rem;
    margin: 1rem 0 0.3rem;
}

.price {
    font-size: 2rem;
    font-weight: 800;
    color: var(--danger);
    letter-spacing: -0.04em;
}

.original {
    color: var(--muted);
    font-size: 1rem;
}

.meta {
    margin: 0 0 1.2rem;
}

.actions {
    display: flex;
    gap: 0.8rem;
    align-items: center;
}

.assurance {
    margin-top: 1.2rem;
    font-size: 0.85rem;
}

.detail-section {
    margin-top: 1.25rem;
    padding: 1.5rem;
    border-radius: 20px;
    background: #fff;
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
}

.detail-section h2 {
    margin: 0 0 0.8rem;
    letter-spacing: -0.03em;
}

.detail-section p {
    line-height: 1.8;
}

.detail-section ul {
    color: var(--muted);
    line-height: 1.9;
    padding-left: 1.2rem;
}

@media (max-width: 720px) {
    .detail {
        grid-template-columns: 1fr;
    }
}
</style>
