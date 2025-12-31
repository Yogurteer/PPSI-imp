cmake_minimum_required(VERSION 3.16)
project(CuckooHash VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置编译器标志
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 设置默认构建类型为Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 包含头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 创建头文件库
add_library(cuckoo_hash INTERFACE)
target_include_directories(cuckoo_hash INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# 创建可执行文件 - 测试程序
add_executable(test_cuckoo_hash test_cuckoo_hash.cpp)
target_link_libraries(test_cuckoo_hash cuckoo_hash)

# 如果系统支持OpenMP，添加并行支持
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_cuckoo_hash OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP found and enabled")
else()
    message(STATUS "OpenMP not found")
endif()

# 添加测试
enable_testing()
add_test(NAME CuckooHashTest COMMAND test_cuckoo_hash)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 打印配置信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")