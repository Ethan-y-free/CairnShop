<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import {
    api,
    type AuditEntry,
    type Category,
    type ConfigSnapshot,
    type OpsStatus,
    type Product,
    formatCents
} from '../api/client';

interface ConfigHistoryEntry {
    version: number;
    value: Record<string, unknown>;
    updated_at: string;
    updated_by: string;
}

const tab = ref('config');

const snapshot = ref<ConfigSnapshot | null>(null);
const configKeys = ref<string[]>([]);
const selectedKey = ref('');
const editor = ref('');
const history = ref<ConfigHistoryEntry[]>([]);
const configLoading = ref(false);

const products = ref<Product[]>([]);
const categories = ref<Category[]>([]);
const productDialog = ref(false);
const editing = ref<Product | null>(null);
const form = ref({
    category_id: 1,
    sku: '',
    name: '',
    description: '',
    price_cents: 0,
    stock: 0,
    status: 'draft'
});

const audit = ref<AuditEntry[]>([]);
const status = ref<OpsStatus | null>(null);
const cairnkvText = computed(() =>
    status.value ? JSON.stringify(status.value.cairnkv, null, 2) : ''
);

async function loadConfig() {
    configLoading.value = true;
    try {
        snapshot.value = await api.get<ConfigSnapshot>('/ops/config');
        configKeys.value = Object.keys(snapshot.value.keys);
        if (!selectedKey.value && configKeys.value.length > 0) {
            selectKey(configKeys.value[0]);
        }
    } catch (error) {
        ElMessage.error((error as Error).message || '加载配置失败');
    } finally {
        configLoading.value = false;
    }
}

function selectKey(key: string) {
    selectedKey.value = key;
    const raw = snapshot.value?.keys[key] ?? '{}';
    try {
        editor.value = JSON.stringify(JSON.parse(raw), null, 2);
    } catch {
        editor.value = raw;
    }
    loadHistory();
}

async function loadHistory() {
    if (!selectedKey.value) {
        history.value = [];
        return;
    }
    try {
        const data = await api.get<{ history: ConfigHistoryEntry[] }>(
            `/ops/config/history?key=${encodeURIComponent(selectedKey.value)}`
        );
        history.value = data.history ?? [];
    } catch {
        history.value = [];
    }
}

async function saveConfig() {
    if (!selectedKey.value) {
        return;
    }
    let value: unknown;
    try {
        value = JSON.parse(editor.value);
    } catch {
        ElMessage.error('配置必须是合法 JSON');
        return;
    }
    if (typeof value !== 'object' || value === null || Array.isArray(value)) {
        ElMessage.error('配置必须是 JSON 对象');
        return;
    }
    try {
        await api.put('/ops/config', { key: selectedKey.value, value });
        ElMessage.success('配置已保存');
        await loadConfig();
        await loadHistory();
    } catch (error) {
        ElMessage.error((error as Error).message || '保存失败');
    }
}

async function rollback(version: number) {
    try {
        await api.post('/ops/config/rollback', {
            key: selectedKey.value,
            version
        });
        ElMessage.success(`已回滚到 v${version}`);
        await loadConfig();
        await loadHistory();
    } catch (error) {
        ElMessage.error((error as Error).message || '回滚失败');
    }
}

async function loadCategories() {
    try {
        const data = await api.get<{ categories: Category[] }>(
            '/catalog/categories'
        );
        categories.value = data.categories;
    } catch {
        categories.value = [];
    }
}

async function loadProducts() {
    try {
        const data = await api.get<{ products: Product[] }>(
            '/ops/catalog/products?limit=100'
        );
        products.value = data.products;
    } catch (error) {
        ElMessage.error((error as Error).message || '加载商品失败');
    }
}

function openCreate() {
    editing.value = null;
    form.value = {
        category_id: categories.value[0]?.id ?? 1,
        sku: '',
        name: '',
        description: '',
        price_cents: 0,
        stock: 0,
        status: 'draft'
    };
    productDialog.value = true;
}

function openEdit(product: Product) {
    editing.value = product;
    form.value = {
        category_id: product.category_id,
        sku: product.sku,
        name: product.name,
        description: product.description,
        price_cents: product.price_cents,
        stock: product.stock,
        status: product.status
    };
    productDialog.value = true;
}

async function saveProduct() {
    try {
        if (editing.value) {
            await api.patch(
                `/ops/catalog/products/${editing.value.id}`,
                form.value
            );
        } else {
            await api.post('/ops/catalog/products', form.value);
        }
        productDialog.value = false;
        await loadProducts();
        ElMessage.success('商品已保存');
    } catch (error) {
        ElMessage.error((error as Error).message || '保存失败');
    }
}

async function loadAudit() {
    try {
        const data = await api.get<{ entries: AuditEntry[] }>(
            '/ops/audit?limit=100'
        );
        audit.value = data.entries;
    } catch (error) {
        ElMessage.error((error as Error).message || '加载审计失败');
    }
}

async function loadStatus() {
    try {
        status.value = await api.get<OpsStatus>('/ops/status');
    } catch (error) {
        ElMessage.error((error as Error).message || '加载状态失败');
    }
}

onMounted(async () => {
    await Promise.all([
        loadCategories(),
        loadConfig(),
        loadProducts(),
        loadAudit(),
        loadStatus()
    ]);
});
</script>

