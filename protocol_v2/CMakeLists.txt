cmake_minimum_required(VERSION 3.16)
project(PayableLPSI)
# 在 project(PayableLPSI) 下方添加
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(OpenSSL REQUIRED)
find_package(OpenMP REQUIRED)
find_package(SEAL 4.1 REQUIRED)
find_package(Kuku 2.1 QUIET REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# 检查 Kuku 是否找到
if(NOT Kuku_FOUND)
    message(FATAL_ERROR "Microsoft Kuku: not found")
else()
    message(STATUS "Microsoft Kuku: found")
endif()

# 设置 libOTe 路径 (使用已编译的版本)
set(LIBOTE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../libOTe)
set(LIBOTE_INSTALL_DIR ${LIBOTE_ROOT}/out/install/linux)
set(LIBOTE_BUILD_DIR ${LIBOTE_ROOT}/out/build/linux)
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${LIBOTE_INSTALL_DIR})

# 添加 PIRANA 目录
set(PIRANA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../PIRANA/src")

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/thirdparty/oos_OT
    ${PIRANA_SRC_DIR}
    ${LIBOTE_ROOT}
    ${LIBOTE_ROOT}/libOTe
    ${LIBOTE_ROOT}/cryptoTools
    ${LIBOTE_ROOT}/cryptoTools/cryptoTools
    ${LIBOTE_ROOT}/thirdparty
    ${LIBOTE_ROOT}/out/coproto
    ${LIBOTE_ROOT}/out/macoro
    ${LIBOTE_BUILD_DIR}
    ${LIBOTE_BUILD_DIR}/libOTe
    ${LIBOTE_BUILD_DIR}/cryptoTools
    ${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools
    ${LIBOTE_BUILD_DIR}/coproto
    ${LIBOTE_BUILD_DIR}/coproto/coproto
    ${LIBOTE_BUILD_DIR}/macoro
    ${LIBOTE_BUILD_DIR}/macoro/macoro
    ${LIBOTE_INSTALL_DIR}/include
    ${OpenSSL_INCLUDE_DIRS}
)

# 库目录
link_directories(
    ${LIBOTE_BUILD_DIR}/libOTe
    ${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools
    ${LIBOTE_BUILD_DIR}/coproto/coproto
    ${LIBOTE_BUILD_DIR}/macoro/macoro
    ${LIBOTE_BUILD_DIR}/thirdparty/KyberOT
    ${LIBOTE_BUILD_DIR}/thirdparty/SimplestOT
    ${LIBOTE_INSTALL_DIR}/lib
)

# PIRANA源文件
set(PIRANA_SOURCES
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/client.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/server.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/pir_parms.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/utils.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/test.cc
    ${CMAKE_SOURCE_DIR}/../PIRANA/src/batch_pir.cc
)

# LPSI源文件
set(LPSI_SOURCES
    src/sender.cpp
    src/receiver.cpp
    src/oos_ot.cpp
    src/main.cpp
)

# OT库源文件
set(OT_SOURCES
    thirdparty/oos_OT/oos_OT.cpp
)

# 生成可执行文件
add_executable(lpsi_test ${LPSI_SOURCES} ${PIRANA_SOURCES} ${OT_SOURCES})

# 设置链接选项
set_target_properties(lpsi_test PROPERTIES
    LINK_FLAGS "-no-pie"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 链接库 (使用静态库路径)
target_link_libraries(lpsi_test
    ${LIBOTE_BUILD_DIR}/libOTe/liblibOTe.a
    ${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools/libcryptoTools.a
    ${LIBOTE_BUILD_DIR}/coproto/coproto/libcoproto.a
    ${LIBOTE_BUILD_DIR}/macoro/macoro/libmacoro.a
    ${LIBOTE_BUILD_DIR}/thirdparty/KyberOT/libKyberOT.a
    ${LIBOTE_BUILD_DIR}/thirdparty/SimplestOT/libSimplestOT.a
    SEAL::seal
    Kuku::kuku
    OpenSSL::SSL
    OpenSSL::Crypto
    OpenMP::OpenMP_CXX
    ${Boost_LIBRARIES}
    ${LIBOTE_INSTALL_DIR}/lib/libsodium.a
    pthread
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(lpsi_test OpenMP::OpenMP_CXX)
endif()

# 编译选项
target_compile_options(lpsi_test PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -O3
    -march=native
    -fcoroutines
    -fPIC
)

# 打印配置信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "SEAL found: ${SEAL_FOUND}")
message(STATUS "Kuku found: ${Kuku_FOUND}")
message(STATUS "libOTe directory: ${LIBOTE_ROOT}")
message(STATUS "libOTe build directory: ${LIBOTE_BUILD_DIR}")
