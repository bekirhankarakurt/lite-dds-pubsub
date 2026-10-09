cmake_minimum_required(VERSION 3.16)
project(lite_dds_pubsub VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LDDS_BUILD_TESTS "Build the unit tests" ON)
option(LDDS_BUILD_APPS "Build the example applications and the spy tool" ON)
option(LDDS_BUILD_BENCH "Build the latency/throughput benchmark" ON)
option(LDDS_WERROR "Treat compiler warnings as errors" OFF)
option(LDDS_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(LDDS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

add_library(ldds_warnings INTERFACE)
target_compile_options(ldds_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
if(LDDS_WERROR)
  target_compile_options(ldds_warnings INTERFACE -Werror)
endif()

add_library(ldds
  src/capture.cpp
  src/describe.cpp
  src/participant.cpp
  src/transport_memory.cpp
  src/transport_udp.cpp
  src/wire.cpp)
add_library(ldds::ldds ALIAS ldds)
target_include_directories(ldds PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(ldds PUBLIC Threads::Threads PRIVATE ldds_warnings)

if(LDDS_BUILD_APPS OR LDDS_BUILD_BENCH)
  add_library(ldds_app_common INTERFACE)
  target_include_directories(ldds_app_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/apps/common)
  target_link_libraries(ldds_app_common INTERFACE ldds::ldds ldds_warnings)
endif()

if(LDDS_BUILD_APPS)
  add_subdirectory(apps)
endif()

if(LDDS_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(LDDS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
