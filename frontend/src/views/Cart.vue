<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { api, type Cart, type CartItem, type Order, formatCents } from '../api/client';
import { useCartStore } from '../stores/cart';
import ProductImage from '../components/ProductImage.vue';

const router = useRouter();
const cartStore = useCartStore();

const cart = ref<Cart>({ items: [], subtotal_cents: 0 });
const loading = ref(false);
const checkingOut = ref(false);
const busyId = ref<number | null>(null);

const empty = computed(() => cart.value.items.length === 0);

function newIdempotencyKey(): string {
    const uuid = (crypto as Crypto & { randomUUID?: () => string }).randomUUID;
    if (typeof uuid === 'function') {
        return uuid.call(crypto);
    }
    return `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

async function load() {
    loading.value = true;
    try {
        cart.value = await api.get<Cart>('/cart');
        cartStore.setFromCart(cart.value);
    } catch (error) {
        ElMessage.error((error as Error).message || '加载购物车失败');
    } finally {
        loading.value = false;
    }
}

async function setQuantity(item: CartItem, quantity: number) {
    if (quantity === item.quantity) {
        return;
    }
    busyId.value = item.product_id;
    try {
        await api.patch(`/cart/items/${item.product_id}`, { quantity });
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '更新数量失败');
    } finally {
        busyId.value = null;
    }
}

async function remove(item: CartItem) {
    busyId.value = item.product_id;
    try {
        await api.delete(`/cart/items/${item.product_id}`);
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '移除失败');
    } finally {
        busyId.value = null;
    }
}

async function checkout() {
    checkingOut.value = true;
    try {
        const order = await api.post<Order>('/orders', undefined, {
            'Idempotency-Key': newIdempotencyKey()
        });
        ElMessage.success(`下单成功：${order.order_no}`);
        cartStore.clear();
        router.push('/orders');
    } catch (error) {
        ElMessage.error((error as Error).message || '下单失败');
    } finally {
        checkingOut.value = false;
    }
}

onMounted(load);
</script>

<template>
    <div>
        <div class="head fade-up">
            <h1>购物车</h1>
            <span class="muted">{{ cart.items.length }} 种商品</span>
        </div>

        <div v-if="loading" class="loading">
            <el-skeleton :rows="4" animated />
        </div>

        <el-empty v-else-if="empty" description="购物车还是空的">
            <el-button type="primary" round @click="router.push('/')">
                去逛逛
            </el-button>
        </el-empty>

        <div v-else class="layout">
            <div class="items">
                <transition-group name="list" tag="div">
                    <div
                        v-for="item in cart.items"
                        :key="item.product_id"
                        class="item card-hover"
                    >
                        <div class="thumb">
                            <ProductImage
                                :src="item.image_url"
                                :alt="item.name"
                                :label="item.name"
                            />
                        </div>
                        <div class="info">
                            <div class="name">{{ item.name }}</div>
                            <div class="muted">{{ item.sku }}</div>
                            <div class="price">{{ formatCents(item.price_cents) }}</div>
                        </div>
                        <el-input-number
                            :model-value="item.quantity"
                            :min="1"
                            :max="Math.min(item.stock, 99)"
                            :disabled="busyId === item.product_id"
                            @change="(value) => setQuantity(item, value ?? 1)"
                        />
                        <div class="line-total">
                            {{ formatCents(item.price_cents * item.quantity) }}
                        </div>
                        <el-button
                            type="danger"
                            link
                            :loading="busyId === item.product_id"
                            @click="remove(item)"
                        >
                            移除
                        </el-button>
                    </div>
                </transition-group>
            </div>

            <aside class="summary fade-up">
                <h3>订单摘要</h3>
                <div class="row">
                    <span class="muted">商品合计</span>
                    <span>{{ formatCents(cart.subtotal_cents) }}</span>
                </div>
                <div class="row">
                    <span class="muted">优惠</span>
                    <span class="muted">结算时按当前配置计算</span>
                </div>
                <div class="total">
                    <span>应付</span>
                    <strong>{{ formatCents(cart.subtotal_cents) }}</strong>
                </div>
                <el-button
                    type="primary"
                    round
                    class="checkout"
                    :loading="checkingOut"
                    @click="checkout"
                >
                    去结算
                </el-button>
            </aside>
        </div>
    </div>
</template>

<style scoped>
.head {
    display: flex;
    align-items: baseline;
    gap: 0.8rem;
    margin-bottom: 1rem;
}

h1 {
    margin: 0;
    letter-spacing: -0.03em;
}

.loading {
    background: #fff;
    border-radius: var(--radius);
    padding: 1.5rem;
    box-shadow: var(--shadow);
}

.layout {
    display: grid;
    grid-template-columns: 1fr 320px;
    gap: 1.25rem;
}

.items {
    display: grid;
    gap: 0.8rem;
}

.item {
    display: grid;
    grid-template-columns: 56px 1fr 150px 100px 60px;
    align-items: center;
    gap: 1rem;
    padding: 0.9rem 1rem;
    border-radius: var(--radius);
    background: #fff;
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
}

.thumb {
    width: 56px;
    height: 56px;
    border-radius: 12px;
    overflow: hidden;
    background: linear-gradient(135deg, rgba(37, 99, 235, 0.14), rgba(124, 58, 237, 0.18));
}

.name {
    font-weight: 700;
}

.line-total {
    font-weight: 700;
    text-align: right;
}

.summary {
    position: sticky;
    top: 5.2rem;
    height: fit-content;
    padding: 1.2rem;
    border-radius: var(--radius);
    background: #fff;
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
}

.summary h3 {
    margin: 0 0 0.9rem;
}

.row {
    display: flex;
    justify-content: space-between;
    margin-bottom: 0.6rem;
}

.total {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin: 1rem 0;
    padding-top: 1rem;
    border-top: 1px dashed var(--border);
}

.total strong {
    font-size: 1.6rem;
    color: var(--danger);
}

.checkout {
    width: 100%;
}

.list-enter-active,
.list-leave-active {
    transition: all 0.25s ease;
}

.list-enter-from,
.list-leave-to {
    opacity: 0;
    transform: translateX(12px);
}

@media (max-width: 900px) {
    .layout {
        grid-template-columns: 1fr;
    }
    .item {
        grid-template-columns: 48px 1fr;
    }
    .item > *:nth-child(n + 3) {
        grid-column: 2;
    }
    .summary {
        position: static;
    }
}
</style>
