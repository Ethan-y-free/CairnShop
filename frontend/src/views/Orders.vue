<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { api, type Order, formatCents } from '../api/client';

const orders = ref<Order[]>([]);
const loading = ref(false);

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
    try {
        await api.post(`/orders/${order.order_no}/pay`);
        ElMessage.success('支付成功');
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '支付失败');
    }
}

async function cancel(order: Order) {
    try {
        await api.post(`/orders/${order.order_no}/cancel`);
        ElMessage.success('订单已取消');
        await load();
    } catch (error) {
        ElMessage.error((error as Error).message || '取消失败');
    }
}

onMounted(load);
</script>

<template>
    <div v-loading="loading">
        <h2>我的订单</h2>
        <el-table :data="orders" empty-text="还没有订单">
            <el-table-column prop="order_no" label="订单号" width="240" />
            <el-table-column label="金额" width="120">
                <template #default="{ row }">
                    {{ formatCents(row.total_cents) }}
                </template>
            </el-table-column>
            <el-table-column label="优惠" width="120">
                <template #default="{ row }">
                    {{ formatCents(row.discount_cents) }}
                </template>
            </el-table-column>
            <el-table-column prop="status" label="状态" width="120">
                <template #default="{ row }">
                    <el-tag :type="row.status === 'paid' ? 'success' : 'warning'">
                        {{ row.status }}
                    </el-tag>
                </template>
            </el-table-column>
            <el-table-column prop="created_at" label="时间" width="200" />
            <el-table-column label="操作" width="180">
                <template #default="{ row }">
                    <el-button
                        v-if="row.status === 'pending'"
                        type="primary"
                        link
                        @click="pay(row)"
                    >
                        支付
                    </el-button>
                    <el-button
                        v-if="row.status === 'pending'"
                        type="danger"
                        link
                        @click="cancel(row)"
                    >
                        取消
                    </el-button>
                </template>
            </el-table-column>
        </el-table>
    </div>
</template>
