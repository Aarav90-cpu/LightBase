cmake_minimum_required(VERSION 3.22)
project(LightBaseCore C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 🚀 HIGH-PERFORMANCE PRODUCTION COMPILER AND LINKER PARAMETERS
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto -s")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-Wl,-z,noexecstack -flto")

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        /usr/include
        /usr/include/quickjs
        /usr/local/include
)

# ============================================================================
# 📦 TARGET 1: THE SHARED LIBRARY CORE (`libcore.so`)
# ============================================================================
add_library(core SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/arena.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sys_monitor.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tlv.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/storage.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/collections.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/assert_engine.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/database.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread_pool.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/schema_docs.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/git_sync.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ai_core.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/git_watcher.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/security.c
        src/crypto_vault.c
)

find_library(QUICKJS_LIB NAMES quickjs qjs PATHS /usr/lib/quickjs /usr/lib/x86_64-linux-gnu/quickjs /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib)
if(NOT QUICKJS_LIB)
    message(FATAL_ERROR "Could not find quickjs library")
endif()

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(core ssl crypto sqlite3 dl m ${QUICKJS_LIB} pthread git2)


# ============================================================================
# ⚡ TARGET 2: THE STANDALONE HEADLESS EXEC-BINARY (`lb-cli`)
# ============================================================================
add_executable(lb-cli
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lb_cli.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/git_sync.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ai_core.c
        src/crypto_vault.c    # 🎯 EXTEND SYMBOLS LOCALLY TO HEADLESS BINARY!
)

# Enforce explicit RPATH headers so lb-cli tracks its library files dynamically at execution runtime
set_target_properties(lb-cli PROPERTIES LINK_FLAGS "-Wl,-rpath,/home/aarav/LightBase/dist/lib")
target_link_libraries(lb-cli core ssl crypto sqlite3 dl m ${QUICKJS_LIB} pthread git2)


# ============================================================================
# 🚚 PRODUCTION BUNDLING DISTRIBUTION LAYOUT DESIGN
# ============================================================================
set(DIST_DIR "/home/aarav/LightBase/dist")

install(TARGETS core lb-cli
        LIBRARY DESTINATION ${DIST_DIR}/lib
        RUNTIME DESTINATION ${DIST_DIR}/bin)

install(FILES include/engine.h
        DESTINATION ${DIST_DIR}/include)