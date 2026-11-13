<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { api, type Order, formatCents } from '../api/client';

const orders = ref<Order[]>([]);
const loading = ref(false);
const actionOrder = ref<string | null>(null);

function statusType(status: string): 'success' | 'warning' | 'danger' | 'info' {
    if (status === 'paid' || status === 'shipped') {
        return 'success';
    }
    if (status === 'cancelled') {
        return 'info';
    }
    if (status === 'refunded') {
        return 'danger';
    }
    return 'warning';
}

async function load() {
    loading.value = true;
    try {
        const data = await api.get<{ orders: Order[] }>('/orders');
        orders.value = data.orders;
    } catch (error) {
        ElMessage.error((error as Error).message || '加载订单失败');
    } finally {
        loading.value = false;
    }
}

async function pay(order: Order) {
    actionOrder.value = order.order_no;
    try {
        await api.post(`/orders/${order.order_no}/pay`);
        ElMessage.success('支付成功');
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '支付失败');
    } finally {
        actionOrder.value = null;
    }
}

async function cancel(order: Order) {
    actionOrder.value = order.order_no;
    try {
        await api.post(`/orders/${order.order_no}/cancel`);
        ElMessage.success('订单已取消');
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '取消失败');
    } finally {
        actionOrder.value = null;
    }
}

onMounted(load);
</script>

<template>
    <div>
        <div class="head fade-up">
            <h1>我的订单</h1>
            <span class="muted">{{ orders.length }} 个订单</span>
        </div>

        <div v-if="loading" class="loading">
            <el-skeleton :rows="4" animated />
        </div>

        <el-empty v-else-if="orders.length === 0" description="还没有订单">
            <el-button type="primary" round @click="$router.push('/')">
                去逛逛
            </el-button>
        </el-empty>

        <div v-else class="list">
            <article
                v-for="(order, index) in orders"
                :key="order.id"
                class="order card-hover fade-up"
                :style="{ animationDelay: `${index * 45}ms` }"
            >
                <header>
                    <div>
                        <span class="no">{{ order.order_no }}</span>
                        <span class="muted">{{ order.created_at }}</span>
                    </div>
                    <el-tag :type="statusType(order.status)" round>
                        {{ order.status }}
                    </el-tag>
                </header>

                <div class="items">
                    <span
                        v-for="item in order.items"
                        :key="item.product_id"
                        class="item"
                    >
                        {{ item.product_name }} × {{ item.quantity }}
                    </span>
                </div>

                <footer>
                    <div class="money">
                        <span class="muted">
                            小计 {{ formatCents(order.subtotal_cents) }} ·
                            优惠 {{ formatCents(order.discount_cents) }}
                        </span>
                        <strong>{{ formatCents(order.total_cents) }}</strong>
                    </div>
                    <div v-if="order.status === 'pending'" class="actions">
                        <el-button
                            type="primary"
                            round
                            :loading="actionOrder === order.order_no"
                            @click="pay(order)"
                        >
                            支付
                        </el-button>
                        <el-button
                            round
                            :loading="actionOrder === order.order_no"
                            @click="cancel(order)"
                        >
                            取消
                        </el-button>
                    </div>
                </footer>
            </article>
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

.list {
    display: grid;
    gap: 0.9rem;
}

.order {
    padding: 1rem 1.2rem;
    border-radius: var(--radius);
    background: #fff;
    border: 1px solid var(--border);
    box-shadow: var(--shadow);
}

.order header,
.order footer {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
}

.order header > div {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
}

.no {
    font-weight: 700;
    letter-spacing: 0.01em;
}

.items {
    display: flex;
    flex-wrap: wrap;
    gap: 0.45rem;
    margin: 0.9rem 0;
}

.item {
    font-size: 0.85rem;
    padding: 0.2rem 0.6rem;
    border-radius: 999px;
    background: #f3f4f6;
    color: #374151;
}

.money {
    display: flex;
    align-items: baseline;
    gap: 0.8rem;
}

.money strong {
    font-size: 1.25rem;
    color: var(--danger);
}

.actions {
    display: flex;
    gap: 0.5rem;
}
</style>
