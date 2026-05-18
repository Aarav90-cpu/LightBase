cmake_minimum_required(VERSION 3.22)
project(LightBaseCore C)

set(CMAKE_C_STANDARD 99)

# 1. Force Extreme Release Optimizations and Strip Debug Symbols
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto -s")

# 2. Define the Shared Library Target Framework
add_library(core SHARED src/engine.c)

# 3. Bind the Local Include Headers Directory
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 4. Link the System's Native Dynamic Linking Binary Library (dl)
target_link_libraries(core dl)

# 5. PRODUCTION BUNDLING LAYOUT DESIGN
# Define our target installation directories on the host filesystem
set(DIST_DIR "/home/aarav/LightBase/dist")

install(TARGETS core
        LIBRARY DESTINATION ${DIST_DIR}/lib
        RUNTIME DESTINATION ${DIST_DIR}/bin)

install(FILES include/engine.h
        DESTINATION ${DIST_DIR}/include)