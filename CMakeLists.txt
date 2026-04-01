cmake_minimum_required(VERSION 3.20)
project(celeritas LANGUAGES C VERSION 0.10.2)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

message(STATUS "Building ${PROJECT_NAME} v${PROJECT_VERSION}")

# --- Threads (for -pthread) ---
find_package(Threads REQUIRED)

# --- OpenSSL ---
find_package(OpenSSL REQUIRED)
message(STATUS "Found OpenSSL ${OPENSSL_VERSION}")

# --- Build flags (compiler only, no linker flags here) ---
option(DEBUG_BUILD "Enable debug CFLAGS" OFF)
if(DEBUG_BUILD)
    set(CMAKE_C_FLAGS "-march=native -Wall -Wextra -Werror -O0 -g -D_GNU_SOURCE -fsanitize=address,undefined")
    message(STATUS "Building with debug symbols and sanitizers.")
else()
    set(CMAKE_C_FLAGS "-march=native -Wall -Wextra -Werror -O2 -D_GNU_SOURCE")
endif()
message(STATUS "CFLAGS: ${CMAKE_C_FLAGS}")

# --- Sources ---
file(GLOB CELERITAS_SOURCES "src/*.c")
list(REMOVE_ITEM CELERITAS_SOURCES "${PROJECT_SOURCE_DIR}/src/main.c")

add_executable(celeritas src/main.c ${CELERITAS_SOURCES})

# --- Link libraries properly ---
target_link_libraries(celeritas
        PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)
