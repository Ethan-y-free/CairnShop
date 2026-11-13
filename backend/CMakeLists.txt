find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

find_program(MYSQL_CONFIG_EXECUTABLE NAMES mysql_config mariadb_config)
if (MYSQL_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND ${MYSQL_CONFIG_EXECUTABLE} --include
        OUTPUT_VARIABLE MYSQL_CONFIG_INCLUDE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REGEX REPLACE "^-I" "" MYSQL_CONFIG_INCLUDE "${MYSQL_CONFIG_INCLUDE}")
    string(STRIP "${MYSQL_CONFIG_INCLUDE}" MYSQL_CONFIG_INCLUDE)
endif()

find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h
    HINTS ${MYSQL_CONFIG_INCLUDE}
    PATH_SUFFIXES mysql mariadb
)
find_library(MYSQL_LIBRARY NAMES mysqlclient mariadb)

if (NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR
        "MySQL client library not found. On Ubuntu/Debian run: "
        "sudo apt install libmysqlclient-dev")
endif()

add_library(cairnshop_core STATIC
    src/config.cc
    src/logging.cc
    src/mysql.cc
    src/crypto.cc
    src/auth.cc
    src/catalog.cc
    src/config_service.cc
    src/cart.cc
    src/promo.cc
    src/order.cc
    src/audit.cc
    src/config_admin.cc
    src/rate_limiter.cc
    src/metrics.cc
)

target_include_directories(cairnshop_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
    ${MYSQL_INCLUDE_DIR}
)

target_link_libraries(cairnshop_core PUBLIC
    OpenSSL::Crypto
    Threads::Threads
    cairnkv_client
    ${MYSQL_LIBRARY}
)

add_executable(cairnshop-backend src/main.cc)
target_link_libraries(cairnshop-backend PRIVATE cairnshop_core)

add_executable(cairnshop_tests
    tests/crypto_test.cc
    tests/validation_test.cc
    tests/promo_test.cc
)
target_link_libraries(cairnshop_tests PRIVATE
    cairnshop_core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME cairnshop_tests COMMAND cairnshop_tests)
