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
        async login(identifier: string, password: string) {
            this.user = await api.post<User>('/auth/login', {
                identifier,
                password
            });
        },
        async register(payload: {
            email?: string;
            phone?: string;
            password: string;
            display_name: string;
        }) {
            this.user = await api.post<User>('/auth/register', payload);
        },
        async logout() {
            await api.post('/auth/logout');
            this.user = null;
        }
    }
});