<template>
    <div>
        <h2>运营台</h2>
        <el-tabs v-model="tab">
            <el-tab-pane label="配置" name="config">
                <div v-loading="configLoading">
                    <el-alert
                        v-if="snapshot"
                        :title="`配置年龄 ${snapshot.age_ms} ms · ${snapshot.stale ? '已过期' : '新鲜'} · ${snapshot.connected ? '已连接 CairnKV' : 'CairnKV 未连接'}`"
                        :type="snapshot.stale ? 'warning' : 'success'"
                        :closable="false"
                        style="margin-bottom: 12px"
                    />
                    <el-row :gutter="16">
                        <el-col :span="8">
                            <el-select
                                v-model="selectedKey"
                                placeholder="选择配置 key"
                                style="width: 100%"
                                @change="selectKey"
                            >
                                <el-option
                                    v-for="key in configKeys"
                                    :key="key"
                                    :label="key"
                                    :value="key"
                                />
                            </el-select>
                            <el-button
                                type="primary"
                                style="margin-top: 12px; width: 100%"
                                :disabled="!selectedKey"
                                @click="saveConfig"
                            >
                                保存
                            </el-button>
                            <h4 style="margin-top: 20px">历史版本</h4>
                            <el-table :data="history" size="small">
                                <el-table-column prop="version" label="版本" width="70" />
                                <el-table-column prop="updated_by" label="操作人" />
                                <el-table-column label="操作" width="80">
                                    <template #default="{ row }">
                                        <el-button
                                            type="warning"
                                            link
                                            @click="rollback(row.version)"
                                        >
                                            回滚
                                        </el-button>
                                    </template>
                                </el-table-column>
                            </el-table>
                        </el-col>
                        <el-col :span="16">
                            <el-input
                                v-model="editor"
                                type="textarea"
                                :rows="16"
                                placeholder="JSON 配置"
                            />
                        </el-col>
                    </el-row>
                </div>
            </el-tab-pane>

            <el-tab-pane label="商品" name="products">
                <el-button type="primary" style="margin-bottom: 12px" @click="openCreate">
                    新建商品
                </el-button>
                <el-table :data="products">
                    <el-table-column prop="sku" label="SKU" width="120" />
                    <el-table-column prop="name" label="名称" />
                    <el-table-column label="价格" width="110">
                        <template #default="{ row }">
                            {{ formatCents(row.price_cents) }}
                        </template>
                    </el-table-column>
                    <el-table-column prop="stock" label="库存" width="90" />
                    <el-table-column prop="status" label="状态" width="100" />
                    <el-table-column label="操作" width="90">
                        <template #default="{ row }">
                            <el-button type="primary" link @click="openEdit(row)">
                                编辑
                            </el-button>
                        </template>
                    </el-table-column>
                </el-table>

                <el-dialog v-model="productDialog" title="商品" width="520px">
                    <el-form label-position="top">
                        <el-form-item label="分类">
                            <el-select v-model="form.category_id" style="width: 100%">
                                <el-option
                                    v-for="category in categories"
                                    :key="category.id"
                                    :label="category.name"
                                    :value="category.id"
                                />
                            </el-select>
                        </el-form-item>
                        <el-form-item label="SKU">
                            <el-input v-model="form.sku" />
                        </el-form-item>
                        <el-form-item label="名称">
                            <el-input v-model="form.name" />
                        </el-form-item>
                        <el-form-item label="描述">
                            <el-input v-model="form.description" type="textarea" />
                        </el-form-item>
                        <el-form-item label="价格（分）">
                            <el-input-number v-model="form.price_cents" :min="0" />
                        </el-form-item>
                        <el-form-item label="库存">
                            <el-input-number v-model="form.stock" :min="0" />
                        </el-form-item>
                        <el-form-item label="状态">
                            <el-select v-model="form.status" style="width: 100%">
                                <el-option label="draft" value="draft" />
                                <el-option label="active" value="active" />
                                <el-option label="archived" value="archived" />
                            </el-select>
                        </el-form-item>
                    </el-form>
                    <template #footer>
                        <el-button @click="productDialog = false">取消</el-button>
                        <el-button type="primary" @click="saveProduct">保存</el-button>
                    </template>
                </el-dialog>
            </el-tab-pane>

            <el-tab-pane label="审计" name="audit">
                <el-table :data="audit">
                    <el-table-column prop="created_at" label="时间" width="210" />
                    <el-table-column prop="actor_role" label="角色" width="100" />
                    <el-table-column prop="action" label="动作" />
                    <el-table-column prop="target_type" label="对象" width="110" />
                    <el-table-column prop="target_id" label="对象 ID" />
                </el-table>
            </el-tab-pane>

            <el-tab-pane label="状态" name="status">
                <el-card v-if="status" style="margin-bottom: 12px">
                    <p>MySQL：{{ status.mysql_ok ? 'OK' : '不可用' }}</p>
                    <p>
                        配置缓存：{{ status.config.age_ms }} ms ·
                        {{ status.config.stale ? '已过期' : '新鲜' }} ·
                        {{ status.config.keys }} 个 key
                    </p>
                </el-card>
                <el-card v-if="status">
                    <h4>CairnKV</h4>
                    <pre>{{ cairnkvText }}</pre>
                </el-card>
            </el-tab-pane>
        </el-tabs>
    </div>
</template>

<style scoped>
pre {
    background: #f3f4f6;
    padding: 1rem;
    border-radius: 6px;
    overflow: auto;
}
</style>
