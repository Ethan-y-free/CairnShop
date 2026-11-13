// Client-side mirrors of the backend's validation rules in
// backend/src/crypto.cc. The backend remains the source of truth.

const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export function trim(value: string): string {
    return value.trim();
}

export function isValidEmail(value: string): boolean {
    const email = value.trim();
    if (email.length < 3 || email.length > 254) {
        return false;
    }
    return EMAIL_PATTERN.test(email);
}

export function normalizePhone(value: string): string {
    let digits = value.replace(/[\s\-()]/g, '');
    if (digits.startsWith('+')) {
        digits = digits.slice(1);
    }
    if (digits.length === 13 && digits.startsWith('86')) {
        digits = digits.slice(2);
    }
    return digits;
}

export function isValidPhone(value: string): boolean {
    return /^1[3-9]\d{9}$/.test(normalizePhone(value));
}

export function isValidPassword(value: string): boolean {
    return value.length >= 8 && value.length <= 128;
}

export function isValidDisplayName(value: string): boolean {
    const name = value.trim();
    if (name.length < 1 || name.length > 64) {
        return false;
    }
    return !/[\u0000-\u001f\u007f]/.test(name);
}
