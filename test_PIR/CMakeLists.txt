cmake_minimum_required(VERSION 3.16)
project(PayableLPSI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(OpenSSL REQUIRED)
find_package(OpenMP REQUIRED)
find_package(SEAL 4.1 REQUIRED)
find_package(Kuku 2.1 QUIET REQUIRED)

# 检查 Kuku 是否找到
if(NOT Kuku_FOUND)
    message(FATAL_ERROR "Microsoft Kuku: not found")
else()
    message(STATUS "Microsoft Kuku: found")
endif()

# 添加 PIRANA 目录
set(PIRANA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../PIRANA/src")

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${PIRANA_SRC_DIR}
    ${OpenSSL_INCLUDE_DIRS}
)

# PIRANA源文件
set(PIRANA_SOURCES
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/client.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/server.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/pir_parms.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/utils.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/test.cc
)

# LPSI源文件
set(LPSI_SOURCES
    test.cpp
)

# 编译选项
add_compile_options(-Wall -Wextra -O3 -march=native)

if(OpenMP_CXX_FOUND)
    add_compile_options(${OpenMP_CXX_FLAGS})
endif()

# 生成可执行文件
add_executable(pirana_test ${LPSI_SOURCES} ${PIRANA_SOURCES})

# 链接库
target_link_libraries(pirana_test
    SEAL::seal
    Kuku::kuku
    ${OpenSSL_LIBRARIES}
    crypto
    pthread
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(pirana_test OpenMP::OpenMP_CXX)
endif()

# 设置输出目录
set_target_properties(pirana_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 打印配置信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "SEAL found: ${SEAL_FOUND}")
message(STATUS "Kuku found: ${Kuku_FOUND}")
