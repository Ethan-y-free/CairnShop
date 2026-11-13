<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { ElMessage } from 'element-plus';
import { api, type Cart, type CartItem, type Order, formatCents } from '../api/client';

const router = useRouter();
const cart = ref<Cart>({ items: [], subtotal_cents: 0 });
const loading = ref(false);
const checkingOut = ref(false);

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
    try {
        await api.patch(`/cart/items/${item.product_id}`, { quantity });
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '更新数量失败');
    }
}

async function remove(item: CartItem) {
    try {
        await api.delete(`/cart/items/${item.product_id}`);
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '移除失败');
    }
}

async function checkout() {
    checkingOut.value = true;
    try {
        const order = await api.post<Order>('/orders', undefined, {
            'Idempotency-Key': newIdempotencyKey()
        });
        ElMessage.success(`下单成功：${order.order_no}`);
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
    <div v-loading="loading">
        <h2>购物车</h2>
        <el-table :data="cart.items" empty-text="购物车是空的">
            <el-table-column prop="name" label="商品" />
            <el-table-column label="单价" width="120">
                <template #default="{ row }">
                    {{ formatCents(row.price_cents) }}
                </template>
            </el-table-column>
            <el-table-column label="数量" width="160">
                <template #default="{ row }">
                    <el-input-number
                        :model-value="row.quantity"
                        :min="1"
                        :max="Math.min(row.stock, 99)"
                        @change="(value) => setQuantity(row, value ?? 1)"
                    />
                </template>
            </el-table-column>
            <el-table-column label="小计" width="120">
                <template #default="{ row }">
                    {{ formatCents(row.price_cents * row.quantity) }}
                </template>
            </el-table-column>
            <el-table-column label="操作" width="100">
                <template #default="{ row }">
                    <el-button type="danger" link @click="remove(row)">
                        移除
                    </el-button>
                </template>
            </el-table-column>
        </el-table>

        <div class="summary">
            <span>合计：{{ formatCents(cart.subtotal_cents) }}</span>
            <el-button
                type="primary"
                :disabled="empty"
                :loading="checkingOut"
                @click="checkout"
            >
                去结算
            </el-button>
        </div>
    </div>
</template>

<style scoped>
.summary {
    display: flex;
    justify-content: flex-end;
    align-items: center;
    gap: 1.5rem;
    margin-top: 1rem;
    font-weight: 600;
}
</style>
