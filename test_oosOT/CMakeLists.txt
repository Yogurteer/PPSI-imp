cmake_minimum_required(VERSION 3.15)
project(LPSI_Implementation)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置 libOTe 路径
set(LIBOTE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../libOTe)
set(LIBOTE_INSTALL_DIR ${LIBOTE_ROOT}/out/install/linux)
set(LIBOTE_BUILD_DIR ${LIBOTE_ROOT}/out/build/linux)
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${LIBOTE_INSTALL_DIR})

# 查找必要的库
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
)

# 源文件
file(GLOB SOURCES "src/*.cpp")

# 设置 libOTe 库路径
set(LIBOTE_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libOTe/out/build/linux)
link_directories(
    ${LIBOTE_BUILD_DIR}/libOTe
    ${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools
    ${LIBOTE_BUILD_DIR}/coproto/coproto
    ${LIBOTE_BUILD_DIR}/macoro/macoro
    ${LIBOTE_BUILD_DIR}/thirdparty/KyberOT
    ${LIBOTE_BUILD_DIR}/thirdparty/SimplestOT
    ${LIBOTE_INSTALL_DIR}/lib
)

# 创建可执行文件
add_executable(oosOT_Test ${SOURCES})

# 设置链接选项
set_target_properties(oosOT_Test PROPERTIES
    LINK_FLAGS "-no-pie"
)

# 链接库
target_link_libraries(oosOT_Test 
    ${LIBOTE_BUILD_DIR}/libOTe/liblibOTe.a
    ${LIBOTE_BUILD_DIR}/cryptoTools/cryptoTools/libcryptoTools.a
    ${LIBOTE_BUILD_DIR}/coproto/coproto/libcoproto.a
    ${LIBOTE_BUILD_DIR}/macoro/macoro/libmacoro.a
    ${LIBOTE_BUILD_DIR}/thirdparty/KyberOT/libKyberOT.a
    ${LIBOTE_BUILD_DIR}/thirdparty/SimplestOT/libSimplestOT.a
    OpenSSL::SSL 
    OpenSSL::Crypto
    ${Boost_LIBRARIES}
    ${LIBOTE_INSTALL_DIR}/lib/libsodium.a
    pthread
)

# 编译选项
target_compile_options(oosOT_Test PRIVATE 
    -Wall 
    -Wextra 
    -Wno-unused-parameter  # 禁用未使用参数的警告
    -O3
    -march=native
    -fcoroutines
    -fPIC
)

# 如果是Debug模式，添加调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(oosOT_Test PRIVATE -g -O0)
endif()

# 安装目标
# install(TARGETS oosOT_Test DESTINATION bin)