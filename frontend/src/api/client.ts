export interface User {
    id: number;
    email: string;
    phone: string;
    display_name: string;
    role: string;
}

export interface Category {
    id: number;
    name: string;
    slug: string;
}

export interface Product {
    id: number;
    category_id: number;
    sku: string;
    name: string;
    description: string;
    image_url: string;
    detail: string;
    price_cents: number;
    original_price_cents: number;
    stock: number;
    status: string;
}

export interface CartItem {
    product_id: number;
    sku: string;
    name: string;
    image_url: string;
    price_cents: number;
    original_price_cents: number;
    stock: number;
    status: string;
    quantity: number;
}

export interface Cart {
    items: CartItem[];
    subtotal_cents: number;
}

export interface OrderItem {
    product_id: number;
    product_name: string;
    unit_price_cents: number;
    quantity: number;
}

export interface Order {
    id: number;
    order_no: string;
    user_id: number;
    status: string;
    subtotal_cents: number;
    discount_cents: number;
    total_cents: number;
    promo: Record<string, unknown> | null;
    config_version: number;
    created_at: string;
    items: OrderItem[];
}

export interface AuditEntry {
    id: number;
    actor_id: number;
    actor_role: string;
    action: string;
    target_type: string;
    target_id: string;
    request_id: string;
    ip: string;
    created_at: string;
    before: unknown;
    after: unknown;
}

export interface ConfigSnapshot {
    age_ms: number;
    stale: boolean;
    connected: boolean;
    keys: Record<string, string>;
}

export interface OpsStatus {
    mysql_ok: boolean;
    config: {
        age_ms: number;
        stale: boolean;
        connected: boolean;
        keys: number;
    };
    cairnkv: Record<string, unknown>;
}

export class ApiRequestError extends Error {
    status: number;
    code: string;

    constructor(status: number, code: string, message: string) {
        super(message);
        this.status = status;
        this.code = code;
    }
}

async function request<T>(path: string, options: RequestInit = {}): Promise<T> {
    const response = await fetch(`/api/v1${path}`, {
        credentials: 'include',
        ...options,
        headers: {
            'Content-Type': 'application/json',
            ...(options.headers ?? {})
        }
    });

    const text = await response.text();
    let body: unknown = null;
    if (text.length > 0) {
        try {
            body = JSON.parse(text);
        } catch {
            body = null;
        }
    }

    if (!response.ok) {
        const error = (body as { error?: { code?: string; message?: string } } | null)?.error;
        throw new ApiRequestError(
            response.status,
            error?.code ?? 'error',
            error?.message ?? response.statusText
        );
    }
    return body as T;
}

export const api = {
    get: <T>(path: string) => request<T>(path),
    post: <T>(path: string, body?: unknown, headers?: Record<string, string>) =>
        request<T>(path, {
            method: 'POST',
            body: body === undefined ? undefined : JSON.stringify(body),
            headers
        }),
    put: <T>(path: string, body?: unknown) =>
        request<T>(path, { method: 'PUT', body: JSON.stringify(body) }),
    patch: <T>(path: string, body?: unknown) =>
        request<T>(path, { method: 'PATCH', body: JSON.stringify(body) }),
    delete: <T>(path: string) => request<T>(path, { method: 'DELETE' })
};

export function formatCents(cents: number): string {
    return `¥${(cents / 100).toFixed(2)}`;
}
