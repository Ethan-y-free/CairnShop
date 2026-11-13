ALTER TABLE users
    MODIFY COLUMN email VARCHAR(254) NULL,
    ADD COLUMN phone VARCHAR(20) NULL AFTER email,
    ADD UNIQUE KEY uk_users_phone (phone);

ALTER TABLE products
    ADD COLUMN original_price_cents BIGINT NULL AFTER price_cents,
    ADD COLUMN image_url VARCHAR(512) NOT NULL DEFAULT '' AFTER description,
    ADD COLUMN detail TEXT NULL AFTER image_url;
