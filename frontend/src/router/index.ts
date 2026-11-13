import { createRouter, createWebHistory } from 'vue-router';
import { useAuthStore } from '../stores/auth';

import Home from '../views/Home.vue';
import ProductDetail from '../views/ProductDetail.vue';
import Login from '../views/Login.vue';
import Register from '../views/Register.vue';
import Cart from '../views/Cart.vue';
import Orders from '../views/Orders.vue';
import Ops from '../views/Ops.vue';

const router = createRouter({
    history: createWebHistory(),
    routes: [
        { path: '/', component: Home },
        { path: '/product/:id', component: ProductDetail },
        { path: '/login', component: Login },
        { path: '/register', component: Register },
        { path: '/cart', component: Cart, meta: { requiresAuth: true } },
        { path: '/orders', component: Orders, meta: { requiresAuth: true } },
        { path: '/ops', component: Ops, meta: { requiresOps: true } }
    ]
});

router.beforeEach(async (to) => {
    const auth = useAuthStore();
    if (!auth.loaded) {
        await auth.load();
    }
    if (Boolean(to.meta.requiresAuth) && !auth.isLoggedIn) {
        return { path: '/login', query: { redirect: to.fullPath } };
    }
    if (Boolean(to.meta.requiresOps) && !auth.isOps) {
        return { path: '/' };
    }
    return true;
});

export default router;
