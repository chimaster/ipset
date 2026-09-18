cmake_minimum_required(VERSION 3.10)
project(ipset_tarpit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIPSET REQUIRED IMPORTED_TARGET libipset)

add_library(ipset_wrapper src/ipset_wrapper.cpp)
target_include_directories(ipset_wrapper PUBLIC include)
target_link_libraries(ipset_wrapper PUBLIC PkgConfig::LIBIPSET)
target_compile_options(ipset_wrapper PRIVATE -Wall -Wextra)

add_executable(waf_tarpit_demo examples/waf_tarpit_demo.cpp)
target_link_libraries(waf_tarpit_demo PRIVATE ipset_wrapper)

enable_testing()
add_subdirectory(tests)
