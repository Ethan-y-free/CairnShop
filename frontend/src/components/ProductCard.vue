<script setup lang="ts">
import { computed } from 'vue';
import { formatCents, type Product } from '../api/client';
import ProductImage from './ProductImage.vue';

const props = defineProps<{
    product: Product;
    adding: boolean;
}>();

defineEmits<{
    add: [product: Product];
}>();

const discount = computed(() => {
    const original = props.product.original_price_cents;
    const price = props.product.price_cents;
    if (original > 0 && original > price) {
        return Math.round((1 - price / original) * 100);
    }
    return 0;
});
</script>

<template>
    <div class="product-card card-hover">
        <div class="cover">
            <ProductImage
                :src="product.image_url"
                :alt="product.name"
                :label="product.name"
            />
            <span v-if="discount > 0" class="discount">-{{ discount }}%</span>
            <em v-if="product.stock <= 5" class="low">
                仅剩 {{ product.stock }}
            </em>
        </div>
        <div class="body">
            <router-link :to="`/product/${product.id}`" class="name">
                {{ product.name }}
            </router-link>
            <p class="desc muted">{{ product.description }}</p>
            <div class="row">
                <div class="prices">
                    <span class="price">{{ formatCents(product.price_cents) }}</span>
                    <s v-if="discount > 0" class="original">
                        {{ formatCents(product.original_price_cents) }}
                    </s>
                </div>
                <span class="stock muted">库存 {{ product.stock }}</span>
            </div>
            <el-button
                type="primary"
                round
                class="add"
                :loading="adding"
                :disabled="product.stock <= 0"
                @click="$emit('add', product)"
            >
                {{ product.stock <= 0 ? '已售罄' : '加入购物车' }}
            </el-button>
        </div>
    </div>
</template>

<style scoped>
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
    height: 180px;
    overflow: hidden;
    background: linear-gradient(135deg, rgba(37, 99, 235, 0.14), rgba(124, 58, 237, 0.18));
}

.discount {
    position: absolute;
    top: 10px;
    left: 10px;
    padding: 0.15rem 0.55rem;
    border-radius: 999px;
    font-size: 0.75rem;
    font-weight: 800;
    color: #fff;
    background: linear-gradient(120deg, #ef4444, #f97316);
    box-shadow: 0 6px 16px rgba(239, 68, 68, 0.35);
}

.low {
    position: absolute;
    top: 10px;
    right: 10px;
    font-style: normal;
    font-size: 0.72rem;
    padding: 0.15rem 0.5rem;
    border-radius: 999px;
    background: rgba(15, 23, 42, 0.72);
    color: #fff;
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
    align-items: flex-end;
    justify-content: space-between;
    margin-bottom: 0.8rem;
}

.prices {
    display: flex;
    align-items: baseline;
    gap: 0.45rem;
}

.price {
    font-size: 1.15rem;
}

.original {
    color: var(--muted);
    font-size: 0.8rem;
}

.stock {
    font-size: 0.78rem;
}

.add {
    width: 100%;
}
</style>
