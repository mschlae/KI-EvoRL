cmake_minimum_required(VERSION 3.16)
project(EvoRL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB SOURCES "src/*.cpp")
add_executable(EvoRL ${SOURCES})

# OpenMP (optional, aber dringend empfohlen fuer Performance)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(EvoRL PUBLIC OpenMP::OpenMP_CXX)
endif()

# Sockets
if(WIN32)
    target_link_libraries(EvoRL PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(EvoRL PUBLIC Threads::Threads)
endif()
