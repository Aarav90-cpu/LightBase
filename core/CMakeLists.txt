cmake_minimum_required(VERSION 3.22)
project(LightBaseCore C)

set(CMAKE_C_STANDARD 99)

# ─── FIX: Add -z noexecstack to link parameters defensively ───
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto -s -z noexecstack")

# Define the Shared Library Target Framework
add_library(core SHARED src/engine.c src/arena.c src/sys_monitor.c src/tlv.c src/storage.c)

# Bind the Local Include Headers Directory
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link the System's Native Dynamic Linking Binary Library (dl)
target_link_libraries(core dl)

# PRODUCTION BUNDLING LAYOUT DESIGN
set(DIST_DIR "/home/aarav/LightBase/dist")

install(TARGETS core
        LIBRARY DESTINATION ${DIST_DIR}/lib
        RUNTIME DESTINATION ${DIST_DIR}/bin)

install(FILES include/engine.h
        DESTINATION ${DIST_DIR}/include)