import { defineStore } from 'pinia';
import { api, type Cart } from '../api/client';

export const useCartStore = defineStore('cart', {
    state: () => ({
        count: 0
    }),
    actions: {
        setFromCart(cart: Cart) {
            this.count = cart.items.reduce((sum, item) => sum + item.quantity, 0);
        },
        async refresh() {
            try {
                const cart = await api.get<Cart>('/cart');
                this.setFromCart(cart);
            } catch {
                this.count = 0;
            }
        },
        clear() {
            this.count = 0;
        }
    }
});
