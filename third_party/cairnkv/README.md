# CairnKV client (vendored)

Copied from https://github.com/Ethan-y-Free/CairnKV at tag **v1.0.1**.

Only the client-side sources are included:

```
include/cairnkv/   public SDK headers
src/common/        status / encoding / logging
src/rpc/           frame + message encoding
src/client/        blocking client implementation
```

The server, RocksDB state machine, tools, and tests are intentionally absent;
CairnShop links only `cairnkv_client` and does not need RocksDB.

## Upgrade

1. Copy the same subset from the new CairnKV tag over this directory.
2. Rebuild CairnShop; the client API is frozen from v1.0 onward.
