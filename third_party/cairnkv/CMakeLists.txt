# Client-only build of CairnKV (vendored).
# The full CairnKV tree also builds the server, RocksDB state machine, tools,
# and tests; CairnShop only needs libcairnkv and its wire-protocol code.

add_library(cairnkv_client STATIC
    src/common/status.cc
    src/common/encoding.cc
    src/common/logging.cc
    src/rpc/frame.cc
    src/rpc/message.cc
    src/client/client.cc
)

target_include_directories(cairnkv_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cairnkv_client PUBLIC Threads::Threads)
