cmake_minimum_required(VERSION 3.16)
project(EvoRL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(EVORL_NATIVE "Auf die lokale CPU optimieren (-march=native, ca. 2x schneller); Binary laeuft dann nur auf aehnlichen CPUs" ON)
option(EVORL_BUILD_TESTS "Unit-/Smoke-Tests bauen (ctest)" ON)

# ---- Kernbibliothek (alles ausser main.cpp) ----
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(evorl_core STATIC ${CORE_SOURCES})
target_include_directories(evorl_core PUBLIC src)

# OpenMP (optional, aber dringend empfohlen fuer Performance)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(evorl_core PUBLIC OpenMP::OpenMP_CXX)
else()
    message(STATUS "OpenMP nicht gefunden - Training laeuft single-threaded")
endif()

# Sockets / Threads
if(WIN32)
    target_link_libraries(evorl_core PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(evorl_core PUBLIC Threads::Threads)
endif()

if(MSVC)
    target_compile_options(evorl_core PUBLIC /W3 /utf-8)
    target_compile_definitions(evorl_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(evorl_core PUBLIC -Wall -Wextra -Wno-unknown-pragmas)
    # -O3 aktiviert die Auto-Vektorisierung der Skalarprodukte im GRU-Forward-Pass
    target_compile_options(evorl_core PUBLIC "$<$<CONFIG:Release>:-O3>")
    if(EVORL_NATIVE)
        target_compile_options(evorl_core PUBLIC -march=native)
    endif()
endif()

add_executable(EvoRL src/main.cpp)
target_link_libraries(EvoRL PRIVATE evorl_core)

# ---- Tests ----
if(EVORL_BUILD_TESTS)
    enable_testing()
    add_executable(evorl_tests tests/tests.cpp)
    target_link_libraries(evorl_tests PRIVATE evorl_core)
    target_compile_definitions(evorl_tests PRIVATE EVORL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME evorl_tests COMMAND evorl_tests)
endif()
