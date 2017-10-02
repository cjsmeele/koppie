cmake_minimum_required(VERSION 2.8)

project(koppie CXX)

find_package(LLVM REQUIRED CONFIG)

add_compile_options(-Wall
                    -Wextra
                    -pedantic-errors
                    -std=c++17
                    -g3
                    -ggdb
                    -O3)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(koppie koppie.cc)

llvm_map_components_to_libnames(llvm_libs support core passes native)
target_link_libraries(koppie ${llvm_libs})
