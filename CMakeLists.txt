cmake_minimum_required(VERSION 3.16)
project(orderbook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(orderbook src/order_book.cpp)
target_include_directories(orderbook PUBLIC include)

# Same sources, compiled with new/delete instead of the pool, for comparison.
add_library(orderbook_heap src/order_book.cpp)
target_include_directories(orderbook_heap PUBLIC include)
target_compile_definitions(orderbook_heap PUBLIC OB_USE_HEAP_ALLOCATOR)

add_executable(demo src/main.cpp)
target_link_libraries(demo orderbook)

add_executable(tests tests/tests.cpp)
target_link_libraries(tests orderbook)

add_executable(bench_pool bench/benchmark.cpp)
target_link_libraries(bench_pool orderbook)

add_executable(bench_heap bench/benchmark.cpp)
target_link_libraries(bench_heap orderbook_heap)
