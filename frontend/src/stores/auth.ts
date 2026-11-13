import { defineStore } from 'pinia';
import { api, type User } from '../api/client';

export const useAuthStore = defineStore('auth', {
    state: () => ({
        user: null as User | null,
        loaded: false
    }),
    getters: {
        isLoggedIn: (state) => state.user !== null,
        isOps: (state) =>
            state.user?.role === 'ops' || state.user?.role === 'admin'
    },
    actions: {
        async load() {
            try {
                this.user = await api.get<User>('/auth/me');
            } catch {
                this.user = null;
            } finally {
                this.loaded = true;
            }
        },
        async login(email: string, password: string) {
            this.user = await api.post<User>('/auth/login', { email, password });
        },
        async register(email: string, password: string, displayName: string) {
            this.user = await api.post<User>('/auth/register', {
                email,
                password,
                display_name: displayName
            });
        },
        async logout() {
            await api.post('/auth/logout');
            this.user = null;
        }
    }
});
